#pragma once
// ============================================================================
//  KeelDynSource.hpp —— 动态源（运行时把脚本文件装进内存，并跟着磁盘变）
// ----------------------------------------------------------------------------
//  它和 BigIO 的分界：
//    * BigIO 面向"文件内容"：任意二进制/文本、句柄、大文件、流。
//    * DynSource 面向"源"：按路径记住这份脚本的文本 + 版本，**每次取都先看磁盘**
//      变没变 —— 变了就重读。典型用法：
//
//          "1.keel" src.load          // 第一次：读盘，缓存
//          ... 1.keel 把自己改写了 ...
//          "1.keel" src.load          // 第二次：发现大小/时间变了，重读出新的内容
//
//  指令表：
//    src.load    路径 -- 字符串|nil   取源文本；磁盘变了就重读（不变就直接给缓存）
//    src.reload  路径 -- 字符串|nil   强制重读，不看缓存
//    src.text    路径 -- 字符串|nil   只取已缓存的那份，不碰磁盘
//    src.loaded  路径 -- 布尔         这个路径当前有没有缓存
//    src.changed 路径 -- 布尔         缓存之后磁盘上的文件变了（或没了）
//    src.version 路径 -- 数字         缓存文本的版本号（FNV-1a 取低 53 位，能无损放进 double）
//    src.tokens  路径 -- list|nil     取源文本并切词（空白分隔，引号保护空格）
//    src.meta    路径 -- map {path,exists,is_dir,size,mtime,version,loads,missing}
//    src.list     -- list             已缓存的全部路径
//    src.drop    路径 -- 布尔         丢掉缓存
//    src.limit   字节数 -- 旧上限     单次加载上限（默认 8MB，夹在 1KB..64MB）
//
//  刷新规则：比较 (大小, 修改时间)。两个值都没变就复用缓存；改了才重读。
//  同一时刻、同一长度、同一 tick 内的改写可能漏掉 —— 用 src.reload 强制。
//  路径会先规范化（绝对路径 + 词法归一），"a/../b.keel" 和 "b.keel" 是同一个缓存项。
// ============================================================================

