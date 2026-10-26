#pragma once
// ============================================================================
//  KeelValue.hpp -- 值类型与错误容器（整棵依赖树的根）
// ----------------------------------------------------------------------------
//  * keel::Value  ：一个值可以是 nil / bool / number / string / list / map /
//                   unordered_map，全部塞在同一个 struct 里（字段全在，按 type 取用）。
//                   列表与映射用 shared_ptr 持有，**拷贝是共享的**，要独立请自己深拷。
//  * 数值解析    ：fast_atof 优先 std::from_chars，老编译器回退 std::strtod。
//  * 错误        ：Error 对象负责文案，keel_errors() 是当前这轮的错误集合；
//                   出错位置（块名 + ip）由 Keel.hpp 在执行前设置，Error::report()
//                   取它来标明"哪一行"。一轮跑完要 clear_errors()。
//
//  本文件不 include 任何其他 Keel 头。
// ============================================================================

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream> // std::ostringstream
#include <utility> // std::move, std::pair
#include <cstdio>  // std::fprintf

#include <charconv> // std::from_chars
#include <string_view>
#include <cstddef>  // std::size_t
#include <cstdlib> // std::strtod (作为老编译器的 fallback)

namespace keel
{
    inline bool fast_atof(std::string_view str, double &out)
    {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        // C++17 现代编译器（GCC 11+, Clang 14+, MSVC 2019+）
        auto result = std::from_chars(str.data(), str.data() + str.size(), out);
        return result.ec == std::errc() && result.ptr == str.data() + str.size();
#else
        std::string tmp(str);
        char *end = nullptr;
        out = std::strtod(tmp.c_str(), &end);
        return end == tmp.c_str() + tmp.size() && !tmp.empty();
#endif
    }
    inline bool fast_atob(std::string_view str)
    {
        return str == "true" || str == "1" || str == "True" || str == "TRUE";
    }
}

namespace keel
{
    class Error;
    inline std::vector<keel::Error> &keel_errors()
    {
        static std::vector<keel::Error> errors;
        return errors;
    }
    inline bool &keel_errors_to_stderr()
    {
        static bool enabled = false;
        return enabled;
    }

    // 容器里只保留最近这么多条，防止长会话里无限增长；累计条数看 keel_error_total()
    constexpr std::size_t kMaxErrors = 100;

    // 进程内累计报过多少条错（只增，clear_errors() 不归零）
    inline std::size_t &keel_error_total()
    {
        static std::size_t total = 0;
        return total;
    }

    // 清空错误容器：agent 每跑一轮之前调一次，"本轮的错"就不会和上一轮混在一起
    inline void clear_errors()
    {
        keel_errors().clear();
    }

    // ---------------------------------------------------------------- 出错位置
    // 引擎在派发每条指令前记一下"当前块 + token 下标"，Error::report() 入表时
    // 把它填进 where_/token_。这里存指针不拷贝：热路径上不能每条指令复制一个字符串。
    // 和 keel_errors() 一样是进程级状态（单线程假设）。
    struct ErrorPos
    {
        const std::string *block = nullptr;   // 指向 KeelData::current_name
        int token = -1;
    };

    inline ErrorPos &keel_error_pos()
    {
        static ErrorPos pos;
        return pos;
    }

    inline void set_error_pos(const std::string *block, int token)
    {
        keel_error_pos().block = block;
        keel_error_pos().token = token;
    }

    inline void clear_error_pos()
    {
        keel_error_pos().block = nullptr;
        keel_error_pos().token = -1;
    }

    using AspErrorHandler = void (*)();

    inline AspErrorHandler &keel_error_handler() noexcept
    {
        static AspErrorHandler handler = nullptr;
        return handler;
    }

    inline void set_keel_error_handler(AspErrorHandler h) noexcept
    {
        keel_error_handler() = h;
    }

    inline void report_error(const std::string &msg)
    {
        if (AspErrorHandler h = keel_error_handler())
        {
            h();
            return;
        }
        std::fprintf(stderr, "[keel::error] %s\n", msg.c_str());
    }
    class Error
    {
    private:
        std::string error_;
        std::string word_;
        std::string where_;   // 出错的代码块名（引擎填，可能为空）
        int index_ = -1;      // 自带位置：JSON 解码的字节偏移等；u::err 不设置
        int token_ = -1;      // 出错的 token 下标（引擎填，-1 = 不知道）
        int code_ = 0;

    public:
        Error(std::string s, int c = 0)
            : error_(std::move(s)), code_(c) {}
        Error(std::string s, std::string w, int c = 0)
            : error_(std::move(s)), word_(std::move(w)), code_(c) {}

