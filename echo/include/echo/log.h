// Echo UI 库 - 日志。
//
// 【为什么自带一个日志】
// 这个库默认以 GUI 子系统（-mwindows）链接，此时 stdout/stderr 是"黑洞"：
// std::cerr 写了没人看得到，初始化失败就表现为"双击没反应"。
// 所以 Echo 自带三条出路，任何一条都能让错误可见：
//
//   1. 环形缓冲：应用内的"日志窗口"可以直接读（见 entries()）；
//   2. 文件：默认写 echo.log（App::init 里可配 AppConfig::logPath）；
//   3. 回调：`log::setCallback` 把每条日志转给控制台窗口或你自己的实现。
//
// 级别：Debug < Info < Warn < Error < Fatal。库只在真正出错时写 Error/Fatal，
// 正常的启动信息写 Info，不会刷屏。
#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace echo {

enum class LogLevel {
    Debug = 0, ///< 诊断细节，默认不会显示给用户
    Info,      ///< 正常事件（启动、加载成功）
    Warn,      ///< 能继续跑，但有东西不对劲
    Error,     ///< 某功能失败，程序还活着
    Fatal      ///< 初始化失败，程序即将退出
};

/// 级别转可读字符串（"INFO" / "WARN" ...），用于自己拼日志行。
const char* toString(LogLevel level);

/// 一条日志。time 是"程序启动后经过的秒数"，不是墙上时间。
struct LogEntry {
    LogLevel level = LogLevel::Info;
    double time = 0.0;
    std::string text;
};

namespace log {

/// 写一条日志：进环形缓冲、写文件（如果开了）、调回调（如果设了）。线程安全。
void write(LogLevel level, const std::string& text);

void debug(const std::string& text);
void info(const std::string& text);
void warn(const std::string& text);
void error(const std::string& text);
void fatal(const std::string& text);

/// 当前缓冲里的全部日志（旧 → 新）。返回拷贝，可安全遍历。
/// 注意：在 setCallback 的回调里调用它是安全的（回调在锁外执行）。
std::vector<LogEntry> entries();

/// 当前缓冲条数。
size_t size();

/// 清空缓冲（不影响文件）。
void clear();

/// 环形缓冲上限，默认 512 条；超出后丢最旧的。
void setCapacity(size_t capacity);

/// 设置文件输出。传空串关闭；打开失败会静默关掉（不影响程序）。
void setFile(const std::string& path);

/// 当前文件路径，未开启时为空串。
const std::string& file();

/// 每条日志的回调（用于把日志实时喂给界面）。
/// 回调必须很快；不要在里面做 ImGui 调用以外的慢操作。
void setCallback(std::function<void(const LogEntry&)> callback);

/// 最近一条日志的正文，没有则为空串。
std::string lastText();

} // namespace log
} // namespace echo
