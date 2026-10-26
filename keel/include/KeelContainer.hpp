#pragma once
// ============================================================================
// KeelContainer.hpp -- 列表 / 映射 操作
// 只依赖 Keel.hpp（keel::Value 自带 list / map / umap），不需要额外标准库头
// （<algorithm> 只用来排序）。
//
// 约定：容器是 shared_ptr 共享的，append / put / del 都是「就地改」，
// 同一个容器的其它拷贝能看到变化；这些指令同时把容器本身压回栈，方便链式写。
//
// 指令表：
//   list / map / umap        -- 新容器
//   append                   列表 值 -- 列表（尾部追加）
//   get                      容器 键 -- 值（缺失给 nil，不报错）
//   put                      容器 键 值 -- 容器|nil（列表：键当下标，负数从尾部算，
//                                                        越界报错并给 nil；映射：按键覆盖）
//   has                      容器 键 -- 布尔
//   del                      容器 键 -- 容器
//   keys / values            映射 -- 列表
//   first / last             列表 -- 值
//   extend                   列表 列表 -- 列表（前者接上后者）
//   sort                     列表 -- 列表（数字优先，其余按文本；就地排序）
// 别名（dict/array/push 等）见 keel/docs/REFERENCE.md。
// ============================================================================
#include "Keel.hpp"
#include "KeelUtil.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace keelcont
{
    namespace u = keel::util;

    namespace detail
    {
        inline void alias(keel::Keel &a, const char *name, const char *of)
        {
            auto it = a.data.funcs.find(of);
            if (it != a.data.funcs.end())
                a.data.funcs.insert_or_assign(name, it->second);
        }

        // 列表下标：负数从尾部算；返回 -1 表示越界
        inline long long list_index(const keel::Value &key, size_t n)
        {
            double x = 0;
            if (!u::num(key, x))
                return -1;
            long long i = static_cast<long long>(u::trunc_d(x));
            if (i < 0)
                i += static_cast<long long>(n);
            if (i < 0 || i >= static_cast<long long>(n))
                return -1;
            return i;
        }

        inline bool get_map(const keel::Value &c, const std::string &key, keel::Value &out)
        {
            if (c.isMap())
                return c.map && c.map->count(key) ? (out = c.map->at(key), true) : false;
            if (c.isUMap())
                return c.unmap && c.unmap->count(key) ? (out = c.unmap->at(key), true) : false;
            return false;
        }

        struct Less
        {
            bool operator()(const keel::Value &a, const keel::Value &b) const
            {
                double x = 0, y = 0;
                if (u::num(a, x) && u::num(b, y))
                    return x < y;
                return a.toString() < b.toString();
            }
        };
    }

    inline void cont_register(keel::Keel &a)
    {
        using namespace detail;

        a.register_func("list", KeelF { u::push(d, keel::make_list()); });
        a.register_func("map", KeelF { u::push(d, keel::make_map()); });
        a.register_func("umap", KeelF { u::push(d, keel::make_umap()); });

        a.register_func("append", KeelF {
            if (!u::need(d, 2, "append"))
                return;
            keel::Value v = u::pop(d);
            keel::Value lv = u::pop(d);
            if (!lv.isList())
            {
                u::err("append", std::string("expected list, got ") + keel::value_type(lv.type));
                u::push(d, keel::make_nil());
                return;
            }
            lv.push(std::move(v));
            u::push(d, std::move(lv));
        });

        a.register_func("get", KeelF {
            if (!u::need(d, 2, "get"))
                return;
            keel::Value key = u::pop(d);
            keel::Value c = u::pop(d);
            if (c.isList())
            {
                const long long i = list_index(key, c.list ? c.list->size() : 0);
                if (i < 0)
                {
                    u::err("get", "list index out of range");
                    u::push(d, keel::make_nil());
                    return;
                }
                u::push(d, (*c.list)[static_cast<size_t>(i)]);
                return;
            }
            if (c.isMap() || c.isUMap())
            {
                keel::Value out;
                if (!get_map(c, key.toString(), out))
                    out = keel::make_nil();
                u::push(d, std::move(out));
                return;
            }
            u::err("get", std::string("expected list/map, got ") + keel::value_type(c.type));
            u::push(d, keel::make_nil());
        });

        a.register_func("put", KeelF {
            if (!u::need(d, 3, "put"))
                return;
            keel::Value v = u::pop(d);
            keel::Value key = u::pop(d);
            keel::Value c = u::pop(d);
            if (c.isList())
            {
                if (!c.list)
                    c.list = std::make_shared<std::vector<keel::Value>>();
                const long long i = list_index(key, c.list->size());
                if (i < 0)
                {
                    // 越界一律报错（和 get 一致）；不再静默追加
                    u::err("put", "list index out of range");
                    u::push(d, keel::make_nil());
                    return;
                }
                (*c.list)[static_cast<size_t>(i)] = std::move(v);
                u::push(d, std::move(c));
                return;
            }
            if (c.isMap() || c.isUMap())
            {
                c.set(key.toString(), std::move(v));
                u::push(d, std::move(c));
                return;
            }
            u::err("put", std::string("expected list/map, got ") + keel::value_type(c.type));
            u::push(d, keel::make_nil());
        });

        a.register_func("has", KeelF {
            if (!u::need(d, 2, "has"))
                return;
            keel::Value key = u::pop(d);
            keel::Value c = u::pop(d);
            if (c.isList())
            {
                const long long i = list_index(key, c.list ? c.list->size() : 0);
                u::push(d, keel::make_bool(i >= 0));
                return;
            }
            if (c.isMap())
            {
                u::push(d, keel::make_bool(c.map && c.map->count(key.toString())));
                return;
            }
            if (c.isUMap())
            {
                u::push(d, keel::make_bool(c.unmap && c.unmap->count(key.toString())));
                return;
            }
            if (c.isString())
            {
                u::push(d, keel::make_bool(c.s.find(key.toString()) != std::string::npos));
                return;
            }
            u::push(d, keel::make_bool(false));
        });

        a.register_func("del", KeelF {
            if (!u::need(d, 2, "del"))
                return;
            keel::Value key = u::pop(d);
            keel::Value c = u::pop(d);
            if (c.isList())
            {
                const long long i = list_index(key, c.list ? c.list->size() : 0);
                if (i < 0)
                {
                    u::err("del", "list index out of range");
                    u::push(d, keel::make_nil());
                    return;
                }
                c.list->erase(c.list->begin() + i);
                u::push(d, std::move(c));
                return;
            }
            if (c.isMap())
            {
                if (c.map)
                    c.map->erase(key.toString());
                u::push(d, std::move(c));
                return;
            }
            if (c.isUMap())
            {
                if (c.unmap)
                    c.unmap->erase(key.toString());
                u::push(d, std::move(c));
                return;
            }
            u::err("del", std::string("expected list/map, got ") + keel::value_type(c.type));
            u::push(d, keel::make_nil());
        });

        a.register_func("keys", KeelF {
            if (!u::need(d, 1, "keys"))
                return;
            keel::Value c = u::pop(d);
            keel::Value out = keel::make_list();
            if (c.isMap() && c.map)
                for (const auto &kv : *c.map)
                    out.push(keel::make_string(kv.first));
            else if (c.isUMap() && c.unmap)
                for (const auto &kv : *c.unmap)
                    out.push(keel::make_string(kv.first));
            else
                u::err("keys", std::string("expected map, got ") + keel::value_type(c.type));
            u::push(d, std::move(out));
        });

        a.register_func("values", KeelF {
            if (!u::need(d, 1, "values"))
                return;
            keel::Value c = u::pop(d);
            keel::Value out = keel::make_list();
            if (c.isMap() && c.map)
                for (const auto &kv : *c.map)
                    out.push(kv.second);
            else if (c.isUMap() && c.unmap)
                for (const auto &kv : *c.unmap)
                    out.push(kv.second);
            else
                u::err("values", std::string("expected map, got ") + keel::value_type(c.type));
            u::push(d, std::move(out));
        });

        a.register_func("first", KeelF {
            if (!u::need(d, 1, "first"))
                return;
            keel::Value c = u::pop(d);
            if (!c.isList() || !c.list || c.list->empty())
            {
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, c.list->front());
        });

        a.register_func("last", KeelF {
            if (!u::need(d, 1, "last"))
                return;
            keel::Value c = u::pop(d);
            if (!c.isList() || !c.list || c.list->empty())
            {
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, c.list->back());
        });

        a.register_func("extend", KeelF {
            if (!u::need(d, 2, "extend"))
                return;
            keel::Value b = u::pop(d);
            keel::Value av = u::pop(d);
            if (!av.isList() || !b.isList())
            {
                u::err("extend", "both operands must be lists");
                u::push(d, keel::make_nil());
                return;
            }
            if (!av.list)
                av.list = std::make_shared<std::vector<keel::Value>>();
            if (b.list)
            {
                const size_t added = b.list->size() * sizeof(keel::Value);
                if (!u::check_alloc(d, added, "extend"))
                {
                    u::push(d, keel::make_nil());
                    return;
                }
                if (av.list == b.list)
                {
                    // 同一个 list（`a dup extend`）：往里 insert 自己的元素，
                    // 迭代器会被扩容失效 —— 标准里是 UB。先拷一份再插。
                    const std::vector<keel::Value> copy = *b.list;
                    av.list->insert(av.list->end(), copy.begin(), copy.end());
                }
                else
                {
                    av.list->insert(av.list->end(), b.list->begin(), b.list->end());
                }
            }
            u::push(d, std::move(av));
        });

        a.register_func("sort", KeelF {
            if (!u::need(d, 1, "sort"))
                return;
            keel::Value c = u::pop(d);
            if (!c.isList() || !c.list)
            {
                u::err("sort", std::string("expected list, got ") + keel::value_type(c.type));
                u::push(d, keel::make_nil());
                return;
            }
            std::stable_sort(c.list->begin(), c.list->end(), Less{});
            u::push(d, std::move(c));
        });

        // ------------------------------------------------------------ 别名
        // 注意：不注册 "set"/"contains"，避免覆盖 Base 的变量指令和 String 的
        // 字符串 contains。
        alias(a, "dict", "map");
        alias(a, "array", "list");
        alias(a, "push", "append");
    }
}
