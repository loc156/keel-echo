#pragma once
// ============================================================================
//  KeelBigIO.hpp —— 复杂 IO（大文件 / 流），独立于 KeelIO.hpp 那套简单 IO
// ----------------------------------------------------------------------------
//  和 FastIO 的分工：
//    * FastIO  = 快进快出：stdout/stderr、一次性整读整写。没有句柄，没有缓冲策略。
//    * BigIO   = 面向大文件与流：句柄、分块读、逐行读、seek、复制、原子写、
//                目录操作，以及内存流（stream.*）用来先把大块内容攒起来再落盘。
//  三个库（FastIO / BigIO / DynSource）互不依赖，可以只装其中一个。
//
//  指令表：
//    打开 / 关闭
//      big.open   路径 模式 -- 句柄|nil      模式：r / w / a / rw
//                                            （也接受 read / write / append / readwrite / r+）
//      big.close  句柄 -- 布尔
//    读写（句柄优先，值在栈顶）
//      big.read   句柄 字节数 -- 字符串|nil   最多读 n 字节；n<=0 读剩余（受 max_alloc 限）
//      big.readln 句柄 -- 字符串|nil          读一行（去掉 \r\n）；EOF 给 nil
//      big.write  句柄 内容 -- 布尔           先入写缓冲，flush/close 时落盘
//    位置 / 状态
//      big.seek   句柄 位置 -- 新位置|nil
//      big.tell   句柄 -- 位置
//      big.size   句柄 -- 字节数
//      big.eof    句柄 -- 布尔
//      big.flush  句柄 -- 布尔
//    整文件
//      big.slurp  路径 -- 字符串|nil          上限 = d.max_alloc
//      big.spit   内容 路径 -- 布尔           原子写（临时文件 + 改名）
//      big.append 内容 路径 -- 布尔
//      big.copy   源 目标 -- 字节数|nil       256KB 一块，不整读进内存
//    文件系统（只做最常用的，不做递归删除）
//      big.stat   路径 -- map {exists,size,is_dir,mtime}
//      big.list   路径 -- list（名字，已排序）
//      big.mkdir  路径 -- 布尔（父目录一起建）
//      big.remove 路径 -- 布尔（文件或空目录）
//      big.rename 源 目标 -- 布尔（Windows 上不覆盖已存在的目标）
//    内存流
//      stream.new   -- 流句柄
//      stream.write 流 内容 -- 布尔
//      stream.read  流 字节数 -- 字符串    n<=0 读剩余
//      stream.seek  流 位置 -- 新位置
//      stream.size  流 -- 字节数
//      stream.save  流 路径 -- 布尔       原子写
//      stream.free  流 -- 布尔
//
//  上限：单个字符串分配受 d.max_alloc（默认 16MB，可用 `limit` 调到 256MB 硬顶）；
//  整文件读写同样卡这个上限 —— 再大的文件请用句柄分块读，别 slurp。
// ============================================================================

