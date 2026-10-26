#pragma once
// ============================================================================
//  KeelInstanceState.hpp —— 给内置库用的"每实例一份私有状态"
// ----------------------------------------------------------------------------
//  KeelData 里只有一个 `void (*on_destroy)(KeelData&)` 钩子，而文件句柄、
//  源码缓存这类状态是**每个 VM 一份**的。多个库都要清理时，谁也别覆盖谁：
//
//    * 第一次用 `get()` 时把自己的清理函数接到钩子链头，并记住原来的钩子；
//    * VM 析构 -> 我的 destroy -> 擦掉自己那份（T 的析构里关文件）
//                -> 调用原来的钩子（下一个库 / keelbase 的变量表）
//
//  所以注册顺序无所谓：谁先真正执行指令谁先上链，析构时按"后进先出"回链。
//  前提：`register_all` 之后再让脚本跑（正常宿主都是这个顺序）。
//
//  用法：
//      struct MyState { std::vector<int> stuff; ~MyState() { /* 关文件 */ } };
//      using States = keel::StateTable<MyState>;
//      MyState &st = States::get(d);          // 没有就建
//      if (MyState *p = States::find(d)) ...  // 只查，不建
// ============================================================================

#include "Keel.hpp"

#include <unordered_map>

namespace keel
{
    template <class T>
    struct StateTable
    {
        struct Entry
        {
            T value{};
            void (*prev_on_destroy)(KeelData &) = nullptr;
            bool hooked = false;
        };

        static std::unordered_map<const KeelData *, Entry> &map()
        {
            static std::unordered_map<const KeelData *, Entry> m;
            return m;
        }

        static void destroy(KeelData &d)
        {
            auto it = map().find(&d);
            if (it == map().end())
                return;
            void (*prev)(KeelData &) = it->second.prev_on_destroy;
            map().erase(it); // 析构 value：关句柄 / 释放缓冲
            if (prev)
                prev(d); // 继续走链上的下一个库
        }

        static T &get(KeelData &d)
        {
            Entry &e = map()[&d];
            if (!e.hooked)
            {
                e.hooked = true;
                e.prev_on_destroy = d.on_destroy;
                d.on_destroy = &destroy;
            }
            return e.value;
        }

        static T *find(const KeelData &d)
        {
            auto it = map().find(&d);
            return it == map().end() ? nullptr : &it->second.value;
        }
    };
} // namespace keel
