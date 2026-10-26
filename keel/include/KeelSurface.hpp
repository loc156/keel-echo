#pragma once
// ============================================================================
//  KeelSurface.hpp —— 表面语法前端（第一版，长期项目）
// ----------------------------------------------------------------------------
//  目的：让人（和模型）能写
//
//      let count = 0
//      if button("+1") { count = count + 1 }
//
//  然后脱糖成核心 token：
//
//      0 "count" store
//      "+1" "ui.button" call if "count" load 1 s.add "count" store then
//
//  **这一层只脱糖，不加运行时语义**：
//    * let / = 都是 store（这门语言没有块作用域，变量就是全局的）；
//    * if / while 直接落到核心的 if/then、begin/again/break；
//    * fn name { ... } 就是核心的 "name" { ... }（运行时定义块）；
//    * 没有局部变量、没有闭包、没有带参函数（要这些是运行时改动，不是糖）。
//
//  调用规则（v1）：
//    * `name(args...)` 里，name 是本文件 fn 定义的块 -> `args "name" call`；
//    * 其它 name（`ui.button`、`str`、`println` ...）-> 直接当指令：`args name`；
//    * 想调外部块：`invoke("name")`（只接受一个字符串参数）。
//
//  运算符（v1）：
//    +   -> s.add（有字符串就拼接，否则相加；由 surface::register_runtime 注册）
//    ..  -> concat（强制拼接）
//    - * / % == != < <= > >= && || !  -> 核心同名（&& -> and，|| -> or，! -> not）
//    优先级：or < and < 比较 < .. < +- < */% < 一元 < 调用/括号
//
//  已知限制（都是 v1 故意不做的）：
//    * 语句以换行/分号分隔；表达式里换行会结束表达式（括号内可以换行）；
//    * 字符串支持 \n \t \r \\ \" \'；核心 token 里不能同时出现单双引号；
//    * fn 不接受参数；调用外部块只能用 invoke；
//    * 没有 return / for / 数组字面量 / 结构体；
//    * 表达式语句不自动 drop（值会留在栈上）。
// ============================================================================