#include "Keel.hpp"
#include "KeelIOCore.hpp"
#include "KeelInstanceState.hpp"
#include "KeelUtil.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keelbig
{
    namespace u = keel::util;
    namespace ioc = keelio_impl;
    namespace fs = std::filesystem;

    constexpr std::size_t kReadChunk = 64 * 1024;  // readln 的内部缓冲块
    constexpr std::size_t kWriteChunk = 256 * 1024; // 写缓冲到这么大就落盘

    struct State
    {
        struct File
        {
            ioc::native_handle h = ioc::invalid_handle();
            bool readable = false;
            bool writable = false;
            bool eof = false;
            std::string rbuf; // 已读进缓冲、还没被消费的字节
            std::size_t rpos = 0;
            std::string wbuf;

            File() = default;
            File(const File &) = delete;
            File &operator=(const File &) = delete;
            ~File() { close(); }

            void close()
            {
                if (!ioc::valid(h))
                    return;
                flush_w();
                ioc::io_close(h);
                h = ioc::invalid_handle();
            }

            bool flush_w()
            {
                if (wbuf.empty())
                    return true;
                const bool ok = ioc::io_write(h, wbuf.data(), wbuf.size());
                wbuf.clear();
                return ok;
            }

            std::size_t pending_read() const { return rbuf.size() - rpos; }
            void clear_read()
            {
                rbuf.clear();
                rpos = 0;
            }

            /// 再读一块进 rbuf。返回 false = IO 错；got 用 pending_read() 看。
            bool fill(std::size_t want)
            {
                if (rpos > 0)
                {
                    rbuf.erase(0, rpos);
                    rpos = 0;
                }
                const std::size_t old = rbuf.size();
                rbuf.resize(old + want);
                std::size_t got = 0;
                if (!ioc::io_read(h, rbuf.data() + old, want, got))
                {
                    rbuf.resize(old);
                    return false;
                }
                rbuf.resize(old + got);
                if (got < want)
                    eof = true;
                return true;
            }
        };

        std::unordered_map<int, std::unique_ptr<File>> files;
        int next_file = 1;

        struct Stream
        {
            std::string data;
            std::size_t pos = 0;
        };
        std::unordered_map<int, Stream> streams;
        int next_stream = 1;
    };

    using States = keel::StateTable<State>;

    // ------------------------------------------------------------------ 内部
    namespace detail
    {
        inline std::string num_text(double v)
        {
            std::string s;
            ioc::append_number(s, v);
            return s;
        }

        inline State::File *file_at(keel::KeelData &d, double id, const char *what)
        {
            State &st = States::get(d);
            auto it = st.files.find(static_cast<int>(id));
            if (it == st.files.end())
            {
                u::err(what, "无效的文件句柄: " + num_text(id));
                return nullptr;
            }
            return it->second.get();
        }

        inline State::Stream *stream_at(keel::KeelData &d, double id, const char *what)
        {
            State &st = States::get(d);
            auto it = st.streams.find(static_cast<int>(id));
            if (it == st.streams.end())
            {
                u::err(what, "无效的流句柄: " + num_text(id));
                return nullptr;
            }
            return &it->second;
        }

        inline bool parse_mode(const std::string &mode, ioc::IoMode &out)
        {
            if (mode == "r" || mode == "read")
            {
                out = ioc::IoMode::Read;
                return true;
            }
            if (mode == "w" || mode == "write")
            {
                out = ioc::IoMode::Write;
                return true;
            }
            if (mode == "a" || mode == "append")
            {
                out = ioc::IoMode::Append;
                return true;
            }
            if (mode == "rw" || mode == "r+" || mode == "readwrite")
            {
                out = ioc::IoMode::ReadWrite;
                return true;
            }
            return false;
        }

        inline std::string err_text(const std::error_code &ec)
        {
            return ec.message();
        }
    } // namespace detail

    // ------------------------------------------------------------------ 指令
    inline void big_register(keel::Keel &a)
    {
        using namespace detail;

        // ---------------------------------------------------------- 打开 / 关闭
        a.register_func("big.open", KeelF {
            if (!u::need(d, 2, "big.open"))
                return;
            std::string mode;
            if (!u::pop_text(d, "big.open", mode))
                return;
            std::string path;
            if (!u::pop_text(d, "big.open", path))
                return;
            if (!u::check_sandbox(d, path, "big.open"))
            {
                u::push(d, keel::make_nil());
                return;
            }

            ioc::IoMode m;
            if (!parse_mode(mode, m))
            {
                u::err("big.open", "模式只能是 r/w/a/rw，收到 '" + mode + "'");
                u::push(d, keel::make_nil());
                return;
            }

            std::string err;
            ioc::native_handle h = ioc::io_open(path, m, err);
            if (!ioc::valid(h))
            {
                u::err("big.open", "cannot open '" + path + "': " + err);
                u::push(d, keel::make_nil());
                return;
            }

            State &st = States::get(d);
            const int id = st.next_file++;
            auto f = std::make_unique<State::File>();
            f->h = h;
            f->readable = (m != ioc::IoMode::Write && m != ioc::IoMode::Append);
            f->writable = (m != ioc::IoMode::Read);
            st.files.emplace(id, std::move(f));
            u::push(d, keel::make_number(id));
        });

        a.register_func("big.close", KeelF {
            double id = 0;
            if (!u::pop_num(d, "big.close", id))
                return;
            State &st = States::get(d);
            auto it = st.files.find(static_cast<int>(id));
            if (it == st.files.end())
            {
                u::err("big.close", "无效的文件句柄: " + num_text(id));
                u::push(d, keel::make_bool(false));
                return;
            }
            it->second->close();
            st.files.erase(it);
            u::push(d, keel::make_bool(true));
        });

        // ---------------------------------------------------------- 读
        a.register_func("big.read", KeelF {
            double n = 0;
            if (!u::pop_num(d, "big.read", n))
                return;
            double id = 0;
            if (!u::pop_num(d, "big.read", id))
                return;

            State::File *f = file_at(d, id, "big.read");
            if (!f)
            {
                u::push(d, keel::make_nil());
                return;
            }
            if (!f->readable)
            {
                u::err("big.read", "句柄不是以读模式打开的");
                u::push(d, keel::make_nil());
                return;
            }

            std::size_t want = 0;
            if (n <= 0)
            {
                // "读剩余"：内部缓冲里没消费的 + 文件里还没读的，一起算
                std::uint64_t size = 0;
                std::uint64_t cur = 0;
                if (!ioc::io_size(f->h, size) || !ioc::io_tell(f->h, cur))
                {
                    u::err("big.read", "取文件位置失败");
                    u::push(d, keel::make_nil());
                    return;
                }
                const std::uint64_t rest = size > cur ? size - cur : 0;
                const std::uint64_t total = rest + f->pending_read();
                if (total > d.max_alloc)
                {
                    u::err("big.read", "剩余 " + std::to_string(total) + " 字节超过 max_alloc（" +
                                           std::to_string(d.max_alloc) + "），请分块读");
                    u::push(d, keel::make_nil());
                    return;
                }
                want = static_cast<std::size_t>(total);
            }
            else
                want = static_cast<std::size_t>(n);

            if (want > d.max_alloc)
            {
                u::err("big.read", "一次读 " + std::to_string(want) + " 字节超过 max_alloc（" +
                                       std::to_string(d.max_alloc) + "）");
                u::push(d, keel::make_nil());
                return;
            }

            std::string out;
            out.reserve(std::min<std::size_t>(want, 1u << 20));

            // 先吃内部缓冲
            const std::size_t take = std::min(f->pending_read(), want);
            out.append(f->rbuf, f->rpos, take);
            f->rpos += take;

            // 不够就直接读文件
            while (out.size() < want && !f->eof)
            {
                const std::size_t need = want - out.size();
                const std::size_t old = out.size();
                out.resize(old + need);
                std::size_t got = 0;
                if (!ioc::io_read(f->h, out.data() + old, need, got))
                {
                    u::err("big.read", "读文件失败");
                    u::push(d, keel::make_nil());
                    return;
                }
                out.resize(old + got);
                if (got < need)
                    f->eof = true;
            }
            u::push(d, keel::make_string(std::move(out)));
        });

        a.register_func("big.readln", KeelF {
            double id = 0;
            if (!u::pop_num(d, "big.readln", id))
                return;

            State::File *f = file_at(d, id, "big.readln");
            if (!f)
            {
                u::push(d, keel::make_nil());
                return;
            }
            if (!f->readable)
            {
                u::err("big.readln", "句柄不是以读模式打开的");
                u::push(d, keel::make_nil());
                return;
            }

            for (;;)
            {
                const std::size_t nl = f->rbuf.find('\n', f->rpos);
                if (nl != std::string::npos)
                {
                    std::string line = f->rbuf.substr(f->rpos, nl - f->rpos);
                    f->rpos = nl + 1;
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    u::push(d, keel::make_string(std::move(line)));
                    return;
                }
                if (f->eof)
                {
                    if (f->rpos >= f->rbuf.size())
                    {
                        u::push(d, keel::make_nil()); // 干净 EOF
                        return;
                    }
                    std::string line = f->rbuf.substr(f->rpos);
                    f->clear_read();
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    u::push(d, keel::make_string(std::move(line)));
                    return;
                }
                if (!f->fill(kReadChunk))
                {
                    u::err("big.readln", "读文件失败");
                    u::push(d, keel::make_nil());
                    return;
                }
                if (f->pending_read() > d.max_alloc)
                {
                    u::err("big.readln", "单行太长（超过 max_alloc）");
                    f->clear_read();
                    u::push(d, keel::make_nil());
                    return;
                }
            }
        });

        // ---------------------------------------------------------- 写
        a.register_func("big.write", KeelF {
            if (!u::need(d, 2, "big.write"))
                return;
            std::string text;
            if (!u::pop_text(d, "big.write", text))
                return;
            double id = 0;
            if (!u::pop_num(d, "big.write", id))
                return;

            State::File *f = file_at(d, id, "big.write");
            if (!f)
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            if (!f->writable)
            {
                u::err("big.write", "句柄不是以写模式打开的");
                u::push(d, keel::make_bool(false));
                return;
            }
            f->wbuf += text;
            if (f->wbuf.size() >= kWriteChunk && !f->flush_w())
            {
                u::err("big.write", "写文件失败");
                u::push(d, keel::make_bool(false));
                return;
            }
            u::push(d, keel::make_bool(true));
        });

        a.register_func("big.flush", KeelF {
            double id = 0;
            if (!u::pop_num(d, "big.flush", id))
                return;
            State::File *f = file_at(d, id, "big.flush");
            bool ok = f != nullptr;
            if (ok)
                ok = f->flush_w() && ioc::io_flush(f->h);
            if (!ok)
                u::err("big.flush", "刷新失败");
            u::push(d, keel::make_bool(ok));
        });

        // ---------------------------------------------------------- 位置 / 状态
        a.register_func("big.seek", KeelF {
            double pos = 0;
            if (!u::pop_num(d, "big.seek", pos))
                return;
            double id = 0;
            if (!u::pop_num(d, "big.seek", id))
                return;

            State::File *f = file_at(d, id, "big.seek");
            if (!f)
            {
                u::push(d, keel::make_nil());
                return;
            }
            if (!f->flush_w())
            {
                u::err("big.seek", "先把写缓冲刷出去时失败");
                u::push(d, keel::make_nil());
                return;
            }
            f->clear_read();
            f->eof = false;
            std::uint64_t np = 0;
            if (!ioc::io_seek(f->h, pos < 0 ? 0 : static_cast<std::uint64_t>(pos), np))
            {
                u::err("big.seek", "定位失败");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(static_cast<double>(np)));
        });

        a.register_func("big.tell", KeelF {
            double id = 0;
            if (!u::pop_num(d, "big.tell", id))
                return;
            State::File *f = file_at(d, id, "big.tell");
            if (!f || !f->flush_w())
            {
                u::push(d, keel::make_nil());
                return;
            }
            std::uint64_t pos = 0;
            if (!ioc::io_tell(f->h, pos))
            {
                u::err("big.tell", "取位置失败");
                u::push(d, keel::make_nil());
                return;
            }
            pos -= std::min<std::uint64_t>(pos, f->pending_read());
            u::push(d, keel::make_number(static_cast<double>(pos)));
        });

        a.register_func("big.size", KeelF {
            double id = 0;
            if (!u::pop_num(d, "big.size", id))
                return;
            State::File *f = file_at(d, id, "big.size");
            std::uint64_t size = 0;
            if (!f || !ioc::io_size(f->h, size))
            {
                u::err("big.size", "取大小失败");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(static_cast<double>(size)));
        });

        a.register_func("big.eof", KeelF {
            double id = 0;
            if (!u::pop_num(d, "big.eof", id))
                return;
            State::File *f = file_at(d, id, "big.eof");
            if (!f)
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            if (f->pending_read() > 0)
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            if (!f->eof)
            {
                if (!f->fill(kReadChunk))
                {
                    u::err("big.eof", "读文件失败");
                    u::push(d, keel::make_bool(false));
                    return;
                }
            }
            u::push(d, keel::make_bool(f->pending_read() == 0));
        });

        // ---------------------------------------------------------- 整文件
        a.register_func("big.slurp", KeelF {
            std::string path;
            if (!u::pop_text(d, "big.slurp", path))
                return;
            if (!u::check_sandbox(d, path, "big.slurp"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            std::string text;
            std::string err;
            if (!ioc::read_file_all(path, d.max_alloc, text, err))
            {
                u::err("big.slurp", "cannot read '" + path + "': " + err);
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_string(std::move(text)));
        });

        // 内容 路径 -- 布尔（和 writefile / f.spit 一致）
        a.register_func("big.spit", KeelF {
            if (!u::need(d, 2, "big.spit"))
                return;
            std::string path;
            if (!u::pop_text(d, "big.spit", path))
                return;
            std::string text;
            if (!u::pop_text(d, "big.spit", text))
                return;
            if (!u::check_sandbox(d, path, "big.spit"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::string err;
            const bool ok = ioc::write_file_atomic(path, text, err);
            if (!ok)
                u::err("big.spit", "cannot write '" + path + "': " + err);
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("big.append", KeelF {
            if (!u::need(d, 2, "big.append"))
                return;
            std::string path;
            if (!u::pop_text(d, "big.append", path))
                return;
            std::string text;
            if (!u::pop_text(d, "big.append", text))
                return;
            if (!u::check_sandbox(d, path, "big.append"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::string err;
            ioc::native_handle h = ioc::io_open(path, ioc::IoMode::Append, err);
            bool ok = ioc::valid(h);
            if (ok)
                ok = ioc::io_write(h, text.data(), text.size());
            if (!ok)
                u::err("big.append", "cannot append '" + path + "': " + err);
            if (ioc::valid(h))
                ioc::io_close(h);
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("big.copy", KeelF {
            if (!u::need(d, 2, "big.copy"))
                return;
            std::string to;
            if (!u::pop_text(d, "big.copy", to))
                return;
            std::string from;
            if (!u::pop_text(d, "big.copy", from))
                return;
            if (!u::check_sandbox(d, from, "big.copy") || !u::check_sandbox(d, to, "big.copy"))
            {
                u::push(d, keel::make_nil());
                return;
            }

            std::string err;
            ioc::native_handle src = ioc::io_open(from, ioc::IoMode::Read, err);
            if (!ioc::valid(src))
            {
                u::err("big.copy", "cannot open '" + from + "': " + err);
                u::push(d, keel::make_nil());
                return;
            }
            ioc::native_handle dst = ioc::io_open(to, ioc::IoMode::Write, err);
            if (!ioc::valid(dst))
            {
                u::err("big.copy", "cannot write '" + to + "': " + err);
                ioc::io_close(src);
                u::push(d, keel::make_nil());
                return;
            }

            std::vector<char> buf(kWriteChunk);
            std::uint64_t total = 0;
            bool ok = true;
            for (;;)
            {
                std::size_t got = 0;
                if (!ioc::io_read(src, buf.data(), buf.size(), got))
                {
                    err = "读失败";
                    ok = false;
                    break;
                }
                if (got == 0)
                    break;
                if (!ioc::io_write(dst, buf.data(), got))
                {
                    err = "写失败";
                    ok = false;
                    break;
                }
                total += got;
            }
            ioc::io_close(dst);
            ioc::io_close(src);

            if (!ok)
            {
                u::err("big.copy", err);
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(static_cast<double>(total)));
        });

        // ---------------------------------------------------------- 文件系统
        a.register_func("big.stat", KeelF {
            std::string path;
            if (!u::pop_text(d, "big.stat", path))
                return;
            if (!u::check_sandbox(d, path, "big.stat"))
            {
                u::push(d, keel::make_map());
                return;
            }
            const ioc::FileSig sig = ioc::file_sig(ioc::from_utf8(path));
            keel::Value m = keel::make_map();
            m.set("exists", keel::make_bool(sig.exists));
            m.set("size", keel::make_number(static_cast<double>(sig.size)));
            m.set("is_dir", keel::make_bool(sig.is_dir));
            m.set("mtime", keel::make_number(ioc::file_mtime_seconds(path)));
            u::push(d, std::move(m));
        });

        a.register_func("big.list", KeelF {
            std::string path;
            if (!u::pop_text(d, "big.list", path))
                return;
            if (!u::check_sandbox(d, path, "big.list"))
            {
                u::push(d, keel::make_nil());
                return;
            }

            std::error_code ec;
            fs::directory_iterator it(ioc::from_utf8(path), ec), end;
            if (ec)
            {
                u::err("big.list", "cannot list '" + path + "': " + ec.message());
                u::push(d, keel::make_nil());
                return;
            }

            std::vector<std::string> names;
            for (; it != end; it.increment(ec))
            {
                if (ec)
                {
                    u::err("big.list", "列出时出错: " + ec.message());
                    u::push(d, keel::make_nil());
                    return;
                }
                names.push_back(ioc::to_utf8(it->path().filename()));
            }
            std::sort(names.begin(), names.end());

            keel::Value list = keel::make_list();
            for (auto &n : names)
                list.push(keel::make_string(std::move(n)));
            u::push(d, std::move(list));
        });

        a.register_func("big.mkdir", KeelF {
            std::string path;
            if (!u::pop_text(d, "big.mkdir", path))
                return;
            if (!u::check_sandbox(d, path, "big.mkdir"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::error_code ec;
            fs::create_directories(ioc::from_utf8(path), ec);
            if (ec)
                u::err("big.mkdir", "cannot mkdir '" + path + "': " + ec.message());
            u::push(d, keel::make_bool(!ec));
        });

        a.register_func("big.remove", KeelF {
            std::string path;
            if (!u::pop_text(d, "big.remove", path))
                return;
            if (!u::check_sandbox(d, path, "big.remove"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::error_code ec;
            const bool ok = fs::remove(ioc::from_utf8(path), ec);
            if (ec)
                u::err("big.remove", "cannot remove '" + path + "': " + ec.message());
            u::push(d, keel::make_bool(ok && !ec));
        });

        a.register_func("big.rename", KeelF {
            if (!u::need(d, 2, "big.rename"))
                return;
            std::string to;
            if (!u::pop_text(d, "big.rename", to))
                return;
            std::string from;
            if (!u::pop_text(d, "big.rename", from))
                return;
            if (!u::check_sandbox(d, from, "big.rename") || !u::check_sandbox(d, to, "big.rename"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::error_code ec;
            fs::rename(ioc::from_utf8(from), ioc::from_utf8(to), ec);
            if (ec)
                u::err("big.rename", "cannot rename '" + from + "' -> '" + to + "': " + ec.message());
            u::push(d, keel::make_bool(!ec));
        });

        // ---------------------------------------------------------- 内存流
        a.register_func("stream.new", KeelF {
            State &st = States::get(d);
            const int id = st.next_stream++;
            st.streams[id] = State::Stream{};
            u::push(d, keel::make_number(id));
        });

        a.register_func("stream.write", KeelF {
            if (!u::need(d, 2, "stream.write"))
                return;
            std::string text;
            if (!u::pop_text(d, "stream.write", text))
                return;
            double id = 0;
            if (!u::pop_num(d, "stream.write", id))
                return;
            State::Stream *s = stream_at(d, id, "stream.write");
            if (!s)
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            if (s->pos > s->data.size())
                s->data.resize(s->pos, '\0');
            if (s->pos + text.size() > s->data.size())
                s->data.resize(s->pos + text.size());
            std::memcpy(s->data.data() + s->pos, text.data(), text.size());
            s->pos += text.size();
            u::push(d, keel::make_bool(true));
        });

        a.register_func("stream.read", KeelF {
            double n = 0;
            if (!u::pop_num(d, "stream.read", n))
                return;
            double id = 0;
            if (!u::pop_num(d, "stream.read", id))
                return;
            State::Stream *s = stream_at(d, id, "stream.read");
            if (!s)
            {
                u::push(d, keel::make_nil());
                return;
            }
            const std::size_t rest = s->data.size() - std::min(s->data.size(), s->pos);
            std::size_t want = n <= 0 ? rest : std::min<std::size_t>(rest, static_cast<std::size_t>(n));
            std::string out = s->data.substr(s->pos, want);
            s->pos += out.size();
            u::push(d, keel::make_string(std::move(out)));
        });

        a.register_func("stream.seek", KeelF {
            double pos = 0;
            if (!u::pop_num(d, "stream.seek", pos))
                return;
            double id = 0;
            if (!u::pop_num(d, "stream.seek", id))
                return;
            State::Stream *s = stream_at(d, id, "stream.seek");
            if (!s)
            {
                u::push(d, keel::make_nil());
                return;
            }
            s->pos = static_cast<std::size_t>(pos < 0 ? 0 : pos);
            u::push(d, keel::make_number(static_cast<double>(s->pos)));
        });

        a.register_func("stream.size", KeelF {
            double id = 0;
            if (!u::pop_num(d, "stream.size", id))
                return;
            State::Stream *s = stream_at(d, id, "stream.size");
            u::push(d, keel::make_number(s ? static_cast<double>(s->data.size()) : 0.0));
        });

        a.register_func("stream.save", KeelF {
            if (!u::need(d, 2, "stream.save"))
                return;
            std::string path;
            if (!u::pop_text(d, "stream.save", path))
                return;
            double id = 0;
            if (!u::pop_num(d, "stream.save", id))
                return;
            if (!u::check_sandbox(d, path, "stream.save"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            State::Stream *s = stream_at(d, id, "stream.save");
            if (!s)
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::string err;
            const bool ok = ioc::write_file_atomic(path, s->data, err);
            if (!ok)
                u::err("stream.save", "cannot write '" + path + "': " + err);
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("stream.free", KeelF {
            double id = 0;
            if (!u::pop_num(d, "stream.free", id))
                return;
            State &st = States::get(d);
            const bool ok = st.streams.erase(static_cast<int>(id)) != 0;
            u::push(d, keel::make_bool(ok));
        });
    }
} // namespace keelbig
