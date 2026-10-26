#pragma once
// ============================================================================
//  KeelUI.hpp —— Keel × ImGui（Echo 的底座）立即模式绑定
// ----------------------------------------------------------------------------
//  设计要点（和"把 Echo 的对象树注册进 VM"不同）：
//    * **立即模式**：脚本的 frame 块每帧从头跑一遍，text/button/input 就是指令；
//    * **没有回调**：按钮返回 bool、输入返回字符串，事件自然回到 VM 变量；
//    * **宿主管帧循环**：Echo 的 App 负责窗口/GL/ImGui，Host 每帧调用 frame 块；
//    * **不用 Echo 的 PanelWindow/Content**：那是保留式对象树，栈机里没有闭包，
//      数据绑定和回调都会变成负担。Echo 在这里提供 App/字体/主题，控件直接用 ImGui。
//
//  脚本约定（由 Host 实现）：
//      fn startup { ... }   只在装载后跑一次（定义块、初始化变量）
//      fn frame   { ... }   每帧跑一次（不要在里面定义块）
//      （文件顶层的 body 是"序章"：装载后跑一次，用来定义 startup/frame 这两个块；
//        所以顶层只放 fn 定义/变量，别放 ui.* 调用）
//
//  每帧的执行方式：init(frame) -> 清栈 -> 最多跑 budget 条 step()。
//  VM 变量跨帧保留；栈每帧清空；出错就停脚本并在窗口里显示（不崩 App）。
//
//  指令表（前缀 ui.）：
//      ui.begin      标题 宽 高 --        开一个窗口（FirstUseEver 尺寸）
//      ui.end        --                   关窗口
//      ui.text       值 --                一行文本
//      ui.textWrapped 值 --               自动折行文本
//      ui.textDim    值 --                灰色文本
//      ui.button     标签 -- 布尔         点击返回 true
//      ui.selectable 标签 -- 布尔
//      ui.input      id -- 字符串         带持久缓冲的单行输入
//      ui.sameLine   --
//      ui.separator  --
//      ui.spacing    --
//      ui.childBegin id 宽 高 --          带边框的滚动子窗口
//      ui.childEnd   --
//      ui.scrollBottom --
// ============================================================================

