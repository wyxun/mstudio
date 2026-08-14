# mstudio-dsh 部署包

公司 → 家 的快速分发工具。

## 目录结构

```
部署包/
├── mstudio-dsh/            # @wx/mstudio-dsh 插件（lib 已编译）
├── kicad-auditor-dsh/      # @wx/kicad-auditor-dsh 插件（lib 已编译）
├── skills/                 # 仓库内 skill 副本
├── install.ps1             # 安装脚本（本机执行）
└── README.md
```

## 快速开始（家机）

```powershell
# 1. 解压部署包到任意目录，进入该目录
# 2. 运行安装脚本（自动探测路径、生成 patch、装 skill）
powershell -ExecutionPolicy Bypass -File .\install.ps1

# 若仓库不在默认兄弟位置，显式指定：
powershell -ExecutionPolicy Bypass -File .\install.ps1 `
    -MStudioDir D:\my\mstudio -KicadAuditorDir D:\my\kicad-auditor `
    -ModusTemplateDir D:\my\modus_template

# 3. 重启 DSH Web GUI → 新会话即可见全部工具
```

## 参数

| 参数 | 含义 |
|---|---|
| `-MStudioDir` | mstudio 仓库路径（不传则探测脚本兄弟目录） |
| `-KicadAuditorDir` | kicad-auditor 仓库路径 |
| `-ModusTemplateDir` | 固件工程路径（aitrace 的 workDir） |
| `-ProfileDir` | DSH profile 目录（默认 `~/.dsh/profiles/web`） |
| `-Build` | 用本地 deepseek-harness 源码重新构建插件（替代部署包内 lib） |
| `-SkipKicad` | 不安装 kicad 插件（家里暂时不做电路） |

## 两种模式

- **部署包模式（默认）**：zip 里 lib 已编译，直接装，无需任何构建环境。
- **源码模式（-Build）**：从 git pull 的源码 + 本机 deepseek-harness 重建，
  适合持续优化。

## 持续优化闭环

```
公司：改 dsh-integration 源码 → pnpm build → 验证 → git push
家：  git pull → 重跑 install.ps1（或只复制新 lib）→ 重启 DSH
```

## 家机还需要的环境（按需）

- **MSYS2 + g++**：构建 kicad-auditor.exe（`kicad-auditor\make.bat`）
- **KiCad 华秋版 9.x**：KiCad MCP（可选，连接见 docs/kicad-mcp-connect.md）
- **OpenOCD + 探针驱动**：MCU 调试（可选）
- **aitrace.exe**：mstudio 源码构建（`mstudio\build.bat`）或从公司复制
