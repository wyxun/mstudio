# Dashboard Macros Auto-Wrap Layout Design

本文档定义了 mstudio 仪表板（Dashboard）宏快捷按钮的自动换行布局设计方案。

## 目标与背景

当前 `mstudio` 的 Dashboard 面板中，从 `macros.ini` 加载的快捷按钮在渲染时会全部排在同一行，无条件调用 `ImGui::SameLine()`。当宏数量较多时，按钮会超出 Dashboard 窗口的可见宽度限制，导致用户无法查看或点击超出部分的按钮。

本设计的目的是使这些宏快捷按钮能够根据当前 Dashboard 窗口的宽度自动换行排列。

## 详细设计

修改 [dashboard_panel.cpp](file:///e:/Project/mstudio/src/panels/dashboard_panel.cpp) 的宏渲染循环，废弃原本无条件添加的 `ImGui::SameLine()`，引入基于可视边界和下一项宽度的动态排版计算。

### 预测换行排版机制

在绘制宏按钮列表的循环中：
1. 绘制当前宏按钮。
2. 获取当前按钮绘制完毕后的实际右边界 X 坐标 `last_button_x2 = ImGui::GetItemRectMax().x`。
3. 如果仍有下一个按钮待渲染，则预测该按钮渲染后的最大右边界 X 坐标：
   - 按钮预测宽度：`next_width = style.FramePadding.x * 2.0f + ImGui::CalcTextSize(next_label).x`。
   - 预计右边界坐标：`next_x2 = last_button_x2 + style.ItemSpacing.x + next_width`。
4. 比较预计的右边界坐标 `next_x2` 与当前窗口的最大内容可视右边界 `window_visible_x2`：
   - 如果 `next_x2 < window_visible_x2`，则在当前按钮之后调用 `ImGui::SameLine()`。
   - 如果越界，则**不调用** `ImGui::SameLine()`，使得下一个按钮自然折行渲染到下一行头部。

## 影响范围与修改文件

- **修改文件**：`src/panels/dashboard_panel.cpp`
- **影响范围**：仅影响 Dashboard 窗口中 "Quick User Macros" 部分的渲染，不影响其他面板和宏命令的解析与发送。

## 验证方案

### 自动验证与编译测试
- 确保代码正常编译通过，没有语法错误或类型不匹配。

### 手动验证
- 加载包含较多（例如 10 个以上）长 label 宏的 `ini` 文件。
- 缩放 Dashboard 窗口的宽度，观察宏快捷按钮是否随着窗口宽度的缩窄和变宽而自动发生换行/重新排列。
- 确认换行后点击任意按钮依然能够正常向通道 0 发送宏指令。
