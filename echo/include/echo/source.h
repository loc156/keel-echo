// Echo UI 库 - 取值来源（Source）。
//
// 【为什么需要它】
// 界面每帧重画一次，但"要显示的值"往往在别处（配置、模型、硬件计数）。
// 如果控件在构造时就把值拷贝进去，值一变就得销毁重建控件，代码会碎一地：
//
//     // 旧写法：值变了要整屏重建
//     text.clear();
//     text.add<PlainText>(std::to_string(cpuPercent) + "%");
//
// Echo 的做法是让控件保存"怎么取值"，而不是"值本身"：
//
//     PlainText([&] { return std::to_string(cpuPercent) + "%"; });  // 每帧求值
//
// 一个 Source<T> 可以装三种东西：
//   1. 普通值（快照）           Source<int> s = 42;            每帧返回 42
//   2. 指针（每帧解引用）       Source<int> s = &counter;       值变则显示变
//   3. 可调用对象（每帧调用）   Source<int> s = [&] { ... };    想怎么算就怎么算
//
// 【代价与注意】
//   - 可调用对象会被拷贝进 std::function，捕获请用引用捕获 [&] 或拷贝小对象；
//     捕获局部变量时，必须保证控件活得比那个变量短（通常是"同一个作用域内"）。
//   - 每帧都调用，别在里面做重活（读文件、加锁、遍历大容器）。
//   - get() 返回 T 的副本；std::string 会因此每帧构造一次，可接受，
//     但如果你每秒要画上万行文本，请自己缓存成字符串再传快照。
#pragma once

#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace echo {

namespace detail {

// 判断 U 是否是"指向 T 的指针"（含 const/volatile 限定）。
// 用来把 `Source<int> s = &value;` 和 `Source<int> s = 1;` 区分开，
// 因为 bool* 能隐式转成 bool，不区分会产生二义性。
template <class T, class U>
struct is_pointer_to : std::false_type {};

template <class T>
struct is_pointer_to<T, T*> : std::true_type {};
template <class T>
struct is_pointer_to<T, const T*> : std::true_type {};
template <class T>
struct is_pointer_to<T, volatile T*> : std::true_type {};
template <class T>
struct is_pointer_to<T, const volatile T*> : std::true_type {};

} // namespace detail

/// 一个"能取出 T"的对象：快照 / 指针 / 函数，三者语法一致。
template <class T>
class Source {
public:
    /// 空来源：get() 返回 T{}（数字是 0、字符串是空串）。
    Source() = default;

    /// 快照：把值存起来，每次都返回同一份拷贝。
    /// 接受任何能转换成 T 的类型，所以 const char* 可以直接当 std::string 用。
    template <class U,
              class = std::enable_if_t<std::is_convertible_v<U, T> &&
                                        !detail::is_pointer_to<T, std::decay_t<U>>::value &&
                                        !std::is_invocable_r_v<T, U&> &&
                                        !std::is_same_v<std::decay_t<U>, Source>>>
    Source(U&& v) : fn_([stored = T(std::forward<U>(v))]() -> T { return stored; })
    {
    }

    /// 指针：每次 get() 解引用。指针为空时返回 T{}，不会崩。
    /// 用法：MeterText("CPU", &cpuPercent, 100.0f, "%");
    Source(const T* ptr) : fn_([ptr]() -> T { return ptr ? *ptr : T{}; }) {}

    /// 可调用对象：每次 get() 调用一次。
    /// 用法：PlainText([&] { return "任务数: " + std::to_string(tasks.size()); });
    template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Source> &&
                                                std::is_invocable_r_v<T, F&>>>
    Source(F fn) : fn_(std::move(fn))
    {
    }

    /// 显式传入 std::function（比如从别处接过来的回调）。
    Source(std::function<T()> fn) : fn_(std::move(fn)) {}

    /// 取当前值。控件每帧调用一次。
    T get() const { return fn_ ? fn_() : T{}; }

    /// 是否装了东西（空 Source 的 get() 返回默认值）。
    bool valid() const { return static_cast<bool>(fn_); }

private:
    std::function<T()> fn_;
};

/// 显式构造一个快照来源，用于消除重载歧义。
template <class T>
Source<T> value(T v)
{
    return Source<T>(std::move(v));
}

/// 显式构造一个指针来源：`Source<int> s = ref(&counter);`
template <class T>
Source<T> ref(const T* ptr)
{
    return Source<T>(ptr);
}

} // namespace echo