#include "KeelFastIO.hpp"
#include "KeelLoader.hpp"
#include "KeelStd.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <cfloat>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keelui
{
    namespace u = keel::util;

    struct UiState
    {
        std::unordered_map<std::string, std::string> text; // input 的持久缓冲
        int windows = 0;
        int children = 0;
        int tables = 0;
        int tab_items = 0;
        int tab_bars = 0;
    };

    using States = keel::StateTable<UiState>;

    /// 每帧清掉没配对的 Begin/End，避免 ImGui 断言把整个 App 带走
    inline void force_close(UiState &st)
    {
        while (st.tables > 0)
        {
            ImGui::EndTable();
            --st.tables;
        }
        while (st.tab_items > 0)
        {
            ImGui::EndTabItem();
            --st.tab_items;
        }
        while (st.tab_bars > 0)
        {
            ImGui::EndTabBar();
            --st.tab_bars;
        }
        while (st.children > 0)
        {
            ImGui::EndChild();
            --st.children;
        }
        while (st.windows > 0)
        {
            ImGui::End();
            --st.windows;
        }
    }

    inline void register_ui(keel::Keel &a)
    {
        a.register_func("ui.begin", KeelF {
            if (!u::need(d, 3, "ui.begin"))
                return;
            double h = 0;
            double w = 0;
            if (!u::pop_num(d, "ui.begin", h) || !u::pop_num(d, "ui.begin", w))
                return;
            std::string title;
            if (!u::pop_text(d, "ui.begin", title))
                return;
            UiState &st = States::get(d);
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            // w/h <= 1 视为"占屏幕比例"，方便脚本在不同 DPI 下自适应
            if (w > 0 && w <= 1.0 && h > 0 && h <= 1.0)
            {
                w *= ds.x;
                h *= ds.y;
            }
            if (w <= 0)
                w = ds.x * 0.8;
            if (h <= 0)
                h = ds.y * 0.8;
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w), static_cast<float>(h)),
                                     ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>((ds.x - w) * 0.5),
                                           static_cast<float>((ds.y - h) * 0.5)),
                                    ImGuiCond_FirstUseEver);
            ImGui::Begin(title.c_str());
            ++st.windows;
        });

        a.register_func("ui.end", KeelF {
            (void)d;
            UiState &st = States::get(d);
            if (st.windows > 0)
            {
                ImGui::End();
                --st.windows;
            }
        });

        a.register_func("ui.text", KeelF {
            if (!u::need(d, 1, "ui.text"))
                return;
            const std::string s = u::pop(d).toString();
            ImGui::TextUnformatted(s.c_str());
        });

        a.register_func("ui.textWrapped", KeelF {
            if (!u::need(d, 1, "ui.textWrapped"))
                return;
            const std::string s = u::pop(d).toString();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(s.c_str());
            ImGui::PopTextWrapPos();
        });

        a.register_func("ui.textDim", KeelF {
            if (!u::need(d, 1, "ui.textDim"))
                return;
            const std::string s = u::pop(d).toString();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.57f, 0.60f, 1.0f));
            ImGui::TextUnformatted(s.c_str());
            ImGui::PopStyleColor();
        });

        a.register_func("ui.button", KeelF {
            if (!u::need(d, 1, "ui.button"))
                return;
            const std::string s = u::pop(d).toString();
            u::push(d, keel::make_bool(ImGui::Button(s.c_str())));
        });

        a.register_func("ui.selectable", KeelF {
            if (!u::need(d, 1, "ui.selectable"))
                return;
            const std::string s = u::pop(d).toString();
            u::push(d, keel::make_bool(ImGui::Selectable(s.c_str())));
        });

        a.register_func("ui.input", KeelF {
            if (!u::need(d, 1, "ui.input"))
                return;
            const std::string id = u::pop(d).toString();
            UiState &st = States::get(d);
            std::string &buf = st.text[id];
            const std::string label = "##" + id; // 不显示标签，只当 ID
            ImGui::SetNextItemWidth(-170.0f);
            ImGui::InputText(label.c_str(), &buf);
            u::push(d, keel::make_string(buf));
        });

        a.register_func("ui.sameLine", KeelF {
            (void)d;
            ImGui::SameLine();
        });

        a.register_func("ui.separator", KeelF {
            (void)d;
            ImGui::Separator();
        });

        a.register_func("ui.spacing", KeelF {
            (void)d;
            ImGui::Spacing();
        });

        a.register_func("ui.childBegin", KeelF {
            if (!u::need(d, 3, "ui.childBegin"))
                return;
            double h = 0;
            double w = 0;
            if (!u::pop_num(d, "ui.childBegin", h) || !u::pop_num(d, "ui.childBegin", w))
                return;
            std::string id;
            if (!u::pop_text(d, "ui.childBegin", id))
                return;
            UiState &st = States::get(d);
            ImGui::BeginChild(id.c_str(), ImVec2(static_cast<float>(w), static_cast<float>(h)), true);
            ++st.children;
        });

        a.register_func("ui.childEnd", KeelF {
            (void)d;
            UiState &st = States::get(d);
            if (st.children > 0)
            {
                ImGui::EndChild();
                --st.children;
            }
        });

        a.register_func("ui.scrollBottom", KeelF {
            (void)d;
            ImGui::SetScrollHereY(1.0f);
        });

        // ---------------------------------------------------------- 页签
        a.register_func("ui.tabBar", KeelF {
            if (!u::need(d, 1, "ui.tabBar"))
                return;
            const std::string id = u::pop(d).toString();
            const bool ok = ImGui::BeginTabBar(id.c_str());
            if (ok)
            {
                UiState &st = States::get(d);
                ++st.tab_bars;
            }
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("ui.tabItem", KeelF {
            if (!u::need(d, 1, "ui.tabItem"))
                return;
            const std::string label = u::pop(d).toString();
            const bool ok = ImGui::BeginTabItem(label.c_str());
            if (ok)
            {
                UiState &st = States::get(d);
                ++st.tab_items;
            }
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("ui.endTabItem", KeelF {
            (void)d;
            UiState &st = States::get(d);
            if (st.tab_items > 0)
            {
                ImGui::EndTabItem();
                --st.tab_items;
            }
        });

        a.register_func("ui.endTabBar", KeelF {
            (void)d;
            UiState &st = States::get(d);
            if (st.tab_bars > 0)
            {
                ImGui::EndTabBar();
                --st.tab_bars;
            }
        });

        // ---------------------------------------------------------- 表格
        a.register_func("ui.tableBegin", KeelF {
            if (!u::need(d, 2, "ui.tableBegin"))
                return;
            double cols = 0;
            if (!u::pop_num(d, "ui.tableBegin", cols))
                return;
            const std::string id = u::pop(d).toString();
            const bool ok = ImGui::BeginTable(
                id.c_str(), static_cast<int>(cols),
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_SizingStretchProp);
            if (ok)
            {
                UiState &st = States::get(d);
                ++st.tables;
            }
            u::push(d, keel::make_bool(ok));
        });

        a.register_func("ui.tableSetup", KeelF {
            if (!u::need(d, 2, "ui.tableSetup"))
                return;
            double weight = 1;
            if (!u::pop_num(d, "ui.tableSetup", weight))
                return;
            const std::string label = u::pop(d).toString();
            ImGui::TableSetupColumn(label.c_str(), ImGuiTableColumnFlags_WidthStretch,
                                    static_cast<float>(weight));
        });

        a.register_func("ui.tableHeaders", KeelF {
            (void)d;
            ImGui::TableHeadersRow();
        });

        a.register_func("ui.tableRow", KeelF {
            (void)d;
            ImGui::TableNextRow();
        });

        a.register_func("ui.tableCell", KeelF {
            (void)d;
            ImGui::TableNextColumn();
        });

        a.register_func("ui.tableEnd", KeelF {
            (void)d;
            UiState &st = States::get(d);
            if (st.tables > 0)
            {
                ImGui::EndTable();
                --st.tables;
            }
        });

        // ---------------------------------------------------------- 其它
        a.register_func("ui.heading", KeelF {
            if (!u::need(d, 1, "ui.heading"))
                return;
            const std::string s = u::pop(d).toString();
            ImGui::SeparatorText(s.c_str());
        });

        a.register_func("ui.plot", KeelF {
            if (!u::need(d, 2, "ui.plot"))
                return;
            keel::Value data = u::pop(d);
            std::string label;
            if (!u::pop_text(d, "ui.plot", label))
                return;
            std::vector<float> vals;
            if (data.isList() && data.list)
            {
                vals.reserve(data.list->size());
                for (const auto &v : *data.list)
                {
                    double x = 0;
                    if (u::num(v, x))
                        vals.push_back(static_cast<float>(x));
                }
            }
            if (vals.empty())
            {
                ImGui::TextUnformatted("(无数据)");
                return;
            }
            const std::string id = "##" + label;
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.35f, 0.65f, 1.0f, 1.0f));
            ImGui::PlotLines(id.c_str(), vals.data(), static_cast<int>(vals.size()), 0, nullptr,
                             FLT_MAX, FLT_MAX, ImVec2(0, 90));
            ImGui::PopStyleColor();
        });
    }

    // ------------------------------------------------------------------ 宿主
    struct Host
    {
        std::unique_ptr<keel::Keel> vm;
        keelld::Program prog;
        std::string path;
        std::string error;
        keelld::Syntax syntax = keelld::Syntax::Surface;
        bool loaded = false;
        int budget = 100000; // 每帧步数上限（安全网；UI 脚本比 agent 宽松）
        int last_steps = 0;
        int max_steps = 0;

        /// 宿主自己的指令（比如 repl.run）要在 install/define_block 之前注册
        std::function<void(keel::Keel &)> before_install;

        bool load(const std::string &p, keelld::Syntax syntax = keelld::Syntax::Surface)
        {
            path = p;
            this->syntax = syntax;
            error.clear();
            loaded = false;
            keelld::Options opt;
            opt.syntax = syntax;
            prog = keelld::load_file(p, opt);
            if (!prog.ok())
            {
                error = keelld::format_diags(prog);
                return false;
            }

            vm = std::make_unique<keel::Keel>();
            keelstd::register_all(*vm);
            register_ui(*vm);
            if (before_install)
                before_install(*vm);

            keelld::Environment env;
            env.limits.steps = 10000000; // UI 脚本不是 agent，别用 20 万步
            env.limits.depth = 1000;
            env.limits.stack = 100000;
            env.limits.max_adjust = 20;
            env.stdout_sink = [](const char *, std::size_t) {}; // UI 脚本的 f.* 丢弃

            const keelld::Prepared prep = keelld::install(*vm, prog, env);
            if (!prep.ok)
            {
                error = prep.error_text;
                return false;
            }

            // 序章：定义 startup / frame 这两个块（顶层只该放 fn 定义）
            keelld::execute(*vm);
            if (!keel::keel_errors().empty())
            {
                error = keelld::format_runtime_errors(*vm, prog);
                return false;
            }

            if (!vm->data.codes.count("frame"))
            {
                error = "脚本里没有 frame 块（用 fn frame { ... } 定义）";
                return false;
            }
            if (vm->data.codes.count("startup") && !run_block("startup"))
                return false;

            loaded = true;
            return true;
        }

        bool reload() { return load(path, syntax); }

        void frame()
        {
            if (!loaded)
            {
                ImGui::Begin("Keel UI");
                ImGui::TextWrapped("%s", error.empty() ? "未装载脚本" : error.c_str());
                ImGui::End();
                return;
            }
            run_block("frame");

            UiState &st = States::get(vm->data);
            force_close(st); // 脚本没配对就替它收尾

            if (!keel::keel_errors().empty())
            {
                error = keelld::format_runtime_errors(*vm, prog);
                loaded = false;
            }
        }

    private:
        bool run_block(const std::string &name)
        {
            if (!vm || !vm->data.codes.count(name))
                return false;

            UiState &st = States::get(vm->data);
            st.windows = 0;
            st.children = 0;
            st.tables = 0;
            st.tab_items = 0;
            st.tab_bars = 0;

            keel::clear_errors();
            vm->init(name);
            // 不依赖 init 的实现细节：显式重置每帧预算计数
            vm->data.steps = 0;
            vm->data.adjust_used = 0;
            vm->data.stack.clear();
            int i = 0;
            for (; i < budget && !vm->data.end; ++i)
                vm->step();
            last_steps = i;
            if (last_steps > max_steps)
                max_steps = last_steps;
            keelfast::flush_all(vm->data);

            if (!keel::keel_errors().empty())
            {
                force_close(st);
                error = keelld::format_runtime_errors(*vm, prog);
                loaded = false;
                return false;
            }
            if (!vm->data.end)
            {
                force_close(st);
                error = "帧预算用完（frame 块超过 " + std::to_string(budget) + " 条指令）";
                loaded = false;
                return false;
            }
            return true;
        }
    };
} // namespace keelui
