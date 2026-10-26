#include "echo/log.h"

#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace echo {

const char* toString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    }
    return "?";
}

namespace log {

namespace {

std::mutex& mutex()
{
    static std::mutex m;
    return m;
}

std::deque<LogEntry>& buffer()
{
    static std::deque<LogEntry> entries;
    return entries;
}

size_t& capacity()
{
    static size_t cap = 512;
    return cap;
}

std::ofstream& fileSink()
{
    static std::ofstream stream;
    return stream;
}

std::string& filePath()
{
    static std::string path;
    return path;
}

std::function<void(const LogEntry&)>& callback()
{
    static std::function<void(const LogEntry&)> cb;
    return cb;
}

std::chrono::steady_clock::time_point startTime()
{
    static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    return start;
}

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime()).count();
}

} // namespace

void write(LogLevel level, const std::string& text)
{
    LogEntry entry;
    entry.level = level;
    entry.time = now();
    entry.text = text;

    std::function<void(const LogEntry&)> cb;
    {
        std::lock_guard<std::mutex> lock(mutex());
        auto& entries = buffer();
        entries.push_back(entry);
        while (entries.size() > capacity())
            entries.pop_front();

        if (fileSink().is_open()) {
            std::ostringstream line;
            line << '[' << std::fixed << std::setprecision(3) << entry.time << "] " << toString(level) << ": "
                 << text << '\n';
            fileSink() << line.str();
            fileSink().flush();
        }

        cb = callback();
    }

    // Called outside the lock so a callback may itself call entries().
    if (cb)
        cb(entry);
}

void debug(const std::string& text) { write(LogLevel::Debug, text); }
void info(const std::string& text) { write(LogLevel::Info, text); }
void warn(const std::string& text) { write(LogLevel::Warn, text); }
void error(const std::string& text) { write(LogLevel::Error, text); }
void fatal(const std::string& text) { write(LogLevel::Fatal, text); }

std::vector<LogEntry> entries()
{
    std::lock_guard<std::mutex> lock(mutex());
    return std::vector<LogEntry>(buffer().begin(), buffer().end());
}

size_t size()
{
    std::lock_guard<std::mutex> lock(mutex());
    return buffer().size();
}

void clear()
{
    std::lock_guard<std::mutex> lock(mutex());
    buffer().clear();
}

void setCapacity(size_t cap)
{
    std::lock_guard<std::mutex> lock(mutex());
    capacity() = cap == 0 ? 1 : cap;
    while (buffer().size() > capacity())
        buffer().pop_front();
}

void setFile(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex());
    if (fileSink().is_open())
        fileSink().close();
    filePath() = path;
    if (!path.empty()) {
        fileSink().open(path.c_str(), std::ios::out | std::ios::app);
        if (!fileSink().is_open())
            filePath().clear();
    }
}

const std::string& file()
{
    return filePath();
}

void setCallback(std::function<void(const LogEntry&)> cb)
{
    std::lock_guard<std::mutex> lock(mutex());
    callback() = std::move(cb);
}

std::string lastText()
{
    std::lock_guard<std::mutex> lock(mutex());
    return buffer().empty() ? std::string{} : buffer().back().text;
}

} // namespace log
} // namespace echo