        static Error with_index(std::string s, int i, int c = 0)
        {
            Error e(std::move(s), c);
            e.index_ = i;
            return e;
        }
        static Error with_word_index(std::string s, std::string w, int i, int c = 0)
        {
            Error e(std::move(s), std::move(w), c);
            e.index_ = i;
            return e;
        }

        const std::string &what() const { return error_; }
        const std::string &word() const { return word_; }
        const std::string &where() const { return where_; }
        int index() const { return index_; }
        int token() const { return token_; }
        int code() const { return code_; }

        // "块名:下标"，没有位置信息时给空串
        std::string position_text() const
        {
            if (where_.empty() || token_ < 0)
                return std::string();
            return where_ + ":" + std::to_string(token_);
        }

        void report() const
        {
            ++keel_error_total();

            // 自己没带位置就补上引擎记的（JSON 解码这种自带位置的不动）
            Error rec = *this;
            const ErrorPos &pos = keel_error_pos();
            if (rec.token_ < 0 && rec.index_ < 0 && pos.block && pos.token >= 0)
            {
                rec.where_ = *pos.block;
                rec.token_ = pos.token;
            }

            // 注意：先打印再 move 进容器，否则 rec 的字符串已经被搬空
            if (keel_errors_to_stderr())
            {
                const std::string pos_text = rec.position_text();
                const std::string head = pos_text.empty() ? std::string() : pos_text + ": ";
                std::fprintf(stderr, "[keel::error] %s%s\n", head.c_str(), rec.error_.c_str());
            }

            std::vector<Error> &all = keel_errors();
            if (all.size() >= kMaxErrors)
                all.erase(all.begin());   // 只保留最近的 kMaxErrors 条
            all.push_back(std::move(rec));
        }
    };
    // ---------------------------------------------------------------- 错误格式化
    // 把 Error 列表拼成给 AI 看的文本。宿主在 run() 结束后调用：
    //     std::string feedback = keel::format_errors_for_ai(keel::keel_errors());
    //
    // 规则：
    //   * 同 (块, ip, 字节偏移, what) 的错误聚合成一条，带 "(xN)" 计数
    //     —— 死循环里同一条报 100 次，AI 只会看到一行 "(x100)"。
    //   * 最多列 max_unique 条不同错误，剩下的折成 "... (K more unique errors)"。
    //   * 有位置的行格式： "块:ip (xN) 指令: 描述"
    //     无位置的行（如 JSON 解码错）： "(xN) 描述"
    inline std::string format_errors_for_ai(const std::vector<Error> &errors,
                                            std::size_t max_unique = 5)
    {
        if (errors.empty())
            return std::string();

        struct Agg
        {
            const Error *err;
            std::size_t count;
        };
        std::vector<Agg> aggs;
        aggs.reserve(errors.size());
        std::unordered_map<std::string, std::size_t> key_to_index;

        for (const auto &e : errors)
        {
            // token 和 index 都进 key：JSON 错（token=-1、index>=0）不会和
            // 普通指令错（token>=0、index=-1）互相聚合。
            std::string key = e.where();
            key += '|';
            key += std::to_string(e.token());
            key += ':';
            key += std::to_string(e.index());
            key += '|';
            key += e.what();

            auto it = key_to_index.find(key);
            if (it != key_to_index.end())
                ++aggs[it->second].count;
            else
            {
                key_to_index.emplace(std::move(key), aggs.size());
                aggs.push_back(Agg{&e, 1});
            }
        }

        std::string out;
        out.reserve(256);
        std::size_t emitted = 0;
        for (const auto &a : aggs)
        {
            if (emitted >= max_unique)
            {
                out += "... (";
                out += std::to_string(aggs.size() - emitted);
                out += " more unique errors)\n";
                break;
            }

            const std::string pos = a.err->position_text();
            if (!pos.empty())
            {
                out += pos;
                out += ' ';
            }
            if (a.count > 1)
            {
                out += "(x";
                out += std::to_string(a.count);
                out += ") ";
            }
            out += a.err->what();
            out += '\n';
            ++emitted;
        }
        return out;
    }
    enum class Type
    {
        Nil,
        Bool,
        Number,
        String,
        List,
        Map,
        UMap
    };

    inline std::string value_type(Type t)
    {
        switch (t)
        {
        case Type::Nil:
            return "nil";
        case Type::Bool:
            return "bool";
        case Type::Number:
            return "number";
        case Type::String:
            return "string";
        case Type::List:
            return "list";
        case Type::Map:
            return "map";
        case Type::UMap:
            return "unordered_map";
        }
        return "[?type]";
    }

    struct Value
    {
        Type type = Type::Nil;
        bool b = false;
        double n = 0;
        std::string s;
        std::shared_ptr<std::vector<Value>> list;
        std::shared_ptr<std::map<std::string, Value>> map;
        std::shared_ptr<std::unordered_map<std::string, Value>> unmap;

