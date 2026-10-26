// Echo UI 库 - 固定步长工具。
//
// 【为什么需要】
// 界面是"每帧画一次"，但模拟/动画/轮询往往要求"每 1/60 秒算一步"，与帧率无关。
// 如果直接用 dt 做累加，帧率一抖结果就抖，120Hz 屏和 60Hz 屏行为还不一致。
//
//   FixedStep step(1.0 / 60.0);
//   while (app.running() && app.beginFrame()) {
//       step.advance(app.deltaTime(), [&](double fixedDt) { simulate(fixedDt); });
//       draw(step.alpha());   // alpha 是"当前帧在两个模拟步之间"的比例，用于插值
//       app.endFrame();
//   }
//
// 【死亡螺旋保护】
// 如果某一帧特别长（断点、拖窗口），按步长会一次算出成百上千步，越算越慢。
// 所以每帧最多补 maxTicksPerFrame 步，剩下的直接丢掉（宁可慢动作，不可卡死）。
#pragma once

#include <cstddef>

namespace echo {

class FixedStep {
public:
    /// step 每次回调的时间间隔；maxTicksPerFrame 单帧最多补几步。
    explicit FixedStep(double step = 1.0 / 60.0, size_t maxTicksPerFrame = 5)
        : step_(step), maxTicksPerFrame_(maxTicksPerFrame)
    {
    }

    /// 累积 deltaTime，并按需回调 tick(step)。回调次数在 0..maxTicksPerFrame 之间。
    template <class Fn>
    void advance(double deltaTime, Fn&& tick)
    {
        accumulator_ += deltaTime;
        size_t ticks = 0;
        while (accumulator_ >= step_ && ticks < maxTicksPerFrame_) {
            accumulator_ -= step_;
            ++ticks;
            tick(step_);
        }
        // 追不上了就别再攒，避免下一帧继续爆。
        if (ticks == maxTicksPerFrame_ && accumulator_ > step_)
            accumulator_ = step_;
    }

    /// 0..1，表示距离下一个模拟步还差多少（做渲染插值用）。
    double alpha() const { return accumulator_ / step_; }
    double accumulator() const { return accumulator_; }
    double step() const { return step_; }

    void setStep(double step) { step_ = step > 0.0 ? step : 1.0 / 60.0; }
    void reset() { accumulator_ = 0.0; }

private:
    double step_ = 1.0 / 60.0;
    double accumulator_ = 0.0;
    size_t maxTicksPerFrame_ = 5;
};

/// 旧名字，等价于 FixedStep。
using FixedTimestep = FixedStep;

} // namespace echo
