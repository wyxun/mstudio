# Version Info

## V0.6.0.0
- **Release Date:** 2026-08-07
- **Updates:**
  - **Waveform V2/V2.1 协议支持**：
    - 支持 `AA 55 FC` 批量帧、`AA 55 FA` snapshot 帧和 `AA 55 FE` 采样周期元数据。
    - 支持每通道/每变量独立刷新率，同一流中混合 10 kHz 与 1 kHz 通道。
    - 支持 MCU 上报采样周期，Dashboard 同时显示 `Actual` 与 `MCU` 采样率。
  - **波形渲染与交互**：
    - 修复 X 轴缩放被实时滚动覆盖的问题。
    - 新增 `TestSin Only` / `Show All` 验证视图，`TestSin Only` 自动切换到 0.1s 窗口。
    - 密集数据改为逐像素 min/max 包络绘制，避免伪锯齿。
    - 修复 batch 边界时间戳重叠导致的同一 X 轴双点问题。
  - **稳定性与构建**：
    - 修复 MStudio 启动闪退。
    - Makefile 增加 `-MMD -MP` 头文件依赖追踪。
    - 更新协议解析器以支持 descriptor/meta/batch/snapshot 帧和 CRC16。

## V0.5.0.4
- **Release Date:** 2026-07-25
- **Updates:**
  - **动态追踪与高精细节分析**：重构波形渲染逻辑，Roll/Run 模式下支持 60FPS 无卡顿平滑实时追踪最新数据（Id/Iq/速度/角度）；鼠标拖拽或点击 Pause 可无缝定格并进入 Free 模式，最小支持 **1ms** 级别采样点微观分析。
  - **单轴/双轴独立放缩与交互优化**：
    - 兼容按住 Shift 键时系统将滚轮重定向为 `MouseWheelH` 的问题，支持独占 X 轴与 Y 轴缩放。
    - Dashboard 新增 **Fit Y** 按钮，支持一键将 Y 轴幅度自适应至波形峰峰值。
  - **数据层与架构稳定性提升**：
    - 新增断线重连清空积压缓冲、僵尸 Channel 自动擦除。
    - 引入可变权重 EMA 采样率估算器与 Resume Warmup 保护，解决暂停恢复后采样率估算需要十几秒收敛的滞后问题。
    - 在 `PlotLineG` 闭包中加入安全边界保护，重构 ImPlot Setup 调用规范，消除 API 阶段锁定断言。
    - Makefile 增加头文件依赖规则，保证内存结构体定义更新时全量编译同步。
  - **OcdClient 同步 API 导出**：为 `OcdClient` 新增 `ConnectSync` / `SendCommandSync` / `GetRegsSync` / `ReadMem32Sync` / `ReadMemBlock32Sync` 同步接口，aitrace CLI 不再依赖异步任务队列，驱动方式更符合命令行一次性调用模式。
  - **aitrace shell 命令重构**：
    - 新增 `--raw` flag 控制是否过滤固件 `[T]` 级日志行，默认过滤心跳/TRACE 噪声。
    - 发送命令前 400ms RTT 积压排泄窗口，避免陈旧 prompt 残留导致提前终止。
    - 双超时终止策略：提示符 `\n> ` 快速返回 + 600ms 空闲超时 + 5s 硬上限。
  - **aitrace wave stat 链路质量统计**：新增 `wave stat [seconds]` 子命令，每秒一行实时输出 `rate (f/s) / crc_err / seq_lost`，含 2s 静默预热解析至首个描述帧以学习掩码长度，并修复序号 0xFD 保留值引起的隔帧判定失误。

## V0.5.0.3
- **Release Date:** 2026-07-25
- **Updates:**
  - 修复协议解析器的组帧错误：数据帧掩码长度改为从通道描述符的通道数量推导（`(ch_count + 7) / 8`），不再依赖编译期构造值，避免两者不一致时所有数据帧解析错位。
  - 修复描述符帧 CRC 校验失败时的重同步策略：由擦除整个描述符长度改为逐字节滑动重同步，避免误吞后续数据帧（例如 seq 字节恰好为 0xFD 的数据帧）。
  - 窗口标题版本号更新为 V0.5.0.3，同步重新编译 `core/build/libmstudiocore.a`。

## V0.5.0.1
- **Release Date:** 2026-06-03
- **Updates:**
  - 允许 Dashboard 中的 Quick User Macros 按钮根据窗口宽度自动换行排列。
  - 在 Makefile 中默认开启 `USE_ICON` 编译选项，使生成的可执行文件携带图标。
