#pragma once
// ============================================================================
// KeelIO.hpp -- 简单的输入输出（std::cout / std::cin / fstream）
// 因为要额外的 <iostream> <fstream>，所以单独一个文件。
//
// 指令表：
//   print     值 --           打印，不换行（字符串原样，其它按 toString）
//   println   值 --           打印 + 换行
//   nl        --              打印一个换行
//   eprint    值 --           打到 stderr，不换行
//   eprintln  值 --           打到 stderr + 换行
//   flush     --              刷新 cout / cerr
//   readline  -- 字符串       读一行（去掉 \r\n）；EOF 给 nil
//   readall   -- 字符串       读掉剩余的整个 stdin
//   readfile  路径 -- 字符串  读文本文件；失败给 nil 并报错
//   writefile 内容 路径 -- 布尔
//   appendfile 内容 路径 -- 布尔
//   exists    路径 -- 布尔    文件/目录是否存在
//   dump      --              把当前栈内容打到 stdout（不弹栈，调试用）
// 别名（puts/echo/input/readln 等）见 keel/docs/REFERENCE.md。
// ============================================================================
#include "Keel.hpp"
#include "KeelIOCore.hpp"
#include "KeelUtil.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace keelio
{
    namespace u = keel::util;

    namespace detail
    {
        inline void alias(keel::Keel &a, const char *name, const char *of)
        {
            auto it = a.data.funcs.find(of);
            if (it != a.data.funcs.end())
                a.data.funcs.insert_or_assign(name, it->second);
        }

        inline bool write_file(const std::string &path, const std::string &content, bool append)
        {
            // 路径按 UTF-8 解释（统一走 KeelIOCore，避免 u8path 在 C++20 废弃）
            std::ofstream f(keelio_impl::from_utf8(path),
                            std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!f.is_open())
                return false;
            f.write(content.data(), static_cast<std::streamsize>(content.size()));
            f.flush(); // 不 flush 的话，写到一半失败要等析构才暴露
            return f.good();
        }
    }

    inline void io_register(keel::Keel &a)
    {
        using namespace detail;

        a.register_func("print", KeelF {
            if (!u::need(d, 1, "print"))
                return;
            std::cout << u::pop(d).toString() << std::flush;
        });

        a.register_func("println", KeelF {
            if (!u::need(d, 1, "println"))
                return;
            std::cout << u::pop(d).toString() << '\n' << std::flush;
        });

        a.register_func("nl", KeelF {
            (void)d;
            std::cout << '\n' << std::flush;
        });

        a.register_func("eprint", KeelF {
            if (!u::need(d, 1, "eprint"))
                return;
            std::cerr << u::pop(d).toString() << std::flush;
        });

        a.register_func("eprintln", KeelF {
            if (!u::need(d, 1, "eprintln"))
                return;
            std::cerr << u::pop(d).toString() << '\n' << std::flush;
        });

        a.register_func("flush", KeelF {
            (void)d;
            std::cout.flush();
            std::cerr.flush();
        });

        a.register_func("readline", KeelF {
            std::string line;
            if (!std::getline(std::cin, line))
            {
                u::push(d, keel::make_nil()); // EOF
                return;
            }
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            u::push(d, keel::make_string(std::move(line)));
        });

        a.register_func("readall", KeelF {
            // 整块读，保留尾随换行（getline 循环会丢掉最后一个 '\n'）
            std::string out((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
            u::push(d, keel::make_string(std::move(out)));
        });

        a.register_func("readfile", KeelF {
            std::string path;
            if (!u::pop_text(d, "readfile", path))
                return;
            if (!u::check_sandbox(d, path, "readfile"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            std::ifstream f(keelio_impl::from_utf8(path), std::ios::binary);
            if (!f.is_open())
            {
                u::err("readfile", "cannot open '" + path + "'");
                u::push(d, keel::make_nil());
                return;
            }
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (f.bad())
            {
                u::err("readfile", "read error on '" + path + "'");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_string(std::move(content)));
        });

        a.register_func("writefile", KeelF {
            if (!u::need(d, 2, "writefile"))
                return;
            std::string path;
            if (!u::pop_text(d, "writefile", path))
                return;
            std::string content;
            if (!u::pop_text(d, "writefile", content))
                return;
            if (!u::check_sandbox(d, path, "writefile"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            const bool ok = write_file(path, content, false);
            if (!ok)
                u::err("writefile", "cannot write '" + path + "'");
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("appendfile", KeelF {
            if (!u::need(d, 2, "appendfile"))
                return;
            std::string path;
            if (!u::pop_text(d, "appendfile", path))
                return;
            std::string content;
            if (!u::pop_text(d, "appendfile", content))
                return;
            if (!u::check_sandbox(d, path, "appendfile"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            const bool ok = write_file(path, content, true);
            if (!ok)
                u::err("appendfile", "cannot append to '" + path + "'");
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("exists", KeelF {
            std::string path;
            if (!u::pop_text(d, "exists", path))
                return;
            if (!u::check_sandbox(d, path, "exists"))
            {
                u::push(d, keel::make_bool(false));
                return;
            }
            // 用 filesystem 而不是 ifstream：ifstream 打不开目录，
            // 会把确实存在的目录报成 false（指令表里写的是"文件/目录"）
            std::error_code ec;
            u::push(d, keel::make_bool(std::filesystem::exists(keelio_impl::from_utf8(path), ec)));
        });

        a.register_func("dump", KeelF {
            // 不动栈，只把内容打出来，方便 agent 自查
            std::cout << "stack(" << d.stack.size() << "):";
            for (size_t i = 0; i < d.stack.size(); ++i)
            {
                std::cout << "\n  [" << i << "] " << keel::value_type(d.stack[i].type) << " = "
                          << d.stack[i].toString();
            }
            std::cout << std::endl;
        });

        // ------------------------------------------------------------ 别名
        alias(a, "puts", "println");
        alias(a, "input", "readline");
        alias(a, "readln", "readline");
        alias(a, "echo", "println");
    }
}
