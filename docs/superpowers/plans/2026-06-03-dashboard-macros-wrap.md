# Dashboard Macros Auto-Wrap Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 允许 Dashboard 中的 Quick User Macros 按钮根据窗口宽度自动换行排列，以避免按钮溢出窗口可视范围。

**Architecture:** 通过在 ImGui 渲染宏按钮时引入流式排版计算。通过 `ImGui::GetItemRectMax().x` 获取当前按钮的右边界坐标，并在渲染下一个按钮前利用 `ImGui::CalcTextSize()` 计算出其预测的右边界，若预测超出当前窗口内容区域可视最大右边界，则不调用 `ImGui::SameLine()` 从而触发自动折行。

**Tech Stack:** C++, Dear ImGui, MSYS2/MinGW-w64 make

---

### Task 1: 修改 Dashboard 宏渲染代码以支持自动换行

**Files:**
- Modify: `src/panels/dashboard_panel.cpp:118-129`

- [ ] **Step 1: 修改 `src/panels/dashboard_panel.cpp` 中的宏渲染循环**

打开 `src/panels/dashboard_panel.cpp`，定位到宏渲染部分（约第 118 行开始），替换为包含预测换行逻辑的代码。

具体需要被替换的原始代码段：
```cpp
    const auto& macros = state.macro_mgr_.GetMacros();
    if (!macros.empty()) {
        ImGui::Separator();
        for (size_t i = 0; i < macros.size(); ++i) {
            char btn_id[128];
            snprintf(btn_id, sizeof(btn_id), "%s##macro%zu", macros[i].label.c_str(), i);
            if (ImGui::Button(btn_id)) {
                net.SendToCh0(macros[i].command + "\n");
            }
            if (i + 1 < macros.size()) ImGui::SameLine();
        }
    }
```

替换后的代码段：
```cpp
    const auto& macros = state.macro_mgr_.GetMacros();
    if (!macros.empty()) {
        ImGui::Separator();
        ImGuiStyle& style = ImGui::GetStyle();
        // 获取当前窗口内容区域的最大可视右边界 X 坐标
        float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        for (size_t i = 0; i < macros.size(); ++i) {
            char btn_id[128];
            snprintf(btn_id, sizeof(btn_id), "%s##macro%zu", macros[i].label.c_str(), i);
            if (ImGui::Button(btn_id)) {
                net.SendToCh0(macros[i].command + "\n");
            }

            // 自动折行逻辑
            if (i + 1 < macros.size()) {
                // 获取当前已渲染按钮的右边界 X 坐标
                float last_button_x2 = ImGui::GetItemRectMax().x;
                // 预测下一个按钮的宽度（包含 Padding 和文本尺寸）
                float next_button_width = style.FramePadding.x * 2.0f + ImGui::CalcTextSize(macros[i + 1].label.c_str()).x;
                // 计算下一个按钮渲染后的预测右边界坐标
                float next_button_x2 = last_button_x2 + style.ItemSpacing.x + next_button_width;

                // 如果预测右边界未超出当前窗口可视最右端，则并排渲染，否则换行
                if (next_button_x2 < window_visible_x2) {
                    ImGui::SameLine();
                }
            }
        }
    }
```

---

### Task 2: 编译与手动功能验证

**Files:**
- Test: 编译运行 `mstudio.exe` 进行手动界面验证。

- [ ] **Step 1: 运行 `build.bat` 编译项目**

在工作区根目录下，运行命令行进行编译：
Run: `.\build.bat`
Expected: 编译无报错，终端打印 `[SUCCESS] Build complete.` 并且成功更新 `mstudio.exe`。

- [ ] **Step 2: 启动 `mstudio.exe` 进行手动验证**

启动应用并执行测试：
1. 运行 `mstudio.exe`。
2. 确保已加载 `macros.ini` 配置文件（如未加载，点击 "Load Macros..." 按钮加载一个包含较多宏的配置文件）。
3. 拖动缩放 "Dashboard" 窗口的宽度，确认原本横向溢出的宏快捷按钮能够根据宽度自适应自动折行排列。
4. 确认折行后，点击任意宏按钮，其绑定的快捷指令依然能够成功触发。