#include "Keel.hpp"
#include "KeelIOCore.hpp"
#include "KeelInstanceState.hpp"
#include "KeelUtil.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keelsrc
{
    namespace u = keel::util;
    namespace ioc = keelio_impl;
    namespace fs = std::filesystem;

    struct Entry
    {
        std::string text;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        std::uint64_t version = 0;
        int loads = 0;
        bool missing = false;
    };

    struct State
    {
        std::unordered_map<std::string, Entry> cache;
        std::size_t limit = 8u << 20; // 单次加载上限
    };

    using States = keel::StateTable<State>;

    namespace detail
    {
        /// 规范化路径，作为缓存键（也是实际读盘用的路径）
        inline std::string key_of(const std::string &path)
        {
            std::error_code ec;
            const fs::path abs = fs::absolute(ioc::from_utf8(path), ec);
            if (ec)
                return path;
            return ioc::to_utf8(abs.lexically_normal());
        }

        /// 取一份"新鲜的"源文本：签名没变就复用缓存，变了才读盘。
        inline bool load_fresh(State &st, const std::string &key, std::string &out, std::string &err)
        {
            const ioc::FileSig sig = ioc::file_sig(ioc::from_utf8(key));
            if (!sig.exists)
            {
                Entry &e = st.cache[key];
                e.missing = true;
                err = "找不到文件";
                return false;
            }
            if (sig.is_dir)
            {
                Entry &e = st.cache[key];
                e.missing = true;
                err = "这是一个目录，不是文件";
                return false;
            }

            Entry &e = st.cache[key];
            if (!e.missing && e.size == sig.size && e.mtime == sig.mtime)
            {
                out = e.text; // 命中缓存
                return true;
            }

            if (sig.size > st.limit)
            {
                err = "文件太大（" + std::to_string(sig.size) + " 字节 > 上限 " +
                      std::to_string(st.limit) + "）";
                return false;
            }

            std::string text;
            if (!ioc::read_file_all(key, st.limit, text, err))
                return false;

            e.text = text;
            e.size = sig.size;
            e.mtime = sig.mtime;
            // 低 53 位：double 能精确表示，脚本里比较版本号不会因舍入误判
            e.version = ioc::fnv1a64(text) & ((1ull << 53) - 1);
            ++e.loads;
            e.missing = false;
            out = std::move(text);
            return true;
        }
    } // namespace detail

    inline void src_register(keel::Keel &a)
    {
        using namespace detail;

        a.register_func("src.load", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.load", path))
                return;
            if (!u::check_sandbox(d, path, "src.load"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            State &st = States::get(d);
            const std::string key = key_of(path);
            std::string text;
            std::string err;
            if (!load_fresh(st, key, text, err))
            {
                u::err("src.load", err + ": " + key);
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_string(std::move(text)));
        });

        a.register_func("src.reload", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.reload", path))
                return;
            if (!u::check_sandbox(d, path, "src.reload"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            State &st = States::get(d);
            const std::string key = key_of(path);

            // 强制重读：把签名弄失效再走同一条路
            auto it = st.cache.find(key);
            if (it != st.cache.end())
                it->second.mtime = -1;

            std::string text;
            std::string err;
            if (!load_fresh(st, key, text, err))
            {
                u::err("src.reload", err + ": " + key);
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_string(std::move(text)));
        });

        a.register_func("src.text", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.text", path))
                return;
            State &st = States::get(d);
            auto it = st.cache.find(key_of(path));
            if (it == st.cache.end() || it->second.missing)
            {
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_string(it->second.text));
        });

        a.register_func("src.loaded", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.loaded", path))
                return;
            State &st = States::get(d);
            auto it = st.cache.find(key_of(path));
            u::push(d, keel::make_bool(it != st.cache.end() && !it->second.missing));
        });

        a.register_func("src.changed", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.changed", path))
                return;
            if (!u::check_sandbox(d, path, "src.changed"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            State &st = States::get(d);
            const std::string key = key_of(path);
            auto it = st.cache.find(key);
            if (it == st.cache.end() || it->second.missing)
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            const ioc::FileSig sig = ioc::file_sig(ioc::from_utf8(key));
            const bool changed = !sig.exists || sig.is_dir || sig.size != it->second.size ||
                                 sig.mtime != it->second.mtime;
            u::push(d, keel::make_bool(changed));
        });

        a.register_func("src.version", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.version", path))
                return;
            State &st = States::get(d);
            auto it = st.cache.find(key_of(path));
            const double v = (it == st.cache.end() || it->second.missing)
                                 ? 0.0
                                 : static_cast<double>(it->second.version);
            u::push(d, keel::make_number(v));
        });

        a.register_func("src.tokens", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.tokens", path))
                return;
            if (!u::check_sandbox(d, path, "src.tokens"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            State &st = States::get(d);
            const std::string key = key_of(path);
            std::string text;
            std::string err;
            if (!load_fresh(st, key, text, err))
            {
                u::err("src.tokens", err + ": " + key);
                u::push(d, keel::make_nil());
                return;
            }
            const keel::strings toks = keel::fast_split_tokens(text);
            keel::Value list = keel::make_list();
            for (const auto &t : toks)
                list.push(keel::make_string(t));
            u::push(d, std::move(list));
        });

        a.register_func("src.meta", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.meta", path))
                return;
            if (!u::check_sandbox(d, path, "src.meta"))
            {
                u::push(d, keel::make_map());
                return;
            }
            State &st = States::get(d);
            const std::string key = key_of(path);
            const ioc::FileSig sig = ioc::file_sig(ioc::from_utf8(key));

            keel::Value m = keel::make_map();
            m.set("path", keel::make_string(key));
            m.set("exists", keel::make_bool(sig.exists));
            m.set("is_dir", keel::make_bool(sig.is_dir));
            m.set("size", keel::make_number(static_cast<double>(sig.size)));
            m.set("mtime", keel::make_number(ioc::file_mtime_seconds(key)));

            auto it = st.cache.find(key);
            const bool cached = it != st.cache.end() && !it->second.missing;
            m.set("version", keel::make_number(cached ? static_cast<double>(it->second.version) : 0.0));
            m.set("loads", keel::make_number(cached ? it->second.loads : 0));
            m.set("missing", keel::make_bool(!sig.exists));
            u::push(d, std::move(m));
        });

        a.register_func("src.list", KeelF {
            State &st = States::get(d);
            std::vector<std::string> keys;
            keys.reserve(st.cache.size());
            for (const auto &kv : st.cache)
                if (!kv.second.missing)
                    keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());

            keel::Value list = keel::make_list();
            for (auto &k : keys)
                list.push(keel::make_string(std::move(k)));
            u::push(d, std::move(list));
        });

        a.register_func("src.drop", KeelF {
            std::string path;
            if (!u::pop_text(d, "src.drop", path))
                return;
            State &st = States::get(d);
            const bool ok = st.cache.erase(key_of(path)) != 0;
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("src.limit", KeelF {
            double n = 0;
            if (!u::pop_num(d, "src.limit", n))
                return;
            State &st = States::get(d);
            const std::size_t old = st.limit;
            constexpr std::size_t kMin = 1u << 10;
            constexpr std::size_t kMax = 64u << 20;
            std::size_t v = n <= 0 ? kMin : static_cast<std::size_t>(n);
            v = std::max(kMin, std::min(v, kMax));
            st.limit = v;
            u::push(d, keel::make_number(static_cast<double>(old)));
        });
    }
} // namespace keelsrc
