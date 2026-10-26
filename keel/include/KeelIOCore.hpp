#pragma once
// ============================================================================
//  KeelIOCore.hpp —— 新 IO 库（FastIO / BigIO / DynSource）共用的底层小工具
// ----------------------------------------------------------------------------
//  只放"三个库都要用、但谁也不该独占"的东西：
//    * UTF-8 路径 <-> fs::path（C++20 的 u8path 已废弃，这里统一处理）
//    * 数字 -> 文本：std::to_chars，精度 6，和 iostream 默认输出一致
//    * 文件签名（存在 / 大小 / 修改时间）：DynSource 判断"文件变了没"
//    * 原生文件句柄的 6 个基元：open / read / write / seek / size / flush / close
//      Windows 走 CreateFileW + ReadFile/WriteFile；其它平台退回 FILE*（能用，未重点验证）
//    * 控制台输出：Windows 控制台句柄要 UTF-8 -> UTF-16 走 WriteConsoleW，
//      否则中文是乱码；重定向到管道/文件时直接 WriteFile 字节
//
//  这一层**不包含**任何 Keel 指令，也没有 iostream。
// ============================================================================

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace keelio_impl
{
    namespace fs = std::filesystem;

    // ------------------------------------------------------------------ 文本
    inline fs::path from_utf8(std::string_view s)
    {
        // 用 UTF-8 解释，别让中文路径按 ANSI 解码
#if defined(__cpp_lib_char8_t) && __cpp_lib_char8_t >= 201811L
        return fs::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
#else
        return fs::u8path(s.begin(), s.end());
#endif
    }

    inline std::string to_utf8(const fs::path &p)
    {
#if defined(__cpp_lib_char8_t) && __cpp_lib_char8_t >= 201811L
        const std::u8string u8 = p.u8string();
        return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
#else
        return p.u8string();
#endif
    }

    /// 数字 -> 文本。精度 6、general 格式：和 `std::cout << double` 的默认结果一致，
    /// 这样脚本里混用 println（走 ostream）和 f.outln（走这里）不会看到两种写法。
    inline void append_number(std::string &out, double v)
    {
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general, 6);
        if (r.ec == std::errc())
            out.append(buf, r.ptr);
        else
            out += "nan";
    }

    /// FNV-1a 64：给"这段源码的版本"用，不追求密码学强度，只要稳定、便宜
    inline std::uint64_t fnv1a64(std::string_view s)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    // ------------------------------------------------------------------ 文件签名
    struct FileSig
    {
        bool exists = false;
        bool is_dir = false;
        std::uint64_t size = 0;
        std::int64_t mtime = 0; // 只用于比较相等，单位随平台（Windows 是 100ns）
    };

    inline FileSig file_sig(const fs::path &p)
    {
        FileSig s;
        std::error_code ec;
        const fs::file_status st = fs::status(p, ec);
        if (ec || !fs::exists(st))
            return s;

        s.exists = true;
        s.is_dir = fs::is_directory(st);
        if (!s.is_dir)
        {
            const auto sz = fs::file_size(p, ec);
            if (!ec)
                s.size = static_cast<std::uint64_t>(sz);
        }
        const auto t = fs::last_write_time(p, ec);
        if (!ec)
            s.mtime = static_cast<std::int64_t>(t.time_since_epoch().count());
        return s;
    }

    // ------------------------------------------------------------------ 原生句柄
