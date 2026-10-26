#pragma once
// ============================================================================
// KeelUtil.hpp -- 内部公共工具（不注册任何指令）
// 所有 Keel 内置函数库共用的取值/报错/索引小工具，统一放这里，
// 免得每个库各写一份。只依赖 Keel.hpp。
// ============================================================================
#include "Keel.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace keel
{
    namespace util
    {
        // ---------------------------------------------------------------- 报错
        // 统一格式："指令名: 说明"，走 keel::Error 收集 + 可选 stderr
        inline void err(const char *name, const std::string &msg)
        {
            keel::Error(std::string(name) + ": " + msg, name).report();
        }
        // 单次分配上限：bytes 超过 d.max_alloc 时报错并返回 false
        inline bool check_alloc(KeelData &d, size_t bytes, const char *name)
        {
            if (bytes <= d.max_alloc)
                return true;
            err(name, "allocation too large (" + std::to_string(bytes) +
                          " bytes > " + std::to_string(d.max_alloc) + ")");
            return false;
        }

        // ---------------------------------------------------------------- 沙箱
        // UTF-8 字符串 <-> std::filesystem::path：C++17 用 u8path，C++20 用 u8string。
        inline std::filesystem::path fs_path(const std::string &s)
        {
#if defined(__cpp_char8_t)
            return std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
#else
            return std::filesystem::u8path(s);
#endif
        }

        inline std::string path_to_utf8(const std::filesystem::path &p)
        {
#if defined(__cpp_char8_t)
            const std::u8string u = p.u8string();
            return std::string(reinterpret_cast<const char *>(u.data()), u.size());
#else
            return p.u8string();
#endif
        }

        inline std::string lower_ascii(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        /// 规范化成绝对、无 “..” 的路径（装载时调用一次，存进 KeelData::sandbox_root）。
        inline std::string normalize_path(const std::string &p)
        {
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(fs_path(p), ec);
            if (ec)
                abs = fs_path(p);
            std::filesystem::path canon = std::filesystem::weakly_canonical(abs, ec);
            if (ec)
                canon = abs.lexically_normal();
            return path_to_utf8(canon);
        }

        /// path 是否在 d.sandbox_root 里（按路径组件比较，Windows 不区分大小写）。
        /// d.sandbox_root 为空 = 不限制。
        inline bool in_sandbox(const KeelData &d, const std::string &path)
        {
            if (d.sandbox_root.empty())
                return true;

            std::filesystem::path root = fs_path(d.sandbox_root);
            if (!root.is_absolute())
                root = std::filesystem::absolute(root).lexically_normal();

            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(fs_path(path), ec);
            if (ec)
                abs = fs_path(path);
            std::filesystem::path p = std::filesystem::weakly_canonical(abs, ec);
            if (ec)
                p = abs.lexically_normal();

            auto r = root.begin();
            auto q = p.begin();
            for (; r != root.end(); ++r, ++q)
            {
                if (q == p.end())
                    return false;
                if (lower_ascii(path_to_utf8(*r)) != lower_ascii(path_to_utf8(*q)))
                    return false;
            }
            return true;
        }

        /// 不通过时报错，返回 false；调用方负责按原失败语义压回 nil/false/空 map。
        inline bool check_sandbox(const KeelData &d, const std::string &path, const char *name)
        {
            if (in_sandbox(d, path))
                return true;
            err(name, "path escapes sandbox: " + path);
            return false;
        }
        // ------------------------------------------------------------ 栈操作
        inline keel::Value pop(KeelData &d)
        {
            keel::Value v = std::move(d.stack.back());
            d.stack.pop_back();
            return v;
        }
        inline void push(KeelData &d, keel::Value v) { d.stack.push_back(std::move(v)); }

        // 栈上至少有 n 个操作数才继续
        inline bool need(KeelData &d, size_t n, const char *name)
        {
            if (d.stack.size() >= n)
                return true;
            err(name, "stack underflow: need " + std::to_string(n) +
                          " operand(s), got " + std::to_string(d.stack.size()));
            return false;
        }

        // ------------------------------------------------------------ 取值
        // 数值化：Number / Bool / 可解析为数字的 String 都接受
        inline bool num(const keel::Value &v, double &out)
        {
            switch (v.type)
            {
            case keel::Type::Number:
                out = v.n;
                return true;
            case keel::Type::Bool:
                out = v.b ? 1.0 : 0.0;
                return true;
            case keel::Type::String:
                return keel::fast_atof(v.s, out);
            default:
                return false;
            }
        }

        inline bool pop_num(KeelData &d, const char *name, double &out)
        {
            if (!need(d, 1, name))
                return false;
            keel::Value v = pop(d);
            if (num(v, out))
                return true;
            err(name, std::string("expected number, got ") + keel::value_type(v.type));
            push(d, keel::make_nil()); // 保持栈深度，出错后程序还能往下跑
            return false;
        }

        // 严格字符串：只有 String 能过
        inline bool pop_str(KeelData &d, const char *name, std::string &out)
        {
            if (!need(d, 1, name))
                return false;
            keel::Value v = pop(d);
            if (v.isString())
            {
                out = std::move(v.s);
                return true;
            }
            err(name, std::string("expected string, got ") + keel::value_type(v.type));
            push(d, keel::make_nil());
            return false;
        }

        // 宽松文本：任何值都用 toString() 变字符串（写脚本时省事）
        inline bool pop_text(KeelData &d, const char *name, std::string &out)
        {
            if (!need(d, 1, name))
                return false;
            keel::Value v = pop(d);
            out = v.toString();
            return true;
        }

        inline bool pop_bool(KeelData &d, const char *name, bool &out)
        {
            if (!need(d, 1, name))
                return false;
            out = pop(d).truthy();
            return true;
        }

        // -------------------------------------------------------- 非破坏性取值
        // 用法：need(n) 保证够深 → peek_* 只校验不弹 → 一次性 drop_n(n) → push 结果 / nil。
        // 这样"出错时消耗掉的操作数个数"和成功时完全一样，不会出现
        // "弹了两个才报错、第一个还留在栈上"的半截状态。
        inline const keel::Value &peek(const KeelData &d, size_t k)
        {
            return d.stack[d.stack.size() - 1 - k]; // k = 0 是栈顶
        }

        // 无条件弹掉栈顶 n 个（调用前用 need 保证够深）
        inline void drop_n(KeelData &d, size_t n)
        {
            d.stack.resize(d.stack.size() - n);
        }

        inline bool peek_num(const KeelData &d, size_t k, const char *name, double &out)
        {
            const keel::Value &v = peek(d, k);
            if (num(v, out))
                return true;
            err(name, "operand " + std::to_string(k + 1) + " from top: expected number, got " +
                          keel::value_type(v.type));
            return false;
        }

        inline bool peek_str(const KeelData &d, size_t k, const char *name, std::string &out)
        {
            const keel::Value &v = peek(d, k);
            if (v.isString())
            {
                out = v.s;
                return true;
            }
            err(name, "operand " + std::to_string(k + 1) + " from top: expected string, got " +
                          keel::value_type(v.type));
            return false;
        }

        inline bool peek_list(const KeelData &d, size_t k, const char *name, keel::Value &out)
        {
            const keel::Value &v = peek(d, k);
            if (v.isList())
            {
                out = v; // 只拷 shared_ptr，指向的还是同一份 list
                return true;
            }
            err(name, "operand " + std::to_string(k + 1) + " from top: expected list, got " +
                          keel::value_type(v.type));
            return false;
        }

        // --------------------------------------------------------- 数值小工具
        // 截断取整（不引 <cmath>，保持核心库只依赖 Keel.hpp）
        inline double trunc_d(double x)
        {
            if (!(x == x)) // NaN
                return x;
            if (x >= 9223372036854775807.0 || x <= -9223372036854775808.0)
                return x;
            return x < 0 ? -static_cast<double>(static_cast<long long>(-x))
                         : static_cast<double>(static_cast<long long>(x));
        }

        inline int to_int(double x)
        {
            if (!(x == x))
                return 0;
            if (x >= 2147483647.0)
                return 2147483647;
            if (x <= -2147483648.0)
                return -2147483648;
            return static_cast<int>(x);
        }

        // ------------------------------------------------------------ 运行环境
        // 当前正在执行的代码块名（用于校验循环记录是否跨块）
        inline std::string cur_block(const KeelData &d)
        {
            if (d.cache_code_ptr.empty())
                return std::string();
            return d.cache_code_ptr.back();
        }

        // 当前块的第 i 条 token 是否等于 s（越界安全）
        inline bool tok_is(const KeelData &d, int i, const char *s)
        {
            return d.current_block && i >= 0 &&
                   i < static_cast<int>(d.current_block->size()) &&
                   (*d.current_block)[i] == s;
        }
    }
}
