#pragma once
// ============================================================================
//  KeelSyntax.hpp —— 前端公共类型：token / 诊断 / 选项 / 词法
// ----------------------------------------------------------------------------
//  被两个前端共用：
//    * KeelLoader.hpp  —— 核心语法（body = 入口块）
//    * KeelSurface.hpp —— 表面语法（let / if / while / fn ... 脱糖成核心 token）
//  这里只有"源 -> token"的公共部分，不认识块、不认识 VM。
// ============================================================================

#include "Keel.hpp" // keel::fast_atof

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace keelld
{
    enum class Sev
    {
        Error,
        Warning
    };

    /// 一条诊断。file 为空 = 装载器/前端层面的问题（不是某一行源码）。
    struct Diagnostic
    {
        Sev sev = Sev::Error;
        std::string file; // 源文件路径；`<text>` 表示直接传进来的字符串
        int line = 0;     // 0 = 不知道
        int col = 0;
        std::string message;

        bool fatal() const { return sev == Sev::Error; }
    };

    enum class TokenKind
    {
        Word,   // 普通词（指令名 / 字面量 / 块名）
        String, // 引号包起来的（text 保留外层引号，和 fast_split_tokens 一致）
        Number  // 看起来是数字（VM 运行时仍会自己 parse，这里只给诊断/高亮用）
    };

    struct Token
    {
        std::string text;
        TokenKind kind = TokenKind::Word;
        int line = 1; // 1-based，第一个字符所在行
        int col = 1;
    };

    /// 前端语法。Core = 直接是核心 token；Surface = 先脱糖成核心 token。
    enum class Syntax
    {
        Core,
        Surface
    };

    struct Options
    {
        std::string entry = "main";
        Syntax syntax = Syntax::Core;
        bool hash_comments = true; // `#` 到行尾也当注释（`//` 始终支持）
        std::size_t max_tokens = 200000;
        std::size_t max_bytes = 8u << 20; // 单文件上限（含 include）
        std::size_t max_blocks = 256;     // 静态扫描最多记多少个块
    };

    // ======================================================================== 输入卫生
    // UTF-8 BOM（EF BB BF）：Windows 编辑器常加，不剥会粘在第一个 token 上。
    // 必须用 unsigned char 比较：char 多为 signed，0xEF 直接比永远不等。
    inline void strip_bom_inplace(std::string &s)
    {
        if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
            s.erase(0, 3);
    }

    namespace detail
    {
        inline std::string unquote(const std::string &t)
        {
            if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') ||
                                  (t.front() == '\'' && t.back() == '\'')))
                return t.substr(1, t.size() - 2);
            return t;
        }

        /// 看起来是不是数字（只影响 TokenKind；真正的解析在 VM 里）
        inline bool looks_number(std::string_view s)
        {
            double v = 0;
            return keel::fast_atof(s, v);
        }

        /// 检测 UTF-16/二进制：这类文件按 UTF-8 切出来全是乱码 token，早点报清楚。
        inline bool looks_utf16_or_binary(std::string_view s)
        {
            if (s.size() >= 2)
            {
                const auto a = static_cast<unsigned char>(s[0]);
                const auto b = static_cast<unsigned char>(s[1]);
                if ((a == 0xFF && b == 0xFE) || (a == 0xFE && b == 0xFF))
                    return true;
            }
            const std::size_t n = std::min<std::size_t>(s.size(), 1024);
            for (std::size_t i = 0; i < n; ++i)
                if (s[i] == '\0')
                    return true;
            return false;
        }
    } // namespace detail

    // ======================================================================== 核心词法
    // 规则（和 keel::fast_split_tokens 对齐，另加行列与 `#` 注释）：
    //   * 空白分隔；引号（单/双）保护空格，引号本身留在 token 里；
    //   * `//` 到行尾是注释；`#` 在 token 开头时到行尾也是注释（Options 可关）；
    //   * 不处理转义；引号没闭合 -> 诊断（token 仍保留，方便定位）。
    inline std::vector<Token> tokenize(std::string_view src, std::vector<Diagnostic> &diags,
                                       const Options &opt, const std::string &file)
    {
        std::vector<Token> out;
        out.reserve(src.size() / 4 + 1);

        std::size_t i = 0;
        int line = 1;
        int col = 1;
        std::string cur;
        int cur_line = 1;
        int cur_col = 1;
        bool in_quotes = false;
        char quote = 0;
        int quote_line = 1;

        auto put = [&] {
            if (cur.empty())
                return;
            Token t;
            t.text = std::move(cur);
            t.line = cur_line;
            t.col = cur_col;
            if (t.text.size() >= 2 &&
                ((t.text.front() == '"' && t.text.back() == '"') ||
                 (t.text.front() == '\'' && t.text.back() == '\'')))
                t.kind = TokenKind::String;
            else if (detail::looks_number(t.text))
                t.kind = TokenKind::Number;
            out.push_back(std::move(t));
            cur.clear();
        };
        auto step = [&] {
            if (src[i] == '\n')
            {
                ++line;
                col = 1;
            }
            else
                ++col;
            ++i;
        };
        auto to_eol = [&] {
            while (i < src.size() && src[i] != '\n')
                step();
        };

        while (i < src.size())
        {
            if (out.size() >= opt.max_tokens)
            {
                diags.push_back(Diagnostic{Sev::Error, file, line, col,
                                           "token 数量超过上限 " + std::to_string(opt.max_tokens)});
                return out;
            }

            const char c = src[i];

            if (in_quotes)
            {
                cur.push_back(c);
                if (c == quote)
                    in_quotes = false;
                step();
                continue;
            }

            if (c == '"' || c == '\'')
            {
                if (cur.empty())
                {
                    cur_line = line;
                    cur_col = col;
                }
                in_quotes = true;
                quote = c;
                quote_line = line;
                cur.push_back(c);
                step();
                continue;
            }

            if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
            {
                put();
                to_eol();
                continue;
            }

            if (c == '#' && opt.hash_comments && cur.empty())
            {
                to_eol();
                continue;
            }

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                put();
                step();
                continue;
            }

            if (cur.empty())
            {
                cur_line = line;
                cur_col = col;
            }
            cur.push_back(c);
            step();
        }

        put();
        if (in_quotes)
            diags.push_back(Diagnostic{Sev::Error, file, quote_line, 0,
                                       "字符串没有闭合（缺右引号）"});
        return out;
    }
} // namespace keelld