#include "Keel.hpp"
#include "KeelSyntax.hpp"
#include "KeelUtil.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace keelld
{
    namespace surface
    {
        namespace u = keel::util;

        namespace detail
        {
            enum class SK
            {
                Word,
                Number,
                String,
                Op,
                Newline,
                End
            };

            struct SToken
            {
                SK kind = SK::End;
                std::string text; // String 里是**已解开转义**的内容（不带引号）
                int line = 1;
                int col = 1;
            };

            inline bool ident_start(char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            }
            inline bool ident_char(char c)
            {
                return ident_start(c) || (c >= '0' && c <= '9');
            }
            inline bool digit(char c) { return c >= '0' && c <= '9'; }

            /// 按 UTF-8 码点计列：续接字节（10xxxxxx）不增加列号
            inline void bump(char c, int &line, int &col)
            {
                if (c == '\n')
                {
                    ++line;
                    col = 1;
                }
                else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                    ++col;
            }

            inline std::vector<SToken> lex(std::string_view src, std::vector<Diagnostic> &diags,
                                           const Options &opt, const std::string &file)
            {
                std::vector<SToken> out;
                std::size_t i = 0;
                int line = 1, col = 1;

                auto push = [&](SK k, std::string text, int l, int c) {
                    out.push_back(SToken{k, std::move(text), l, c});
                };
                auto advance = [&] {
                    bump(src[i], line, col);
                    ++i;
                };

                while (i < src.size())
                {
                    const char c = src[i];
                    if (c == ' ' || c == '\t' || c == '\r')
                    {
                        advance();
                        continue;
                    }
                    if (c == '\n')
                    {
                        push(SK::Newline, "\n", line, col);
                        advance();
                        continue;
                    }
                    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
                    {
                        while (i < src.size() && src[i] != '\n')
                            advance();
                        continue;
                    }
                    if (c == '#' && opt.hash_comments)
                    {
                        while (i < src.size() && src[i] != '\n')
                            advance();
                        continue;
                    }
                    if (c == '"' || c == '\'')
                    {
                        const char q = c;
                        const int l0 = line, c0 = col;
                        advance(); // 开引号
                        std::string text;
                        bool closed = false;
                        while (i < src.size())
                        {
                            const char d = src[i];
                            if (d == q)
                            {
                                advance();
                                closed = true;
                                break;
                            }
                            if (d == '\\' && i + 1 < src.size())
                            {
                                advance();
                                const char e = src[i];
                                switch (e)
                                {
                                case 'n':
                                    text.push_back('\n');
                                    break;
                                case 't':
                                    text.push_back('\t');
                                    break;
                                case 'r':
                                    text.push_back('\r');
                                    break;
                                case '\\':
                                    text.push_back('\\');
                                    break;
                                case '"':
                                    text.push_back('"');
                                    break;
                                case '\'':
                                    text.push_back('\'');
                                    break;
                                default:
                                    text.push_back(e);
                                    break;
                                }
                                advance();
                                continue;
                            }
                            text.push_back(d);
                            advance();
                        }
                        if (!closed)
                            diags.push_back(Diagnostic{Sev::Error, file, l0, c0,
                                                       "字符串没有闭合（缺右引号）"});
                        push(SK::String, std::move(text), l0, c0);
                        continue;
                    }
                    if (digit(c) || (c == '.' && i + 1 < src.size() && digit(src[i + 1])))
                    {
                        const int l0 = line, c0 = col;
                        std::string text;
                        bool seen_dot = false;
                        while (i < src.size())
                        {
                            const char d = src[i];
                            if (digit(d))
                            {
                                text.push_back(d);
                                advance();
                            }
                            else if (d == '.' && !seen_dot)
                            {
                                seen_dot = true;
                                text.push_back(d);
                                advance();
                            }
                            else
                                break;
                        }
                        if (i < src.size() && (src[i] == 'e' || src[i] == 'E'))
                        {
                            text.push_back(src[i]);
                            advance();
                            if (i < src.size() && (src[i] == '+' || src[i] == '-'))
                            {
                                text.push_back(src[i]);
                                advance();
                            }
                            while (i < src.size() && digit(src[i]))
                            {
                                text.push_back(src[i]);
                                advance();
                            }
                        }
                        push(SK::Number, std::move(text), l0, c0);
                        continue;
                    }
                    if (ident_start(c))
                    {
                        const int l0 = line, c0 = col;
                        std::string text;
                        while (i < src.size() && ident_char(src[i]))
                        {
                            text.push_back(src[i]);
                            advance();
                        }
                        // 带点名字（ui.button）：点后面必须还是标识符字符；".." 是运算符
                        while (i + 1 < src.size() && src[i] == '.' && ident_char(src[i + 1]))
                        {
                            text.push_back('.');
                            advance();
                            while (i < src.size() && ident_char(src[i]))
                            {
                                text.push_back(src[i]);
                                advance();
                            }
                        }
                        push(SK::Word, std::move(text), l0, c0);
                        continue;
                    }

                    // 运算符：先看两字符
                    const int l0 = line, c0 = col;
                    if (i + 1 < src.size())
                    {
                        const std::string two(src.substr(i, 2));
                        if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                            two == "&&" || two == "||" || two == "..")
                        {
                            advance();
                            advance();
                            push(SK::Op, two, l0, c0);
                            continue;
                        }
                    }
                    if (std::string("+-*/%<>=!(){} ,;").find(c) != std::string::npos && c != ' ')
                    {
                        advance();
                        push(SK::Op, std::string(1, c), l0, c0);
                        continue;
                    }

                    diags.push_back(Diagnostic{Sev::Error, file, l0, c0,
                                               std::string("不认识的字符: '") + c + "'"});
                    advance();
                }
                push(SK::End, std::string(), line, col);
                return out;
            }

            /// 把已解转义的字符串包成核心 token；核心没有转义，所以只能换引号。
            inline bool core_string(const std::string &s, std::vector<Diagnostic> &diags,
                                    const std::string &file, int line, int col)
            {
                const bool has_d = s.find('"') != std::string::npos;
                const bool has_s = s.find('\'') != std::string::npos;
                if (has_d && has_s)
                {
                    diags.push_back(Diagnostic{Sev::Error, file, line, col,
                                               "字符串同时含单双引号，核心 token 无法表示（v1 限制）"});
                    return false;
                }
                return true;
            }
        } // namespace detail

        // ------------------------------------------------------------------ 编译器
        class Compiler
        {
        public:
            Compiler(std::vector<detail::SToken> toks, std::string file, const Options &opt,
                     std::vector<Diagnostic> &diags)
                : ts_(std::move(toks)), file_(std::move(file)), opt_(opt), diags_(diags)
            {
                collect_fns();
            }

            std::vector<Token> run()
            {
                skip_separators();
                while (!at_end())
                {
                    parse_statement();
                    skip_separators();
                }
                return std::move(out_);
            }

        private:
            using SK = detail::SK;
            using SToken = detail::SToken;

            // ---------------------------------------------------------- 小工具
            const SToken &peek(std::size_t k = 0) const
            {
                const std::size_t idx = i_ + k;
                return idx < ts_.size() ? ts_[idx] : ts_.back();
            }
            bool at_end() const { return peek().kind == SK::End; }
            bool is_op(const std::string &s) const { return peek().kind == SK::Op && peek().text == s; }
            bool is_word(const std::string &s) const
            {
                return peek().kind == SK::Word && peek().text == s;
            }
            void advance()
            {
                if (i_ + 1 < ts_.size())
                    ++i_;
            }
            void diag(int line, int col, const std::string &msg)
            {
                diags_.push_back(Diagnostic{Sev::Error, file_, line, col, msg});
            }
            void skip_separators()
            {
                while (peek().kind == SK::Newline || is_op(";"))
                    advance();
            }
            void skip_newlines_in_parens()
            {
                while (paren_ > 0 && peek().kind == SK::Newline)
                    advance();
            }
            void emit(const std::string &text, TokenKind kind, int line, int col)
            {
                out_.push_back(Token{text, kind, line, col});
            }
            void emit_word(const std::string &t, const SToken &pos)
            {
                emit(t, TokenKind::Word, pos.line, pos.col);
            }
            void emit_str(const std::string &s, const SToken &pos)
            {
                if (!detail::core_string(s, diags_, file_, pos.line, pos.col))
                    return;
                const char q = s.find('"') != std::string::npos ? '\'' : '"';
                emit(std::string(1, q) + s + std::string(1, q), TokenKind::String, pos.line, pos.col);
            }
            void emit_load(const std::string &name, const SToken &pos)
            {
                emit_str(name, pos);
                emit_word("load", pos);
            }
            void emit_store(const std::string &name, const SToken &pos)
            {
                emit_str(name, pos);
                emit_word("store", pos);
            }
            std::string expect_word(const char *what)
            {
                if (peek().kind != SK::Word)
                {
                    diag(peek().line, peek().col, std::string("这里需要") + what);
                    return {};
                }
                std::string s = peek().text;
                advance();
                return s;
            }
            bool expect_op(const char *op)
            {
                if (!is_op(op))
                {
                    diag(peek().line, peek().col, std::string("这里需要 '") + op + "'");
                    return false;
                }
                advance();
                return true;
            }

            // ---------------------------------------------------------- 预处理
            // 先扫一遍 fn 名字：调用在定义之前也能判断"这是块还是指令"
            void collect_fns()
            {
                std::size_t i = 0;
                while (i < ts_.size())
                {
                    const SToken &t = ts_[i];
                    if (t.kind == SK::Word && t.text == "raw")
                    {
                        // raw { ... } 里的东西是核心 token，里面的 fn 不算
                        std::size_t j = i + 1;
                        while (j < ts_.size() && ts_[j].kind == SK::Newline)
                            ++j;
                        if (j < ts_.size() && ts_[j].kind == SK::Op && ts_[j].text == "{")
                        {
                            int depth = 1;
                            ++j;
                            for (; j < ts_.size() && depth > 0; ++j)
                            {
                                if (ts_[j].kind == SK::Op && ts_[j].text == "{")
                                    ++depth;
                                else if (ts_[j].kind == SK::Op && ts_[j].text == "}")
                                    --depth;
                            }
                            i = j;
                            continue;
                        }
                    }
                    else if (t.kind == SK::Word && t.text == "fn")
                    {
                        std::size_t j = i + 1;
                        while (j < ts_.size() && ts_[j].kind == SK::Newline)
                            ++j;
                        if (j < ts_.size() && ts_[j].kind == SK::Word)
                            fns_.insert(ts_[j].text);
                    }
                    ++i;
                }
            }

            // ---------------------------------------------------------- 语句
            void parse_statement()
            {
                skip_separators();
                if (at_end())
                    return;
                const SToken t = peek();
                if (t.kind == SK::Word)
                {
                    if (t.text == "let")
                    {
                        parse_let();
                        return;
                    }
                    if (t.text == "print")
                    {
                        advance();
                        parse_expr(0);
                        emit_word("println", t);
                        return;
                    }
                    if (t.text == "if")
                    {
                        parse_if();
                        return;
                    }
                    if (t.text == "while")
                    {
                        parse_while();
                        return;
                    }
                    if (t.text == "fn")
                    {
                        parse_fn();
                        return;
                    }
                    if (t.text == "raw")
                    {
                        parse_raw();
                        return;
                    }
                    if (t.text == "include")
                    {
                        advance();
                        if (peek().kind != SK::String)
                        {
                            diag(t.line, t.col, "include 后面需要字符串路径");
                            return;
                        }
                        emit_str(peek().text, peek());
                        emit_word("include", t);
                        advance();
                        return;
                    }
                    if (t.text == "return")
                    {                        advance();
                        const SToken &nxt = peek();
                        const bool bare = nxt.kind == SK::Newline || nxt.kind == SK::End ||
                                          (nxt.kind == SK::Op &&
                                           (nxt.text == ";" || nxt.text == "}"));
                        if (!bare)
                            parse_expr(0);
                        emit_word("ret", t);
                        return;
                    }
                    if (t.text == "else")
                    {
                        diag(t.line, t.col, "else 没有匹配的 if");
                        advance();
                        return;
                    }
                    // 赋值：name = expr（name 不能带点）
                    if (t.text.find('.') == std::string::npos && peek(1).kind == SK::Op &&
                        peek(1).text == "=")
                    {
                        const SToken name = t;
                        advance(); // name
                        advance(); // =
                        parse_expr(0);
                        emit_store(name.text, name);
                        return;
                    }

                    // 单独一个词：当指令语句（break / drop / dup / ret / halt ...），
                    // 不是变量读取；读变量请写 print x / 参与表达式。
                    const SToken &nxt = peek(1);
                    const bool stmt_end = nxt.kind == SK::Newline || nxt.kind == SK::End ||
                                          (nxt.kind == SK::Op && (nxt.text == ";" || nxt.text == "}"));
                    if (stmt_end)
                    {
                        emit_word(t.text, t);
                        advance();
                        return;
                    }
                }
                parse_expr(0); // 表达式语句
            }

            void parse_let()
            {
                const SToken let = peek();
                advance();
                const std::string name = expect_word("变量名");
                if (name.empty())
                    return;
                if (name.find('.') != std::string::npos)
                    diag(let.line, let.col, "let 的变量名不能带 '.'");
                if (!is_op("="))
                {
                    diag(peek().line, peek().col, "let 需要 '= 表达式'");
                    return;
                }
                advance();
                parse_expr(0);
                emit_store(name, let);
            }

            void parse_if()
            {
                const SToken kw = peek();
                advance(); // if
                parse_expr(0);
                emit_word("if", kw);
                parse_block();
                if (is_word("else"))
                {
                    advance();
                    emit_word("else", kw);
                    if (is_word("if"))
                        parse_if();
                    else
                        parse_block();
                }
                emit_word("then", kw);
            }

            void parse_while()
            {
                const SToken kw = peek();
                advance(); // while
                emit_word("begin", kw);
                parse_expr(0);
                emit_word("not", kw);
                emit_word("if", kw);
                emit_word("break", kw);
                emit_word("then", kw);
                parse_block();
                emit_word("again", kw);
            }

            void parse_fn()
            {
                const SToken kw = peek();
                advance(); // fn
                const std::string name = expect_word("块名");
                if (name.empty())
                    return;
                if (name.find('.') != std::string::npos)
                    diag(kw.line, kw.col, "fn 的块名不能带 '.'");
                if (is_op("("))
                {
                    diag(peek().line, peek().col, "fn 暂不支持参数；调用方按栈约定传值（v1 限制）");
                    int depth = 0;
                    while (!at_end())
                    {
                        if (is_op("("))
                            ++depth;
                        else if (is_op(")"))
                        {
                            --depth;
                            advance();
                            if (depth == 0)
                                break;
                            continue;
                        }
                        advance();
                    }
                }
                // 先解析体到 out_ 末尾，再整体包成 "name" { ... }
                const std::size_t start = out_.size();
                parse_block();
                std::vector<Token> body(out_.begin() + static_cast<std::ptrdiff_t>(start), out_.end());
                out_.resize(start);
                emit_str(name, kw);
                emit_word("{", kw);
                out_.insert(out_.end(), body.begin(), body.end());
                emit_word("}", kw);
            }

            void parse_raw()
            {
                const SToken kw = peek();
                advance(); // raw
                if (!is_op("{"))
                {
                    diag(peek().line, peek().col, "raw 后面需要 '{ ... }'");
                    return;
                }
                advance(); // {
                int depth = 1;
                while (!at_end())
                {
                    const SToken t = peek();
                    if (t.kind == SK::Op && t.text == "{")
                    {
                        ++depth;
                        emit_word("{", t);
                        advance();
                        continue;
                    }
                    if (t.kind == SK::Op && t.text == "}")
                    {
                        if (--depth == 0)
                        {
                            advance();
                            return;
                        }
                        emit_word("}", t);
                        advance();
                        continue;
                    }
                    if (t.kind == SK::Newline || (t.kind == SK::Op && t.text == ";"))
                    {
                        advance();
                        continue;
                    }
                    if (t.kind == SK::String)
                    {
                        emit_str(t.text, t);
                        advance();
                        continue;
                    }
                    emit(t.text, t.kind == SK::Number ? TokenKind::Number : TokenKind::Word, t.line,
                         t.col);
                    advance();
                }
                diag(kw.line, kw.col, "raw 的 '{' 没有闭合");
            }

            void parse_block()
            {
                if (!expect_op("{"))
                    return;
                skip_separators();
                while (!at_end() && !is_op("}"))
                {
                    parse_statement();
                    skip_separators();
                }
                expect_op("}");
            }

            // ---------------------------------------------------------- 表达式
            static int binding_power(const std::string &op)
            {
                if (op == "or" || op == "||")
                    return 1;
                if (op == "and" || op == "&&")
                    return 2;
                if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")
                    return 3;
                if (op == "..")
                    return 4;
                if (op == "+" || op == "-")
                    return 5;
                if (op == "*" || op == "/" || op == "%")
                    return 6;
                return 0;
            }

            void emit_binop(const SToken &op)
            {
                if (op.text == "+")
                    emit_word("s.add", op);
                else if (op.text == "..")
                    emit_word("concat", op);
                else if (op.text == "&&" || op.text == "and")
                    emit_word("and", op);
                else if (op.text == "||" || op.text == "or")
                    emit_word("or", op);
                else
                    emit_word(op.text, op);
            }

            void parse_expr(int min_bp)
            {
                parse_unary();
                for (;;)
                {
                    skip_newlines_in_parens();
                    if (at_end() || (paren_ == 0 && peek().kind == SK::Newline))
                        return;
                    const SToken &t = peek();
                    int bp = 0;
                    if (t.kind == SK::Op)
                        bp = binding_power(t.text);
                    else if (t.kind == SK::Word && (t.text == "and" || t.text == "or"))
                        bp = binding_power(t.text);
                    if (bp == 0 || bp < min_bp)
                        return;
                    const SToken op = t;
                    advance();
                    parse_expr(bp + 1);
                    emit_binop(op);
                }
            }

            void parse_unary()
            {
                skip_newlines_in_parens();
                const SToken t = peek();
                if (t.kind == SK::Op && t.text == "-")
                {
                    advance();
                    parse_unary();
                    emit_word("neg", t);
                    return;
                }
                if ((t.kind == SK::Op && t.text == "!") || (t.kind == SK::Word && t.text == "not"))
                {
                    advance();
                    parse_unary();
                    emit_word("not", t);
                    return;
                }
                parse_primary();
            }

            void parse_primary()
            {
                skip_newlines_in_parens();
                const SToken t = peek();
                if (t.kind == SK::Number)
                {
                    emit(t.text, TokenKind::Number, t.line, t.col);
                    advance();
                    return;
                }
                if (t.kind == SK::String)
                {
                    emit_str(t.text, t);
                    advance();
                    return;
                }
                if (t.kind == SK::Op && t.text == "(")
                {
                    advance();
                    ++paren_;
                    parse_expr(0);
                    --paren_;
                    if (!expect_op(")"))
                        return;
                    return;
                }
                if (t.kind == SK::Word)
                {
                    if (t.text == "true" || t.text == "false" || t.text == "nil")
                    {
                        emit_word(t.text, t);
                        advance();
                        return;
                    }
                    const SToken name = t;
                    advance();
                    if (is_op("("))
                    {
                        parse_call(name);
                        return;
                    }
                    emit_load(name.text, name);
                    return;
                }
                diag(t.line, t.col, "这里需要表达式");
                advance();
            }

            void parse_call(const SToken &name)
            {
                advance(); // (
                ++paren_;
                int argc = 0;
                if (!is_op(")"))
                {
                    parse_expr(0);
                    ++argc;
                    while (is_op(","))
                    {
                        advance();
                        parse_expr(0);
                        ++argc;
                    }
                }
                --paren_;
                if (!expect_op(")"))
                    return;

                if (name.text == "invoke")
                {
                    if (argc != 1)
                    {
                        diag(name.line, name.col, "invoke 只接受一个字符串参数");
                        return; // 不再生成 call，避免错误的表达式继续往下跑
                    }
                    emit_word("call", name);
                    return;
                }
                if (fns_.count(name.text))
                {
                    emit_str(name.text, name);
                    emit_word("call", name);
                    return;
                }
                // 不是本文件的 fn：当指令直接调用
                emit_word(name.text, name);
            }

            std::vector<SToken> ts_;
            std::size_t i_ = 0;
            std::string file_;
            Options opt_;
            std::vector<Diagnostic> &diags_;
            std::vector<Token> out_;
            std::unordered_set<std::string> fns_;
            int paren_ = 0;
        };

        // ------------------------------------------------------------------ 公共入口
        inline std::vector<Token> compile(std::string_view src, const std::string &file,
                                          const Options &opt, std::vector<Diagnostic> &diags)
        {
            auto toks = detail::lex(src, diags, opt, file);
            for (const auto &d : diags)
                if (d.fatal())
                    return {};
            Compiler c(std::move(toks), file, opt, diags);
            return c.run();
        }

        /// 表面语法用到的运行时小指令。目前只有一个：
        ///   s.add  a b -- a+b（两边都能当数字）或 a..b（任一为字符串）
        inline void register_runtime(keel::Keel &a)
        {
            if (a.data.funcs.count("s.add"))
                return;
            a.register_func("s.add", KeelF {
                if (!u::need(d, 2, "s.add"))
                    return;
                keel::Value rhs = u::pop(d);
                keel::Value lhs = u::pop(d);
                if (lhs.isString() || rhs.isString())
                {
                    u::push(d, keel::make_string(lhs.toString() + rhs.toString()));
                    return;
                }
                double x = 0;
                double y = 0;
                if (!u::num(lhs, x) || !u::num(rhs, y))
                {
                    u::err("s.add", "operands must be numbers or strings");
                    u::push(d, keel::make_nil());
                    return;
                }
                u::push(d, keel::make_number(x + y));
            });
        }
    } // namespace surface
} // namespace keelld