        Value() = default;

        template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>>>
        Value(T &&v)
        {
            using D = std::decay_t<T>;
            if constexpr (std::is_same_v<D, bool>)
            {
                type = Type::Bool;
                b = v;
            }
            else if constexpr (std::is_arithmetic_v<D>)
            {
                type = Type::Number;
                n = static_cast<double>(v);
            }
            else if constexpr (std::is_same_v<D, std::string>)
            {
                type = Type::String;
                s = std::forward<T>(v);
            }
            else if constexpr (std::is_same_v<D, const char *> || std::is_same_v<D, char *>)
            {
                type = Type::String;
                s = v;
            }
            else
            {
                static_assert(!sizeof(T), "Unsupported type for keel::Value");
            }
        }
        template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>>>
        Value &operator=(T &&v)
        {
            *this = Value(std::forward<T>(v));
            return *this;
        }

        bool isNil() const { return type == Type::Nil; }
        bool isBool() const { return type == Type::Bool; }
        bool isNumber() const { return type == Type::Number; }
        bool isString() const { return type == Type::String; }
        bool isList() const { return type == Type::List; }
        bool isMap() const { return type == Type::Map; }
        bool isUMap() const { return type == Type::UMap; }
        bool isContainer() const { return isList() || isMap() || isUMap(); }

        bool truthy() const
        {
            switch (type)
            {
            case Type::Nil:
                return false;
            case Type::Bool:
                return b;
            case Type::String:
                return !s.empty();
            case Type::Number:
                return n != 0;
            case Type::List:
                return list && !list->empty();
            case Type::Map:
                return map && !map->empty();
            case Type::UMap:
                return unmap && !unmap->empty();
            default:
                return false;
            }
        }

        double asNumber() const
        {
            if (type == Type::Number)
                return n;
            if (type == Type::Bool)
                return b ? 1.0 : 0.0;
            if (type == Type::String)
            {
                double parsed;
                if (fast_atof(s, parsed))
                    return parsed;
            }
            Error("expected number, got " + value_type(type)).report();
            return 0;
        }

        bool asBool() const
        {
            if (type == Type::Bool)
                return b;
            if (type == Type::Number)
                return n != 0;
            if (type == Type::String)
            {
                if (s.empty() || s == "false" || s == "0")
                    return false;
                if (s == "true" || s == "1")
                    return true;
                double parsed;
                if (fast_atof(s, parsed))
                    return parsed != 0;
            }
            Error("expected bool, got " + value_type(type)).report();
            return false;
        }
        const std::string &asString() const
        {
            if (type != Type::String)
            {
                Error("expected string, got " + value_type(type)).report();
                static const std::string empty;
                return empty;
            }
            return s;
        }
        const std::vector<Value> &asList() const
        {
            if (type != Type::List || !list)
            {
                Error("expected list, got " + value_type(type)).report();
                static const std::vector<Value> empty;
                return empty;
            }
            return *list;
        }
        std::vector<Value> &asList()
        {
            if (type != Type::List || !list)
            {
                Error("expected list, got " + value_type(type)).report();
                static std::vector<Value> empty;
                empty.clear();
                return empty;
            }
            return *list;
        }

        const std::map<std::string, Value> &asMap() const
        {
            if (type != Type::Map || !map)
            {
                Error("expected map, got " + value_type(type)).report();
                static const std::map<std::string, Value> empty;
                return empty;
            }
            return *map;
        }
        std::map<std::string, Value> &asMap()
        {
            if (type != Type::Map || !map)
            {
                Error("expected map, got " + value_type(type)).report();
                static std::map<std::string, Value> empty;
                empty.clear();
                return empty;
            }
            return *map;
        }

        const std::unordered_map<std::string, Value> &asUMap() const
        {
            if (type != Type::UMap || !unmap)
            {
                Error("expected unordered_map, got " + value_type(type)).report();
                static const std::unordered_map<std::string, Value> empty;
                return empty;
            }
            return *unmap;
        }
        std::unordered_map<std::string, Value> &asUMap()
        {
            if (type != Type::UMap || !unmap)
            {
                Error("expected unordered_map, got " + value_type(type)).report();
                static std::unordered_map<std::string, Value> empty;
                empty.clear();
                return empty;
            }
            return *unmap;
        }

        size_t size() const
        {
            switch (type)
            {
            case Type::String:
                return s.size();
            case Type::List:
                return list ? list->size() : 0;
            case Type::Map:
                return map ? map->size() : 0;
            case Type::UMap:
                return unmap ? unmap->size() : 0;
            default:
                return 0;
            }
        }

