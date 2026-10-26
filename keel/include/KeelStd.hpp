#pragma once
// ============================================================================
// KeelStd.hpp -- 一次装齐所有内置函数库（可选便利头文件）
//
//   keel::Keel a;
//   keelstd::register_all(a);       // 5 个核心库 + 3 个 IO 扩展库
//
// 如果只要其中一部分，直接 include 对应文件、调用对应的 xxx_register 即可：
//   核心： keelbase / keelstr / keelcont / keelmath / keelio
//   扩展： keelfast（快速 IO）/ keelbig（复杂 IO）/ keelsrc（动态源）
// 扩展库的指令表见 keel/docs/REFERENCE.md（"IO 三库"一节）。
// ============================================================================
#include "KeelBase.hpp"
#include "KeelBigIO.hpp"
#include "KeelContainer.hpp"
#include "KeelDynSource.hpp"
#include "KeelFastIO.hpp"
#include "KeelIO.hpp"
#include "KeelMath.hpp"
#include "KeelString.hpp"

namespace keelstd
{
    /// 三个 IO 扩展库（快速 IO / 复杂 IO / 动态源）。不想要它们就只装核心五个。
    inline void register_io_extras(keel::Keel &a)
    {
        keelfast::fast_register(a);
        keelbig::big_register(a);
        keelsrc::src_register(a);
    }

    inline void register_all(keel::Keel &a)
    {
        keelbase::base_register(a);
        keelstr::str_register(a);
        keelcont::cont_register(a);
        keelmath::math_register(a);
        keelio::io_register(a);
        register_io_extras(a);
    }

    // 清掉该实例的变量表 / 循环栈 / 执行状态（复用同一个 KeelData 再跑一个程序时
    // 调用；codes 里的块定义保留，之后重新 init(入口块) 就能再跑）。
    // 注意：不清扩展库的状态（已打开的文件句柄、已缓存的源文本），那些跟着 VM 析构走。
    inline void reset_state(keel::KeelData &d)
    {
        keelbase::reset_state(d);
        d.stack.clear();
        d.Int.clear();
        d.strCache.clear();
        d.pending_block_names.clear();
        d.stream = 0;
        d.stream_mode = keel::StreamMode::Stack;
        d.ip = 0;
        d.jumped = false;
        d.end = false;
        d.steps = 0;
        d.adjust_used = 0;
        d.cache_code_ptr.clear();
        d.ip_stack.clear();
    }
}
