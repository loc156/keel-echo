#pragma once
// ============================================================================
// KeelJSON.hpp -- JSON 编解码（keel::Value <-> 文本）
// ----------------------------------------------------------------------------
//  为什么单独一个头：
//    Value 只负责"存值"，parse_value 只负责把语言里的标量 token
//    （"1" / "true" / "abc"）猜成值。JSON 是另一件事：要处理转义、嵌套、
//    位置报错，而且 agent 必须靠它跟 HTTP 接口交换数据。混进 parse_value 会
//    顺手改坏语言的字面量语义，所以分开。
//
//  用法：
//      keel::Value v;
//      if (!keel::json_decode(body, v)) { /* 失败原因已 report 到 keel_errors() */ }
//      std::string req = keel::json_encode(v);         // 紧凑
//      std::string doc = keel::json_encode(v, true);   // 缩进 2 空格
//
//  映射：
//      nil -> null      bool -> true/false     number -> 数字
//      string -> "..."  list -> [...]          map / umap -> {...}
//    解码只会产生 map，不会产生 umap（JSON 没有"无序对象"这个概念）。
//    NaN / Infinity 值写出去是 null（JSON 里没有这两个），也不接受它们的字面量。
//
//  出错风格跟 Keel 其余部分一致：不抛异常，走 keel::Error(msg, index).report()，
//  消息里带"第几个字节"。json_decode 返回 false，并把 out 置成 nil。
//
//  不支持：注释、尾逗号、单引号字符串。够用了。
// ============================================================================

#include "KeelValue.hpp"

#include <cmath>  // std::isnan / std::isinf / std::floor / std::fabs
#include <cstdio> // std::snprintf
#include <string>
#include <string_view>
#include <vector>

namespace keel
{
    namespace json_detail
    {
        // 嵌套上限：防御损坏/恶意的输入把栈打爆，正常报文远到不了
        constexpr int kMaxDepth = 100;

        inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

        // 数字 -> JSON 文本。
        // 注意别用 Value::toString()：它走 ostringstream，默认只有 6 位有效数字
        // （"2.33333"），拿来拼请求体会悄悄丢精度。
        inline std::string number_text(double v)
        {
            if (std::isnan(v) || std::isinf(v))
                return "null";
            char buf[64];
            if (v == std::floor(v) && std::fabs(v) < 1e15)
                std::snprintf(buf, sizeof buf, "%.0f", v);
            else
                std::snprintf(buf, sizeof buf, "%.15g", v);
            return std::string(buf);
        }