        Value at(size_t i) const
        {
            if (type != Type::List)
            {
                Error("at() on non-list: " + value_type(type)).report();
                return Value{};
            }
            if (!list || i >= list->size())
            {
                Error("index out of range").report();
                return Value{};
            }
            return (*list)[i];
        }

        Value get(const std::string &key) const
        {
            if (type == Type::Map)
            {
                if (!map)
                {
                    Error("map is null").report();
                    return Value{};
                }
                auto it = map->find(key);
                return it == map->end() ? Value{} : it->second;
            }
            if (type == Type::UMap)
            {
                if (!unmap)
                {
                    Error("unordered_map is null").report();
                    return Value{};
                }
                auto it = unmap->find(key);
                return it == unmap->end() ? Value{} : it->second;
            }
            Error("get() on non-map: " + value_type(type)).report();
            return Value{};
        }

        bool contains(const std::string &key) const
        {
            if (type == Type::Map)
                return map && map->count(key);
            if (type == Type::UMap)
                return unmap && unmap->count(key);
            return false;
        }

        void push(Value v)
        {
            if (type != Type::List)
            {
                Error("push() on non-list: " + value_type(type)).report();
                return;
            }
            if (!list)
                list = std::make_shared<std::vector<Value>>();
            list->push_back(std::move(v));
        }

        void set(const std::string &key, Value v)
        {
            if (type == Type::Map)
            {
                if (!map)
                    map = std::make_shared<std::map<std::string, Value>>();
                (*map)[key] = std::move(v);
                return;
            }
            if (type == Type::UMap)
            {
                if (!unmap)
                    unmap = std::make_shared<std::unordered_map<std::string, Value>>();
                (*unmap)[key] = std::move(v);
                return;
            }
            Error("set() on non-map: " + value_type(type)).report();
        }

        std::string toString() const
        {
            switch (type)
            {
            case Type::Nil:
                return "nil";
            case Type::Bool:
                return b ? "true" : "false";
            case Type::Number:
            {
                std::ostringstream os;
                os << n;
                return os.str();
            }
            case Type::String:
                return s;
            case Type::List:
            {
                std::string r = "[";
                if (list)
                    for (size_t i = 0; i < list->size(); ++i)
                    {
                        if (i)
                            r += ", ";
                        r += (*list)[i].toString();
                    }
                return r + "]";
            }
            case Type::Map:
            {
                std::string r = "{";
                bool first = true;
                if (map)
                    for (auto &[k, v] : *map)
                    {
                        if (!first)
                            r += ", ";
                        first = false;
                        r += "\"" + k + "\": " + v.toString();
                    }
                return r + "}";
            }
            case Type::UMap:
            {
                std::string r = "{";
                bool first = true;
                if (unmap)
                    for (auto &[k, v] : *unmap)
                    {
                        if (!first)
                            r += ", ";
                        first = false;
                        r += "\"" + k + "\": " + v.toString();
                    }
                return r + "}";
            }
            default:
                return "nil";
            }
        }
    };

    inline Value make_nil() { return Value{}; }
    inline Value make_bool(bool v)
    {
        Value r;
        r.type = Type::Bool;
        r.b = v;
        return r;
    }
    inline Value make_number(double v)
    {
        Value r;
        r.type = Type::Number;
        r.n = v;
        return r;
    }
    inline Value make_string(std::string v)
    {
        Value r;
        r.type = Type::String;
        r.s = std::move(v);
        return r;
    }

    inline Value make_list()
    {
        Value v;
        v.type = Type::List;
        v.list = std::make_shared<std::vector<Value>>();
        return v;
    }
    inline Value make_map()
    {
        Value v;
        v.type = Type::Map;
        v.map = std::make_shared<std::map<std::string, Value>>();
        return v;
    }
    inline Value make_umap()
    {
        Value v;
        v.type = Type::UMap;
        v.unmap = std::make_shared<std::unordered_map<std::string, Value>>();
        return v;
    }
    inline Value parse_value(std::string str)
    {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return make_string(std::move(str));
        }
        size_t end = str.find_last_not_of(" \t\r\n");
        std::string_view sv(str.data() + start, end - start + 1);
        if (sv == "nil" || sv == "null")
        {
            return make_nil();
        }
        if (sv == "true")
            return make_bool(true);
        if (sv == "false")
            return make_bool(false);
        double parsed_num;
        if (fast_atof(sv, parsed_num))
        {
            return make_number(parsed_num);
        }
        if (sv.size() >= 2)
        {
            char first = sv.front();
            char last = sv.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
            {
                return make_string(std::string(sv.substr(1, sv.size() - 2)));
            }
        }
        return make_string(std::move(str));
    }
}