# Version Info

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
