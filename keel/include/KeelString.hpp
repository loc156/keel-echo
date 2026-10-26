#pragma once
// ============================================================================
// KeelString.hpp -- 字符串相关内置函数
// 依赖：<string> <algorithm> <cctype>；KeelBase.hpp 不是必需的（除 len 外
// 本文件自给自足），但实际用起来一般都一起注册。
//
// 参数顺序都是 Forth 风格：后写的在栈顶。
// 例如："hello" 1 3 substr  ->  "ell"
//       "a,b" "," split     ->  ["a", "b"]
//       list "," join       ->  "a,b"
//
// 指令表：
//   trim ltrim rtrim          字符串 -- 字符串（首尾去空白）
//   upper lower               字符串 -- 字符串（仅 ASCII）
//   substr                    字符串 起 长 -- 子串（起为负从尾部算，长为负表示到末尾）
//   charat                    字符串 下标 -- 单字符（越界给 "" 并报错）
//   chars                     字符串 -- 单字符列表
//   find rfind                字符串 子串 -- 下标（找不到 -1）
//   contains                  字符串 子串 -- 布尔
//   startswith endswith       字符串 前后缀 -- 布尔
//   count                     字符串 子串 -- 出现次数
//   replace                   字符串 旧 新 -- 新串（全部替换）
//   split                     字符串 分隔符 -- 列表（分隔符给空串则按空白切分）
//   join                      列表 分隔符 -- 字符串
//   repeat                    字符串 次数 -- 字符串
//   reverse                   字符串 -- 字符串
// 别名（strip/starts/ends/slice/indexof/lowercase/uppercase 等）见
// keel/docs/REFERENCE.md。
// ============================================================================
#include "Keel.hpp"
#include "KeelUtil.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace keelstr
{
    namespace u = keel::util;

    namespace detail
    {
        inline bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        inline std::string ltrim_copy(const std::string &s)
        {
            size_t i = 0;
            while (i < s.size() && is_space(s[i]))
                ++i;
            return s.substr(i);
        }

        inline std::string rtrim_copy(const std::string &s)
        {
            size_t n = s.size();
            while (n > 0 && is_space(s[n - 1]))
                --n;
            return s.substr(0, n);
        }

        // 下标转 size_t：负数从尾部算，越界返回 false
        inline bool index_of(long long raw, size_t n, size_t &out)
        {
            if (raw < 0)
                raw += static_cast<long long>(n);
            if (raw < 0 || raw >= static_cast<long long>(n))
                return false;
            out = static_cast<size_t>(raw);
            return true;
        }

        inline void alias(keel::Keel &a, const char *name, const char *of)
        {
            auto it = a.data.funcs.find(of);
            if (it != a.data.funcs.end())
                a.data.funcs.insert_or_assign(name, it->second);
        }
    }

    inline void str_register(keel::Keel &a)
    {
        using namespace detail;

        a.register_func("trim", KeelF {
            std::string s;
            if (!u::pop_text(d, "trim", s))
                return;
            u::push(d, keel::make_string(rtrim_copy(ltrim_copy(s)))); });

        a.register_func("ltrim", KeelF {
            std::string s;
            if (!u::pop_text(d, "ltrim", s))
                return;
            u::push(d, keel::make_string(ltrim_copy(s))); });

        a.register_func("rtrim", KeelF {
            std::string s;
            if (!u::pop_text(d, "rtrim", s))
                return;
            u::push(d, keel::make_string(rtrim_copy(s))); });

        a.register_func("upper", KeelF {
            std::string s;
            if (!u::pop_text(d, "upper", s))
                return;
            for (char &c : s)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            u::push(d, keel::make_string(std::move(s))); });

        a.register_func("lower", KeelF {
            std::string s;
            if (!u::pop_text(d, "lower", s))
                return;
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            u::push(d, keel::make_string(std::move(s))); });

        a.register_func("substr", KeelF {
            // 字符串 起 长 -- 子串；出错时也消费 3 个、压一个 nil
            if (!u::need(d, 3, "substr"))
                return;
            double len = 0, start = 0;
            if (!u::peek_num(d, 0, "substr", len) || !u::peek_num(d, 1, "substr", start))
            {
                u::drop_n(d, 3);
                u::push(d, keel::make_nil());
                return;
            }
            const std::string s = u::peek(d, 2).toString();
            u::drop_n(d, 3);

            const long long n = static_cast<long long>(s.size());
            long long st = static_cast<long long>(u::trunc_d(start));
            if (st < 0)
                st += n;
            if (st < 0)
                st = 0;
            if (st > n)
                st = n;

            long long ln = static_cast<long long>(u::trunc_d(len));
            if (ln < 0)
                ln = n - st; // 长度为负 = 取到末尾
            if (ln < 0)
                ln = 0;
            if (st + ln > n)
                ln = n - st;

            u::push(d, keel::make_string(s.substr(static_cast<size_t>(st), static_cast<size_t>(ln)))); });

        a.register_func("charat", KeelF {
            if (!u::need(d, 2, "charat"))
                return;
            double idx = 0;
            if (!u::peek_num(d, 0, "charat", idx))
            {
                u::drop_n(d, 2);
                u::push(d, keel::make_nil());
                return;
            }
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            size_t at = 0;
            if (!index_of(static_cast<long long>(u::trunc_d(idx)), s.size(), at))
            {
                u::err("charat", "index " + std::to_string(static_cast<long long>(idx)) +
                                     " out of range (size " + std::to_string(s.size()) + ")");
                u::push(d, keel::make_string(std::string()));
                return;
            }
            u::push(d, keel::make_string(std::string(1, s[at]))); });

        a.register_func("chars", KeelF {
            std::string s;
            if (!u::pop_text(d, "chars", s))
                return;
            if (!u::check_alloc(d, s.size() * 128, "chars"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            keel::Value list = keel::make_list();
            for (char c : s)
                list.push(keel::make_string(std::string(1, c)));
            u::push(d, std::move(list)); });

        a.register_func("find", KeelF {
            if (!u::need(d, 2, "find"))
                return;
            const std::string sub = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            const size_t p = s.find(sub);
            u::push(d, keel::make_number(p == std::string::npos ? -1.0 : static_cast<double>(p))); });

        a.register_func("rfind", KeelF {
            if (!u::need(d, 2, "rfind"))
                return;
            const std::string sub = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            const size_t p = s.rfind(sub);
            u::push(d, keel::make_number(p == std::string::npos ? -1.0 : static_cast<double>(p))); });

        a.register_func("contains", KeelF {
            if (!u::need(d, 2, "contains"))
                return;
            const std::string sub = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            u::push(d, keel::make_bool(s.find(sub) != std::string::npos)); });

        a.register_func("startswith", KeelF {
            if (!u::need(d, 2, "startswith"))
                return;
            const std::string pre = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            u::push(d, keel::make_bool(s.size() >= pre.size() &&
                                      s.compare(0, pre.size(), pre) == 0)); });

        a.register_func("endswith", KeelF {
            if (!u::need(d, 2, "endswith"))
                return;
            const std::string suf = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            u::push(d, keel::make_bool(s.size() >= suf.size() &&
                                      s.compare(s.size() - suf.size(), suf.size(), suf) == 0)); });

        a.register_func("count", KeelF {
            if (!u::need(d, 2, "count"))
                return;
            const std::string sub = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            if (sub.empty())
            {
                u::push(d, keel::make_number(0));
                return;
            }
            double n = 0;
            size_t p = 0;
            while ((p = s.find(sub, p)) != std::string::npos)
            {
                ++n;
                p += sub.size();
            }
            u::push(d, keel::make_number(n)); });

        a.register_func("replace", KeelF {
            if (!u::need(d, 3, "replace"))
                return;
            const std::string repl = u::peek(d, 0).toString();
            const std::string from = u::peek(d, 1).toString();
            const std::string s = u::peek(d, 2).toString();
            u::drop_n(d, 3);
            if (from.empty())
            {
                u::push(d, keel::make_string(s));
                return;
            }
            std::string out;
            const size_t max_out = s.size() * (1 + repl.size());
            if (!u::check_alloc(d, max_out, "replace"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            out.reserve(s.size());
            size_t p = 0;
            while (true)
            {
                const size_t q = s.find(from, p);
                if (q == std::string::npos)
                {
                    out.append(s, p, std::string::npos);
                    break;
                }
                out.append(s, p, q - p);
                out += repl;
                p = q + from.size();
            }
            u::push(d, keel::make_string(std::move(out))); });

        a.register_func("split", KeelF {
            if (!u::need(d, 2, "split"))
                return;
            const std::string sep = u::peek(d, 0).toString();
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            if (!u::check_alloc(d, s.size() * 128, "split"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            keel::Value list = keel::make_list();
            if (sep.empty())
            {
                // 按空白切分，丢弃空片段
                size_t i = 0;
                while (i < s.size())
                {
                    while (i < s.size() && is_space(s[i]))
                        ++i;
                    size_t j = i;
                    while (j < s.size() && !is_space(s[j]))
                        ++j;
                    if (j > i)
                        list.push(keel::make_string(s.substr(i, j - i)));
                    i = j;
                }
            }
            else
            {
                size_t p = 0;
                while (true)
                {
                    const size_t q = s.find(sep, p);
                    if (q == std::string::npos)
                    {
                        list.push(keel::make_string(s.substr(p)));
                        break;
                    }
                    list.push(keel::make_string(s.substr(p, q - p)));
                    p = q + sep.size();
                }
            }
            u::push(d, std::move(list)); });

        a.register_func("join", KeelF {
            if (!u::need(d, 2, "join"))
                return;
            const std::string sep = u::peek(d, 0).toString();
            keel::Value lv;
            if (!u::peek_list(d, 1, "join", lv))
            {
                u::drop_n(d, 2);
                u::push(d, keel::make_nil());
                return;
            }
            u::drop_n(d, 2);
            std::string out;
            const std::vector<keel::Value> &items = lv.asList();
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i)
                    out += sep;
                out += items[i].toString();
            }
            u::push(d, keel::make_string(std::move(out))); });

        a.register_func("repeat", KeelF {
            if (!u::need(d, 2, "repeat"))
                return;
            double n = 0;
            if (!u::peek_num(d, 0, "repeat", n))
            {
                u::drop_n(d, 2);
                u::push(d, keel::make_nil());
                return;
            }
            const std::string s = u::peek(d, 1).toString();
            u::drop_n(d, 2);
            const int times = u::to_int(n);
            if (times <= 0 || s.empty())
            {
                u::push(d, keel::make_string(std::string()));
                return;
            }
            const size_t total = s.size() * static_cast<size_t>(times);
            if (!u::check_alloc(d, total, "repeat"))
            {
                u::push(d, keel::make_nil());
                return;
            }
            std::string out;
            out.reserve(s.size() * static_cast<size_t>(times));
            for (int i = 0; i < times; ++i)
                out += s;
            u::push(d, keel::make_string(std::move(out))); });

        a.register_func("reverse", KeelF {
            std::string s;
            if (!u::pop_text(d, "reverse", s))
                return;
            std::reverse(s.begin(), s.end());
            u::push(d, keel::make_string(std::move(s))); });

        // ------------------------------------------------------------ 别名
        alias(a, "strip", "trim");
        alias(a, "starts", "startswith");
        alias(a, "ends", "endswith");
        alias(a, "slice", "substr");
        alias(a, "indexof", "find");
        alias(a, "lowercase", "lower");
        alias(a, "uppercase", "upper");
    }
}
