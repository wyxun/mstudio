# MStudio 性能卡顿优化与 UI 视觉美化设计文档

*   **创建日期**：2026-06-04
*   **状态**：设计验证中 (Pending Review)
*   **作者**：Antigravity
*   **优化目标**：彻底解决 OpenOCD 连接时的卡顿问题、大幅降低大日志渲染开销、支持文本局部复制，并提升界面输入框的视觉识别度。

---

## 1. 痛点分析与诊断报告

### 1.1 输入框响应慢与打字延迟 (Lag & Input Delay)
经查，输入框卡顿是由两个独立的性能瓶颈引起的：
1.  **OpenOCD 同步阻塞通信**：
    在 `RegisterPanel::Render()` 和 `VariablePanel::Render()` 中，一旦连接 OpenOCD，每隔 1000 毫秒就会在 GUI 渲染主线程中同步发起 `ocd_.GetRegs()` 或 `ocd_.ReadMem32()` 请求。这会产生数十毫秒到数百毫秒不等的阻塞等待，直接引起主线程丢帧，使打字操作缓冲延迟。
2.  **大文本无裁剪渲染负载**：
    终端和串口日志采用 `ImGui::TextUnformatted` 对数十万字符的字符串进行整体渲染。在 Dear ImGui 中，大段文本的行宽量测、换行排版和顶点生成极其消耗 CPU，导致单帧渲染耗时飙升，产生明显的性能衰减。

### 1.2 文本不可复制问题
目前日志使用的是 `TextUnformatted`，ImGui 默认不提供鼠标拖拽选中和剪贴板复制能力。

### 1.3 输入框颜色过浅
`ImGuiCol_FrameBg` 被设为 `(0.08f, 0.10f, 0.12f)`，与窗口背景 `(0.06f, 0.07f, 0.09f)` 仅有 2% 的亮度差，且未开启边框（`FrameBorderSize = 0`），导致用户极难从视觉上识别出可输入区域。

---

## 2. 优化方案设计

### 2.1 方案一：`OcdClient` 异步任务队列线程 (Serial Task Worker)
为了确保不发生多线程竞争套接字导致崩溃，在 `OcdClient` 内部引入一个独立的**串行工作线程**。

*   **工作线程设计**：
    *   内部使用 `std::thread` 运行一个不断轮询 `std::queue<std::function<void()>>` 的工作循环。
    *   使用 `std::mutex` 和 `std::condition_variable` 进行任务同步，主线程投递任务后立即返回，从不阻塞。
*   **异步 API 接口**：
    *   主线程调用 `ConnectAsync`、`DisconnectAsync`、`HaltAsync`、`ResumeAsync`、`TriggerRefreshRegs`、`TriggerReadMem32` 来投递通信任务。
    *   提供线程安全的 `FetchNewRegs(out_regs)` 和 `GetCachedMemValue(addr, out_val)`，主线程直接读取被互斥锁保护的缓存数据。
*   **静默强杀 OpenOCD 进程**：
    *   增加 `KillProcessSilently` 辅助函数。在 Windows 平台下，通过 `CreateProcessA` 传入 `CREATE_NO_WINDOW` 标记静默执行 `taskkill /F /IM openocd.exe`，避免弹出黑窗口破坏用户体验。

### 2.2 方案二：只读多行文本输入框剥离样式 (实现文本局部复制)
*   **组件替换**：将日志显示区域替换为 `ImGui::InputTextMultiline`，并设置 `ImGuiInputTextFlags_ReadOnly`。
*   **视觉剥离**：利用 `PushStyleColor` 将 `ImGuiCol_FrameBg` 设为完全透明 `(0,0,0,0)`，并将 `FrameBorderSize` 设为 `0`，使其视觉效果完全等同于平铺文本。
*   **自动滚屏**：为 `InputTextMultiline` 配置 `ImGuiInputTextCallback`。当收到新日志且开启 `Auto-scroll` 时，将光标位置和选择区域强行移动到文本末尾，引导 ImGui 自动滚屏。

### 2.3 方案三：UI 对比度与精致边框提升
*   **亮度对比**：在 `gui_layer.cpp` 中将 `ImGuiCol_FrameBg` 的颜色调亮到 `(0.13f, 0.16f, 0.20f)`，增加 Hover 和 Active 颜色反馈。
*   **科技感边框**：设置 `style.FrameBorderSize = 1.0f`，并自定义 `ImGuiCol_Border` 为优雅的蓝灰色 `(0.22f, 0.28f, 0.35f, 0.85f)`。

---

## 3. 影响范围与回归测试

| 模块 | 修改内容 | 潜在风险 | 规避措施 |
| :--- | :--- | :--- | :--- |
| **`OcdClient`** | 增加 `std::thread` 工作循环，重构通信函数 | 析构时线程挂起、主线程卡死 | 析构函数中主动关闭 Socket、唤醒条件变量，调用 `join` |
| **`RegisterPanel`** | 对接异步获取与强杀按键 | 状态更新不及时 | 引入 Dirty 标记与 Dirty 数据 Fetch 接口 |
| **`VariablePanel`** | 异步读取 favorite 内存变量 | 变量刷新延迟 | 内存读取结果缓存至全局 Map |
| **`GuiLayer` & Panels** | 调整配色、边框、替换 Multiline 输入框 | 终端日志自动滚动失效 | 在回调中通过设置 `CursorPos` 强行滚动 |