#if defined(_WIN32)

    using native_handle = HANDLE;

    inline native_handle invalid_handle() { return INVALID_HANDLE_VALUE; }
    inline bool valid(native_handle h) { return h != nullptr && h != INVALID_HANDLE_VALUE; }

    enum class IoMode
    {
        Read,
        Write,
        Append,
        ReadWrite
    };

    inline std::string last_error_text()
    {
        return std::system_category().message(static_cast<int>(::GetLastError()));
    }

    inline bool wide_path(const std::string &utf8, std::wstring &out)
    {
        if (utf8.empty())
            return false;
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                            nullptr, 0);
        if (n <= 0)
            return false;
        out.resize(static_cast<std::size_t>(n));
        return ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                     out.data(), n) == n;
    }

    inline native_handle io_open(const std::string &utf8, IoMode mode, std::string &err)
    {
        std::wstring w;
        if (!wide_path(utf8, w))
        {
            err = "路径不是合法 UTF-8";
            return invalid_handle();
        }
        DWORD access = 0;
        DWORD disposition = 0;
        switch (mode)
        {
        case IoMode::Read:
            access = GENERIC_READ;
            disposition = OPEN_EXISTING;
            break;
        case IoMode::Write:
            access = GENERIC_WRITE;
            disposition = CREATE_ALWAYS;
            break;
        case IoMode::Append:
            access = GENERIC_WRITE;
            disposition = OPEN_ALWAYS;
            break;
        case IoMode::ReadWrite:
            access = GENERIC_READ | GENERIC_WRITE;
            disposition = OPEN_ALWAYS;
            break;
        }
        HANDLE h = ::CreateFileW(w.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            err = last_error_text();
            return invalid_handle();
        }
        if (mode == IoMode::Append)
        {
            LARGE_INTEGER zero{};
            ::SetFilePointerEx(h, zero, nullptr, FILE_END);
        }
        return h;
    }

    inline bool io_close(native_handle h)
    {
        return valid(h) && ::CloseHandle(h) != 0;
    }

    inline bool io_write(native_handle h, const char *data, std::size_t n)
    {
        while (n > 0)
        {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(n, 1u << 20));
            DWORD written = 0;
            if (!::WriteFile(h, data, chunk, &written, nullptr) || written == 0)
                return false;
            data += written;
            n -= written;
        }
        return true;
    }

    inline bool io_read(native_handle h, char *data, std::size_t n, std::size_t &got)
    {
        got = 0;
        while (got < n)
        {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(n - got, 1u << 20));
            DWORD read = 0;
            if (!::ReadFile(h, data + got, chunk, &read, nullptr))
                return false;
            got += read;
            if (read < chunk) // EOF
                break;
        }
        return true;
    }

    inline bool io_seek(native_handle h, std::uint64_t pos, std::uint64_t &new_pos)
    {
        LARGE_INTEGER in{};
        in.QuadPart = static_cast<LONGLONG>(pos);
        LARGE_INTEGER out{};
        if (!::SetFilePointerEx(h, in, &out, FILE_BEGIN))
            return false;
        new_pos = static_cast<std::uint64_t>(out.QuadPart);
        return true;
    }

    inline bool io_tell(native_handle h, std::uint64_t &pos)
    {
        LARGE_INTEGER zero{};
        LARGE_INTEGER out{};
        if (!::SetFilePointerEx(h, zero, &out, FILE_CURRENT))
            return false;
        pos = static_cast<std::uint64_t>(out.QuadPart);
        return true;
    }

    inline bool io_size(native_handle h, std::uint64_t &size)
    {
        LARGE_INTEGER li{};
        if (!::GetFileSizeEx(h, &li))
            return false;
        size = static_cast<std::uint64_t>(li.QuadPart);
        return true;
    }

    inline bool io_flush(native_handle h) { return ::FlushFileBuffers(h) != 0; }

    /// 控制台 / 管道 / 文件都走这个：控制台要转 UTF-16，重定向时直接写字节
    inline bool write_utf8_handle(native_handle h, const char *data, std::size_t n)
    {
        if (n == 0)
            return true;
        DWORD mode = 0;
        if (::GetConsoleMode(h, &mode))
        {
            const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(n), nullptr, 0);
            if (wlen <= 0)
                return false;
            std::wstring w(static_cast<std::size_t>(wlen), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(n), w.data(), wlen);
            std::size_t off = 0;
            while (off < w.size())
            {
                const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(w.size() - off, 1u << 20));
                DWORD written = 0;
                if (!::WriteConsoleW(h, w.data() + off, chunk, &written, nullptr) || written == 0)
                    return true; // 控制台失效（被关掉）不算致命，别让脚本崩
                off += written;
            }
            return true;
        }
        return io_write(h, data, n);
    }

    inline bool write_stdout_bytes(const char *data, std::size_t n)
    {
        return write_utf8_handle(::GetStdHandle(STD_OUTPUT_HANDLE), data, n);
    }

    inline bool write_stderr_bytes(const char *data, std::size_t n)
    {
        return write_utf8_handle(::GetStdHandle(STD_ERROR_HANDLE), data, n);
    }

    inline bool io_replace(const std::string &from, const std::string &to)
    {
        std::wstring wf, wt;
        if (!wide_path(from, wf) || !wide_path(to, wt))
            return false;
        return ::MoveFileExW(wf.c_str(), wt.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }

    /// 修改时间换算成 Unix 秒（拿不到就给 0）。stat / meta 里给人看用。
    inline double file_mtime_seconds(const std::string &utf8)
    {
        std::wstring w;
        if (!wide_path(utf8, w))
            return 0.0;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!::GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &data))
            return 0.0;
        const std::uint64_t ticks =
            (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
            data.ftLastWriteTime.dwLowDateTime;
        constexpr std::uint64_t kEpochDiff = 116444736000000000ull; // 1601 -> 1970，单位 100ns
        if (ticks < kEpochDiff)
            return 0.0;
        return static_cast<double>(ticks - kEpochDiff) / 1e7;
    }