        // 写一个带双引号的 JSON 字符串（UTF-8 原样保留）
        inline void write_string(std::string &out, const std::string &s)
        {
            out += '"';
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                        out += buf;
                    }
                    else
                    {
                        out += static_cast<char>(c);
                    }
                }
            }
            out += '"';
        }

        inline void write_value(std::string &out, const Value &v, bool pretty, int indent, int depth)
        {
            if (depth > kMaxDepth)
            {
                out += "null"; // 超深就退化成 null，不递归
                return;
            }
            const auto newline_indent = [&](int d)
            {
                if (!pretty)
                    return;
                out += '\n';
                out.append(static_cast<size_t>(indent * d), ' ');
            };

            switch (v.type)
            {
            case Type::Nil:
                out += "null";
                break;
            case Type::Bool:
                out += v.b ? "true" : "false";
                break;
            case Type::Number:
                out += number_text(v.n);
                break;
            case Type::String:
                write_string(out, v.s);
                break;
            case Type::List:
            {
                out += '[';
                const size_t n = v.list ? v.list->size() : 0;
                for (size_t i = 0; i < n; ++i)
                {
                    if (i)
                        out += ',';
                    newline_indent(depth + 1);
                    write_value(out, (*v.list)[i], pretty, indent, depth + 1);
                }
                if (n)
                    newline_indent(depth);
                out += ']';
                break;
            }
            case Type::Map:
            {
                out += '{';
                size_t i = 0;
                if (v.map)
                {
                    for (const auto &kv : *v.map)
                    {
                        if (i++)
                            out += ',';
                        newline_indent(depth + 1);
                        write_string(out, kv.first);
                        out += pretty ? ": " : ":";
                        write_value(out, kv.second, pretty, indent, depth + 1);
                    }
                }
                if (v.map && !v.map->empty())
                    newline_indent(depth);
                out += '}';
                break;
            }
            case Type::UMap:
            {
                // 没有序也是合法 JSON，直接按对象写
                out += '{';
                size_t i = 0;
                if (v.unmap)
                {
                    for (const auto &kv : *v.unmap)
                    {
                        if (i++)
                            out += ',';
                        newline_indent(depth + 1);
                        write_string(out, kv.first);
                        out += pretty ? ": " : ":";
                        write_value(out, kv.second, pretty, indent, depth + 1);
                    }
                }
                if (v.unmap && !v.unmap->empty())
                    newline_indent(depth);
                out += '}';
                break;
            }
            }
        }

        // 递归下降读一个 JSON 文本。
        // 不抛异常：出错时置 bad_ 并 report 一次（后续调用直接返回 nil），
        // 这样错误信息只出现一条，调用方看返回值就行。
        struct Reader
        {
            std::string_view s;
            size_t p = 0;
            bool bad = false;

            explicit Reader(std::string_view text) : s(text) {}

            bool at_end() const { return p >= s.size(); }
            char peek() const { return s[p]; }

            void fail(const std::string &msg)
            {
                if (bad)
                    return;
                bad = true;
                Error::with_index("json: " + msg, static_cast<int>(p)).report();
            }

            void skip_ws()
            {
                while (!at_end())
                {
                    const char c = s[p];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                        ++p;
                    else
                        break;
                }
            }

            bool expect(char c)
            {
                if (at_end() || s[p] != c)
                {
                    fail(std::string("expected '") + c + "'");
                    return false;
                }
                ++p;
                return true;
            }

            bool word(std::string_view w)
            {
                if (s.size() - p < w.size() || s.compare(p, w.size(), w) != 0)
                {
                    fail("expected " + std::string(w));
                    return false;
                }
                p += w.size();
                return true;
            }

            Value value(int depth)
            {
                if (bad)
                    return Value{};
                if (depth > kMaxDepth)
                {
                    fail("nesting too deep (max " + std::to_string(kMaxDepth) + ")");
                    return Value{};
                }
                if (at_end())
                {
                    fail("unexpected end of input");
                    return Value{};
                }

                switch (peek())
                {
                case 'n':
                    if (!word("null"))
                        return Value{};
                    return make_nil();
                case 't':
                    if (!word("true"))
                        return Value{};
                    return make_bool(true);
                case 'f':
                    if (!word("false"))
                        return Value{};
                    return make_bool(false);
                case '"':
                {
                    std::string text;
                    if (!string_lit(text))
                        return Value{};
                    return make_string(std::move(text));
                }
                case '[':
                    return array(depth);
                case '{':
                    return object(depth);
                default:
                    if (peek() == '-' || is_digit(peek()))
                        return number();
                    fail(std::string("unexpected character '") + peek() + "'");
                    return Value{};
                }
            }

            // 严格按 JSON 数字语法读，"1." / "1e" / "-" 这类半截数字都算错
            Value number()
            {
                const size_t start = p;
                if (!at_end() && peek() == '-')
                    ++p;
                if (at_end() || !is_digit(peek()))
                {
                    fail("number has no integer part");
                    return Value{};
                }
                while (!at_end() && is_digit(peek()))
                    ++p;
                if (!at_end() && peek() == '.')
                {
                    ++p;
                    if (at_end() || !is_digit(peek()))
                    {
                        fail("number has no digits after '.'");
                        return Value{};
                    }
                    while (!at_end() && is_digit(peek()))
                        ++p;
                }
                if (!at_end() && (peek() == 'e' || peek() == 'E'))
                {
                    ++p;
                    if (!at_end() && (peek() == '+' || peek() == '-'))
                        ++p;
                    if (at_end() || !is_digit(peek()))
                    {
                        fail("number has no digits in exponent");
                        return Value{};
                    }
                    while (!at_end() && is_digit(peek()))
                        ++p;
                }

                double parsed = 0;
                // 复用 KeelValue 里的 fast_atof（from_chars / strtod 两条路），不重复造
                if (!fast_atof(s.substr(start, p - start), parsed))
                {
                    fail("number out of range or malformed");
                    return Value{};
                }
                return make_number(parsed);
            }

            static char hex_digit(char c)
            {
                if (c >= '0' && c <= '9')
                    return static_cast<char>(c - '0');
                if (c >= 'a' && c <= 'f')
                    return static_cast<char>(c - 'a' + 10);
                if (c >= 'A' && c <= 'F')
                    return static_cast<char>(c - 'A' + 10);
                return static_cast<char>(-1);
            }

            unsigned int hex4()
            {
                if (p + 4 > s.size())
                {
                    fail("\\u needs 4 hex digits");
                    return 0;
                }
                unsigned int v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char d = hex_digit(s[p++]);
                    if (d < 0)
                    {
                        fail("\\u is not followed by hex digits");
                        return 0;
                    }
                    v = (v << 4) | static_cast<unsigned int>(d);
                }
                return v;
            }

            // 读一个 \uXXXX；高代理紧跟低代理时合并成一个码点。
            // 落单的代理编不出合法 UTF-8，统一换 U+FFFD。
            unsigned int code_point()
            {
                const unsigned int high = hex4();
                if (bad)
                    return 0;
                if (high >= 0xD800 && high <= 0xDBFF)
                {
                    const size_t save = p;
                    if (p + 1 < s.size() && s[p] == '\\' && s[p + 1] == 'u')
                    {
                        p += 2;
                        const unsigned int low = hex4();
                        if (!bad && low >= 0xDC00 && low <= 0xDFFF)
                            return 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
                        p = save; // 后面那个不是低代理：退回去让它自己解析
                    }
                    return 0xFFFD;
                }
                if (high >= 0xDC00 && high <= 0xDFFF)
                    return 0xFFFD;
                return high;
            }

            static void utf8_append(std::string &out, unsigned int cp)
            {
                if (cp <= 0x7F)
                {
                    out += static_cast<char>(cp);
                }
                else if (cp <= 0x7FF)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp <= 0xFFFF)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            bool string_lit(std::string &out)
            {
                if (!expect('"'))
                    return false;
                while (true)
                {
                    if (at_end())
                    {
                        fail("unterminated string");
                        return false;
                    }
                    const unsigned char c = static_cast<unsigned char>(s[p++]);
                    if (c == '"')
                        return true;
                    if (c != '\\')
                    {
                        if (c < 0x20)
                        {
                            fail("unescaped control character in string");
                            return false;
                        }
                        out += static_cast<char>(c);
                        continue;
                    }
                    if (at_end())
                    {
                        fail("unterminated escape");
                        return false;
                    }
                    const char e = s[p++];
                    switch (e)
                    {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                        utf8_append(out, code_point());
                        if (bad)
                            return false;
                        break;
                    default:
                        fail(std::string("unsupported escape \\") + e);
                        return false;
                    }
                }
            }

            Value array(int depth)
            {
                Value out = make_list();
                ++p; // '['
                skip_ws();
                if (!at_end() && peek() == ']')
                {
                    ++p;
                    return out;
                }
                while (true)
                {
                    skip_ws();
                    Value item = value(depth + 1);
                    if (bad)
                        return Value{};
                    out.list->push_back(std::move(item));
                    skip_ws();
                    if (at_end())
                    {
                        fail("unterminated array");
                        return Value{};
                    }
                    if (peek() == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (peek() == ']')
                    {
                        ++p;
                        return out;
                    }
                    fail("expected ',' or ']'");
                    return Value{};
                }
            }

            Value object(int depth)
            {
                Value out = make_map();
                ++p; // '{'
                skip_ws();
                if (!at_end() && peek() == '}')
                {
                    ++p;
                    return out;
                }
                while (true)
                {
                    skip_ws();
                    if (at_end() || peek() != '"')
                    {
                        fail("object key must be a string");
                        return Value{};
                    }
                    std::string key;
                    if (!string_lit(key))
                        return Value{};
                    skip_ws();
                    if (!expect(':'))
                        return Value{};
                    skip_ws();
                    Value item = value(depth + 1);
                    if (bad)
                        return Value{};
                    (*out.map)[std::move(key)] = std::move(item);
                    skip_ws();
                    if (at_end())
                    {
                        fail("unterminated object");
                        return Value{};
                    }
                    if (peek() == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (peek() == '}')
                    {
                        ++p;
                        return out;
                    }
                    fail("expected ',' or '}'");
                    return Value{};
                }
            }
        };
    } // namespace json_detail

    // -------------------------------------------------------------------
    // 编码：Value -> JSON 文本
    //   pretty = true 时缩进 2 空格、一个成员一行，方便往日志里贴
    // -------------------------------------------------------------------
    inline std::string json_encode(const Value &v, bool pretty = false)
    {
        std::string out;
        out.reserve(64);
        json_detail::write_value(out, v, pretty, 2, 0);
        return out;
    }

    // -------------------------------------------------------------------
    // 解码：JSON 文本 -> Value
    //   成功返回 true；失败返回 false，out 置 nil，原因已 report
    // -------------------------------------------------------------------
    inline bool json_decode(std::string_view text, Value &out)
    {
        json_detail::Reader r(text);
        r.skip_ws();
        Value v = r.value(0);
        if (!r.bad)
        {
            r.skip_ws();
            if (!r.at_end())
                r.fail("trailing garbage after JSON value");
        }
        if (r.bad)
        {
            out = Value{};
            return false;
        }
        out = std::move(v);
        return true;
    }
} // namespace keel
