#pragma once
// ============================================================================
//  KeelFastIO.hpp —— 快速 IO（底层输出 + 整读整写，不走 iostream/stdio）
// ----------------------------------------------------------------------------
//  指令表（前缀 f.，不会和现有 print/println/readfile 撞名）：
//
//    f.out    值 --            写 stdout，不换行
//    f.outln  值 --            写 stdout + '\n'
//    f.err    值 --            写 stderr
//    f.errln  值 --            写 stderr + '\n'
//    f.raw    字符串 --        原样写字节（不做任何格式化）
//    f.flush  --               把 stdout / stderr 缓冲全刷出去
//    f.spit   内容 路径 -- 布尔 一次性整写（覆盖）
//    f.slurp  路径 -- 字符串|nil 一次性整读（上限 = d.max_alloc）
//
//  为什么快：
//    * 数字用 std::to_chars（精度 6，和 iostream 默认一致），不碰 locale / 虚函数；
//    * stdout / stderr 有 32KB 缓冲，多行小输出不会一行一次系统调用；
//    * Windows 下走 WriteFile；控制台句柄自动转 UTF-16 用 WriteConsoleW，
//      所以中文不会乱码，重定向到管道/文件时又是纯 UTF-8 字节。
//
//  宿主接口（调试器 / agent 用）：
//      keelfast::set_writer(a.data, [&](const char* p, std::size_t n) { ... });
//      keelfast::flush_all(a.data);   // run 结束 / 暂停时调一次
//  设了 writer 之后 stdout 的字节交给它（stderr 仍走系统句柄）。
// ============================================================================

#include "Keel.hpp"
#include "KeelIOCore.hpp"
#include "KeelInstanceState.hpp"
#include "KeelUtil.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace keelfast
{
    namespace u = keel::util;
    namespace ioc = keelio_impl;

    struct State
    {
        static constexpr std::size_t kFlushAt = 32 * 1024;

        std::string out;
        std::string err;
        std::function<void(const char *, std::size_t)> writer;
        bool writer_set = false;

        void flush_out()
        {
            if (out.empty())
                return;
            if (writer_set && writer)
                writer(out.data(), out.size());
            else
                ioc::write_stdout_bytes(out.data(), out.size());
            out.clear();
        }

        void flush_err()
        {
            if (err.empty())
                return;
            ioc::write_stderr_bytes(err.data(), err.size());
            err.clear();
        }

        void flush_all()
        {
            flush_out();
            flush_err();
        }
    };

    using States = keel::StateTable<State>;

    // ------------------------------------------------------------------ 宿主接口
    /// 接管 stdout：之后 f.out / f.outln 的字节交给 w（调试器把它接进「输出」窗口）
    inline void set_writer(keel::KeelData &d, std::function<void(const char *, std::size_t)> w)
    {
        State &s = States::get(d);
        s.writer = std::move(w);
        s.writer_set = true;
    }

    inline void clear_writer(keel::KeelData &d)
    {
        if (State *s = States::find(d))
        {
            s->writer = nullptr;
            s->writer_set = false;
        }
    }

    /// 把两个缓冲都刷出去。宿主在 run 结束、单步暂停、帧末调用都行（没数据时几乎零开销）。
    inline void flush_all(keel::KeelData &d)
    {
        if (State *s = States::find(d))
            s->flush_all();
    }

    /// 还没刷出去的字节数（测试/诊断用）
    inline std::size_t pending(const keel::KeelData &d)
    {
        const State *s = States::find(d);
        return s ? s->out.size() + s->err.size() : 0;
    }

    // ------------------------------------------------------------------ 内部
    namespace detail
    {
        inline void append_value(std::string &out, const keel::Value &v)
        {
            switch (v.type)
            {
            case keel::Type::Nil:
                out += "nil";
                break;
            case keel::Type::Bool:
                out += v.b ? "true" : "false";
                break;
            case keel::Type::Number:
                ioc::append_number(out, v.n);
                break;
            case keel::Type::String:
                out.append(v.s);
                break;
            default:
                out += v.toString(); // 容器只能走慢路径（toString 内部还是 ostringstream）
                break;
            }
        }

        inline bool write_whole_file(const std::string &path, const std::string &text, std::string &err)
        {
            return ioc::write_file_all(path, text, err);
        }

        inline bool read_whole_file(const std::string &path, std::size_t limit, std::string &out,
                                    std::string &err)
        {
            return ioc::read_file_all(path, limit, out, err);
        }
    } // namespace detail

    // ------------------------------------------------------------------ 指令
    inline void fast_register(keel::Keel &a)
    {
        using namespace detail;

        a.register_func("f.out", KeelF {
            if (!u::need(d, 1, "f.out"))
                return;
            State &s = States::get(d);
            append_value(s.out, u::pop(d));
            if (s.out.size() >= State::kFlushAt)
                s.flush_out();
        });

        a.register_func("f.outln", KeelF {
            if (!u::need(d, 1, "f.outln"))
                return;
            State &s = States::get(d);
            append_value(s.out, u::pop(d));
            s.out.push_back('\n');
            if (s.out.size() >= State::kFlushAt)
                s.flush_out();
        });

        a.register_func("f.err", KeelF {
            if (!u::need(d, 1, "f.err"))
                return;
            State &s = States::get(d);
            append_value(s.err, u::pop(d));
            if (s.err.size() >= State::kFlushAt)
                s.flush_err();
        });

        a.register_func("f.errln", KeelF {
            if (!u::need(d, 1, "f.errln"))
                return;
            State &s = States::get(d);
            append_value(s.err, u::pop(d));
            s.err.push_back('\n');
            if (s.err.size() >= State::kFlushAt)
                s.flush_err();
        });

        a.register_func("f.raw", KeelF {
            if (!u::need(d, 1, "f.raw"))
                return;
            State &s = States::get(d);
            std::string text;
            if (!u::pop_text(d, "f.raw", text))
                return;
            s.out.append(text);
            if (s.out.size() >= State::kFlushAt)
                s.flush_out();
        });

        a.register_func("f.flush", KeelF {
            (void)d;
            if (State *s = States::find(d))
                s->flush_all();
        });

        // 内容 路径 -- 布尔（和 writefile 的参数顺序一致）
        a.register_func("f.spit", KeelF {
            if (!u::need(d, 2, "f.spit"))
                return;
            std::string path;
            if (!u::pop_text(d, "f.spit", path))
                return;
            std::string text;
            if (!u::pop_text(d, "f.spit", text))
                return;
            if (!u::check_sandbox(d, path, "f.spit"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            std::string err;
            const bool ok = write_whole_file(path, text, err);
            if (!ok)
                u::err("f.spit", "cannot write '" + path + "': " + err);
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("f.slurp", KeelF {
            std::string path;
            if (!u::pop_text(d, "f.slurp", path))
                return;
            if (!u::check_sandbox(d, path, "f.slurp"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            std::string text;
            std::string err;
            if (!read_whole_file(path, d.max_alloc, text, err))
            {
                u::err("f.slurp", "cannot read '" + path + "': " + err);
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_string(std::move(text)));
        });
    }
} // namespace keelfast