#else // ------------------------------------------------------------------ 其它平台：FILE*

    using native_handle = std::FILE *;

    inline native_handle invalid_handle() { return nullptr; }
    inline bool valid(native_handle h) { return h != nullptr; }

    enum class IoMode
    {
        Read,
        Write,
        Append,
        ReadWrite
    };

    inline native_handle io_open(const std::string &path, IoMode mode, std::string &err)
    {
        const char *m = "rb";
        switch (mode)
        {
        case IoMode::Read:
            m = "rb";
            break;
        case IoMode::Write:
            m = "wb";
            break;
        case IoMode::Append:
            m = "ab";
            break;
        case IoMode::ReadWrite:
            m = "r+b";
            break;
        }
        std::FILE *f = std::fopen(path.c_str(), m);
        if (!f && mode == IoMode::ReadWrite)
            f = std::fopen(path.c_str(), "w+b"); // 不存在就建
        if (!f)
            err = std::system_category().message(errno);
        return f;
    }

    inline bool io_close(native_handle h) { return h && std::fclose(h) == 0; }

    inline bool io_write(native_handle h, const char *data, std::size_t n)
    {
        return n == 0 || std::fwrite(data, 1, n, h) == n;
    }

    inline bool io_read(native_handle h, char *data, std::size_t n, std::size_t &got)
    {
        got = std::fread(data, 1, n, h);
        return got == n || !std::ferror(h);
    }

    inline bool io_seek(native_handle h, std::uint64_t pos, std::uint64_t &new_pos)
    {
        if (std::fseek(h, static_cast<long>(pos), SEEK_SET) != 0)
            return false;
        new_pos = pos;
        return true;
    }

    inline bool io_tell(native_handle h, std::uint64_t &pos)
    {
        const long p = std::ftell(h);
        if (p < 0)
            return false;
        pos = static_cast<std::uint64_t>(p);
        return true;
    }

    inline bool io_size(native_handle h, std::uint64_t &size)
    {
        const long cur = std::ftell(h);
        if (cur < 0 || std::fseek(h, 0, SEEK_END) != 0)
            return false;
        const long end = std::ftell(h);
        std::fseek(h, cur, SEEK_SET);
        if (end < 0)
            return false;
        size = static_cast<std::uint64_t>(end);
        return true;
    }

    inline bool io_flush(native_handle h) { return std::fflush(h) == 0; }

    inline bool write_stdout_bytes(const char *data, std::size_t n)
    {
        return std::fwrite(data, 1, n, stdout) == n;
    }

    inline bool write_stderr_bytes(const char *data, std::size_t n)
    {
        return std::fwrite(data, 1, n, stderr) == n;
    }

    inline bool io_replace(const std::string &from, const std::string &to)
    {
        return std::rename(from.c_str(), to.c_str()) == 0;
    }

    inline double file_mtime_seconds(const std::string &utf8)
    {
        std::error_code ec;
        const auto t = fs::last_write_time(from_utf8(utf8), ec);
        if (ec)
            return 0.0;
        return static_cast<double>(t.time_since_epoch().count()) / 1e9; // POSIX 上是纳秒
    }

#endif

    // ------------------------------------------------------------------ 整文件读写
    // FastIO / BigIO 共用：一次开-读-关，读之前先卡上限。
    inline bool read_file_all(const std::string &path, std::size_t limit, std::string &out,
                              std::string &err)
    {
        native_handle h = io_open(path, IoMode::Read, err);
        if (!valid(h))
            return false;

        std::uint64_t size = 0;
        if (!io_size(h, size))
        {
            err = "取文件大小失败";
            io_close(h);
            return false;
        }
        if (size > limit)
        {
            err = "文件太大（" + std::to_string(size) + " 字节 > 上限 " + std::to_string(limit) + "）";
            io_close(h);
            return false;
        }

        out.resize(static_cast<std::size_t>(size));
        std::size_t got = 0;
        const bool ok = out.empty() || io_read(h, out.data(), out.size(), got);
        if (!ok)
        {
            err = "读文件失败";
            out.clear();
        }
        else
            out.resize(got);
        io_close(h);
        return ok;
    }

    inline bool write_file_all(const std::string &path, const std::string &text, std::string &err)
    {
        native_handle h = io_open(path, IoMode::Write, err);
        if (!valid(h))
            return false;
        const bool ok = io_write(h, text.data(), text.size());
        if (!ok)
            err = "写文件失败";
        io_close(h);
        return ok;
    }

    /// 先写同目录临时文件再改名：中途失败不会留下半个文件。
    inline bool write_file_atomic(const std::string &path, const std::string &text, std::string &err)
    {
        static unsigned counter = 0;
        const std::string tmp = path + ".astmp" + std::to_string(++counter);
        if (!write_file_all(tmp, text, err))
        {
            std::error_code ec;
            fs::remove(from_utf8(tmp), ec);
            return false;
        }
        if (!io_replace(tmp, path))
        {
            err = "替换目标失败";
            std::error_code ec;
            fs::remove(from_utf8(tmp), ec);
            return false;
        }
        return true;
    }

} // namespace keelio_impl
