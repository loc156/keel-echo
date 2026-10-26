#pragma once
// ============================================================================
// KeelMath.hpp -- 数学函数（需要 <cmath>，所以单独一个文件）
// 基本算数 + - * / % min max clamp abs 在 KeelBase.hpp 里，这里只放
// 必须 <cmath> 才能写的东西。
//
// 指令表（除 pi / e 不弹操作数、isnan 压布尔外，其余都是弹出 double 压回 double；
// 别名与完整语义见 keel/docs/REFERENCE.md）：
//   sqrt x            平方根
//   pow x y           x^y
//   exp ln log10 log2 x
//   sin cos tan asin acos atan x      （弧度）
//   atan2 y x         方位角
//   floor ceil round trunc x          向下 / 向上 / 四舍五入 / 向零取整
//   hypot x y         sqrt(x*x + y*y)
//   fmod x y          浮点余数（符号跟 x）
//   sign x            符号（-1 / 0 / 1）
//   pi e              不弹操作数，压入常量
//   deg rad x         弧度 <-> 角度
//   isnan x -> 布尔
// 别名（log/lg/sqr/signum 等）见 keel/docs/REFERENCE.md。
// ============================================================================
#include "Keel.hpp"
#include "KeelUtil.hpp"

#include <cmath>
#include <string>

namespace keelmath
{
    namespace u = keel::util;

    namespace detail
    {
        template <typename F>
        inline void un(keel::KeelData &d, const char *name, F f)
        {
            double x = 0;
            if (!u::pop_num(d, name, x))
                return;
            u::push(d, keel::make_number(f(x)));
        }

        // 二元：x y -- f(x, y)。先校验再一次性弹，出错时也消费 2 个、压一个 nil
        template <typename F>
        inline void bin(keel::KeelData &d, const char *name, F f)
        {
            if (!u::need(d, 2, name))
                return;
            double y = 0, x = 0;
            if (!u::peek_num(d, 0, name, y) || !u::peek_num(d, 1, name, x))
            {
                u::drop_n(d, 2);
                u::push(d, keel::make_nil());
                return;
            }
            u::drop_n(d, 2);
            u::push(d, keel::make_number(f(x, y)));
        }

        inline void alias(keel::Keel &a, const char *name, const char *of)
        {
            auto it = a.data.funcs.find(of);
            if (it != a.data.funcs.end())
                a.data.funcs.insert_or_assign(name, it->second);
        }
    }

    inline void math_register(keel::Keel &a)
    {
        using namespace detail;

        a.register_func("sqrt", KeelF { un(d, "sqrt", [](double x) { return std::sqrt(x); }); });
        a.register_func("exp", KeelF { un(d, "exp", [](double x) { return std::exp(x); }); });
        a.register_func("ln", KeelF { un(d, "ln", [](double x) { return std::log(x); }); });
        a.register_func("log10", KeelF { un(d, "log10", [](double x) { return std::log10(x); }); });
        a.register_func("log2", KeelF { un(d, "log2", [](double x) { return std::log2(x); }); });

        a.register_func("sin", KeelF { un(d, "sin", [](double x) { return std::sin(x); }); });
        a.register_func("cos", KeelF { un(d, "cos", [](double x) { return std::cos(x); }); });
        a.register_func("tan", KeelF { un(d, "tan", [](double x) { return std::tan(x); }); });
        a.register_func("asin", KeelF { un(d, "asin", [](double x) { return std::asin(x); }); });
        a.register_func("acos", KeelF { un(d, "acos", [](double x) { return std::acos(x); }); });
        a.register_func("atan", KeelF { un(d, "atan", [](double x) { return std::atan(x); }); });

        a.register_func("floor", KeelF { un(d, "floor", [](double x) { return std::floor(x); }); });
        a.register_func("ceil", KeelF { un(d, "ceil", [](double x) { return std::ceil(x); }); });
        a.register_func("round", KeelF { un(d, "round", [](double x) { return std::round(x); }); });
        a.register_func("trunc", KeelF { un(d, "trunc", [](double x) { return std::trunc(x); }); });
        a.register_func("isnan", KeelF {
            double x = 0;
            if (!u::pop_num(d, "isnan", x))
                return;
            u::push(d, keel::make_bool(std::isnan(x)));
        });
        a.register_func("sign", KeelF {
            un(d, "sign", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); });
        });

        a.register_func("pow", KeelF { bin(d, "pow", [](double x, double y) { return std::pow(x, y); }); });
        a.register_func("hypot", KeelF { bin(d, "hypot", [](double x, double y) { return std::hypot(x, y); }); });
        a.register_func("fmod", KeelF { bin(d, "fmod", [](double x, double y) { return std::fmod(x, y); }); });
        a.register_func("atan2", KeelF { bin(d, "atan2", [](double y, double x) { return std::atan2(y, x); }); });

        a.register_func("pi", KeelF { u::push(d, keel::make_number(3.14159265358979323846)); });
        a.register_func("e", KeelF { u::push(d, keel::make_number(2.71828182845904523536)); });
        a.register_func("deg", KeelF {
            un(d, "deg", [](double x) { return x * 180.0 / 3.14159265358979323846; });
        });
        a.register_func("rad", KeelF {
            un(d, "rad", [](double x) { return x * 3.14159265358979323846 / 180.0; });
        });

        // ------------------------------------------------------------ 别名
        alias(a, "log", "ln");
        alias(a, "lg", "log10");
        alias(a, "sqr", "sqrt");
        alias(a, "signum", "sign");
    }
}
