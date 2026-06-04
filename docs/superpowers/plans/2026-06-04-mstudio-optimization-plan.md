# MStudio 性能卡顿与 UI 样式优化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 优化 MStudio 与 OpenOCD 的通信机制为后台线程异步模式，提升输入框对比度与边框识别度，并使日志文本框支持选择与复制。

**Architecture:** 在 OcdClient 中引入单线程任务队列，串行处理阻塞式 OpenOCD 通信任务。UI 主线程通过脏标记和缓存线程安全地拉取数据。日志渲染由 TextUnformatted 替换为只读且剥离样式的 InputTextMultiline，并利用回调控制自动滚动。

**Tech Stack:** C++11 (std::thread, std::mutex, std::condition_variable), Dear ImGui, SDL2, Win32 API.

---

### Task 1: OcdClient 类结构重构与异步引擎搭建

**Files:**
- Modify: `src/utils/ocd_client.h`
- Modify: `src/utils/ocd_client.cpp`

- [ ] **Step 1: 重构 `ocd_client.h`**
  引入线程、互斥锁、条件变量、任务队列及缓存，将原有通信接口改为 Async 后缀，并将原同步阻塞接口标记为内部私有接口 `Internal`。
  
  在 `src/utils/ocd_client.h` 中修改内容为：
  ```cpp
  #ifndef OCD_CLIENT_H
  #define OCD_CLIENT_H
  
  #include <string>
  #include <vector>
  #include <map>
  #include <queue>
  #include <thread>
  #include <mutex>
  #include <condition_variable>
  #include <atomic>
  #include <functional>
  #include <cstdint>
  
  #ifdef _WIN32
  #include <winsock2.h>
  #else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define SOCKET int
  #define INVALID_SOCKET (-1)
  #define closesocket close
  #endif
  
  struct RegEntry {
      std::string name;
      uint32_t    value;
      int         bits;
  };
  
  class OcdClient {
  public:
      OcdClient();
      ~OcdClient();
  
      void ConnectAsync(const std::string& host = "127.0.0.1", int port = 4444);
      void DisconnectAsync();
      
      bool IsConnected();
      bool IsConnecting();
  
      void HaltAsync();
      void ResumeAsync();
      void TriggerRefreshRegs();
      void TriggerReadMem32(uint32_t addr);
  
      bool FetchNewRegs(std::vector<RegEntry>& out_regs);
      bool GetCachedMemValue(uint32_t addr, uint32_t& out_val);
  
  private:
      bool ConnectInternal(const char* host, int port);
      void DisconnectInternal();
      bool HaltInternal();
      bool ResumeInternal();
      std::vector<RegEntry> GetRegsInternal();
      uint32_t ReadMem32Internal(uint32_t addr);
  
      std::thread worker_thread_;
      std::mutex queue_mutex_;
      std::condition_variable queue_cv_;
      std::queue<std::function<void()>> task_queue_;
      std::atomic<bool> running_{true};
  
      enum class State {
          Disconnected,
          Connecting,
          Connected
      };
      std::atomic<State> state_{State::Disconnected};
  
      std::mutex regs_mutex_;
      std::vector<RegEntry> cached_regs_;
      bool regs_dirty_ = false;
  
      std::mutex mem_mutex_;
      std::map<uint32_t, uint32_t> cached_mem_;
  
      SOCKET sock_ = INVALID_SOCKET;
      std::string host_ = "127.0.0.1";
      int port_ = 4444;
  
      void WorkerLoop();
      void PushTask(std::function<void()> task);
  
      std::string StripTelnet(const std::string& data);
      std::string RecvUntilTimeout(int timeout_ms = 300);
      void DrainPending();
  };
  
  #endif // OCD_CLIENT_H
  ```

- [ ] **Step 2: 在 `ocd_client.cpp` 中重构原有同步通信函数为 Internal 命名**
  重命名原 `Connect`, `Disconnect`, `Halt`, `Resume`, `GetRegs`, `ReadMem32` 为 `Internal` 版本。
  修改 `src/utils/ocd_client.cpp`：
  将 `bool OcdClient::Connect` 改为 `bool OcdClient::ConnectInternal`；
  将 `void OcdClient::Disconnect` 改为 `void OcdClient::DisconnectInternal`；
  将 `bool OcdClient::Halt` 改为 `bool OcdClient::HaltInternal`；
  将 `bool OcdClient::Resume` 改为 `bool OcdClient::ResumeInternal`；
  将 `std::vector<RegEntry> OcdClient::GetRegs` 改为 `std::vector<RegEntry> OcdClient::GetRegsInternal`；
  将 `uint32_t OcdClient::ReadMem32` 改为 `uint32_t OcdClient::ReadMem32Internal`。

- [ ] **Step 3: 运行本地编译以确保类声明修改无语法错误**
  运行：`.\build.bat`
  预期输出：编译失败，报 panels 中找不到旧同步接口的错误（这证明头文件修改生效，下一步将实现这些新接口）。

---

### Task 2: OcdClient 异步多线程引擎与强杀进程实现

**Files:**
- Modify: `src/utils/ocd_client.cpp`

- [ ] **Step 1: 实现 OcdClient 的构造与析构函数，启动/停止后台工作线程**
  在 `src/utils/ocd_client.cpp` 的文件开头部分写入构造与析构的逻辑：
  ```cpp
  OcdClient::OcdClient() {
      worker_thread_ = std::thread(&OcdClient::WorkerLoop, this);
  }
  
  OcdClient::~OcdClient() {
      running_ = false;
      {
          std::lock_guard<std::mutex> lock(queue_mutex_);
      }
      queue_cv_.notify_all();
      
      // 主线程关闭套接字强制唤醒可能处于阻塞状态的后台 select/recv
      DisconnectInternal();
      
      if (worker_thread_.joinable()) {
          worker_thread_.join();
      }
  }
  ```

- [ ] **Step 2: 实现后台线程循环 `WorkerLoop` 与任务推送 `PushTask`**
  ```cpp
  void OcdClient::PushTask(std::function<void()> task) {
      {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          task_queue_.push(task);
      }
      queue_cv_.notify_one();
  }
  
  void OcdClient::WorkerLoop() {
      while (running_) {
          std::function<void()> task;
          {
              std::unique_lock<std::mutex> lock(queue_mutex_);
              queue_cv_.wait(lock, [this]() { return !task_queue_.empty() || !running_; });
              if (!running_) break;
              task = std::move(task_queue_.front());
              task_queue_.pop();
          }
          if (task) {
              task();
          }
      }
  }
  ```

- [ ] **Step 3: 实现主线程调用的非阻塞 Async 异步接口**
  在 `src/utils/ocd_client.cpp` 尾部实现所有异步操作方法：
  ```cpp
  void OcdClient::ConnectAsync(const std::string& host, int port) {
      if (state_ != State::Disconnected) return;
      state_ = State::Connecting;
      host_ = host;
      port_ = port;
      PushTask([this]() {
          bool ok = ConnectInternal(host_.c_str(), port_);
          state_ = ok ? State::Connected : State::Disconnected;
      });
  }
  
  void OcdClient::DisconnectAsync() {
      PushTask([this]() {
          DisconnectInternal();
          state_ = State::Disconnected;
      });
  }
  
  bool OcdClient::IsConnected() {
      return state_ == State::Connected;
  }
  
  bool OcdClient::IsConnecting() {
      return state_ == State::Connecting;
  }
  
  void OcdClient::HaltAsync() {
      PushTask([this]() {
          HaltInternal();
      });
  }
  
  void OcdClient::ResumeAsync() {
      PushTask([this]() {
          ResumeInternal();
      });
  }
  
  void OcdClient::TriggerRefreshRegs() {
      PushTask([this]() {
          auto regs = GetRegsInternal();
          std::lock_guard<std::mutex> lock(regs_mutex_);
          cached_regs_ = std::move(regs);
          regs_dirty_ = true;
      });
  }
  
  void OcdClient::TriggerReadMem32(uint32_t addr) {
      PushTask([this, addr]() {
          uint32_t val = ReadMem32Internal(addr);
          std::lock_guard<std::mutex> lock(mem_mutex_);
          cached_mem_[addr] = val;
      });
  }
  ```

- [ ] **Step 4: 实现线程安全的数据拉取 Getter**
  ```cpp
  bool OcdClient::FetchNewRegs(std::vector<RegEntry>& out_regs) {
      std::lock_guard<std::mutex> lock(regs_mutex_);
      if (!regs_dirty_) return false;
      out_regs = cached_regs_;
      regs_dirty_ = false;
      return true;
  }
  
  bool OcdClient::GetCachedMemValue(uint32_t addr, uint32_t& out_val) {
      std::lock_guard<std::mutex> lock(mem_mutex_);
      auto it = cached_mem_.find(addr);
      if (it == cached_mem_.end()) return false;
      out_val = it->second;
      return true;
  }
  ```

- [ ] **Step 5: 运行本地编译**
  运行：`.\build.bat`
  预期输出：编译仍报 panels 内调用错误，但 `ocd_client.cpp` 已编译通过。

---

### Task 3: 寄存器面板适配异步化与 OpenOCD 静默强杀按钮

**Files:**
- Modify: `src/panels/register_panel.cpp`

- [ ] **Step 1: 写入静默强杀辅助函数**
  在 `src/panels/register_panel.cpp` 的头文件引用区下方写入 `KillProcessSilently`：
  ```cpp
  #ifdef _WIN32
  #include <windows.h>
  static void KillProcessSilently(const char* process_name) {
      char cmd[256];
      snprintf(cmd, sizeof(cmd), "cmd.exe /c taskkill /F /IM %s >nul 2>&1", process_name);
      STARTUPINFOA si = { sizeof(si) };
      si.dwFlags = STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_HIDE;
      PROCESS_INFORMATION pi = {};
      BOOL success = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
      if (success) {
          WaitForSingleObject(pi.hProcess, 1000);
          CloseHandle(pi.hProcess);
          CloseHandle(pi.hThread);
      }
  }
  #else
  #include <cstdlib>
  static void KillProcessSilently(const char* process_name) {
      char cmd[256];
      snprintf(cmd, sizeof(cmd), "killall -9 %s >/dev/null 2>&1", process_name);
      std::system(cmd);
  }
  #endif
  ```

- [ ] **Step 2: 修改 `RegisterPanel::Render()` 为异步调用并添加 Kill 按钮**
  定位到 `src/panels/register_panel.cpp` 第 186 行至第 230 行附近，替换同步连接和刷新逻辑：
  ```cpp
      // Connection bar
      if (!ocd_.IsConnected() && !ocd_.IsConnecting()) {
          if (ImGui::Button("Connect to OpenOCD")) {
              ocd_.ConnectAsync();
          }
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
          if (ImGui::Button("Kill OpenOCD")) {
              KillProcessSilently("openocd.exe");
          }
          ImGui::PopStyleColor(2);
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Disconnected");
      } else if (ocd_.IsConnecting()) {
          ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Connecting...");
          ImGui::SameLine();
          if (ImGui::Button("Cancel")) {
              ocd_.DisconnectAsync();
          }
      } else {
          ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Connected");
  
          ImGui::SameLine();
          if (ImGui::Button("Disconnect")) {
              ocd_.DisconnectAsync();
              connected_ = false;
              regs_.clear();
          }
  
          ImGui::SameLine();
          if (ImGui::Button("Halt & Read")) {
              ocd_.HaltAsync();
              ocd_.TriggerRefreshRegs();
          }
  
          ImGui::SameLine();
          if (ImGui::Button("Resume")) {
              ocd_.ResumeAsync();
          }
  
          ImGui::SameLine();
          if (ImGui::Button("Refresh")) {
              ocd_.TriggerRefreshRegs();
          }
  
          // Auto-refresh every 1s when connected
          auto now = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - last_refresh_).count();
          if (elapsed > 1000) {
              last_refresh_ = now;
              ocd_.TriggerRefreshRegs();
          }
      }
  
      ImGui::Separator();
  
      // Fetch latest regs
      std::vector<RegEntry> new_regs;
      if (ocd_.FetchNewRegs(new_regs)) {
          for (auto& r : regs_) {
              prev_vals_[r.name] = r.value;
          }
          regs_ = std::move(new_regs);
      }
  
      if (regs_.empty()) {
          ImGui::TextUnformatted("No register data. Connect to OpenOCD and refresh.");
      } else {
          show_groups = true;
      }
  ```
  同时彻底删除 `RefreshRegs()` 的实现代码或只清空其函数体。

- [ ] **Step 3: 运行本地编译**
  运行：`.\build.bat`
  预期输出：编译应该只在 `variable_panel.cpp` 处报错。

---

### Task 4: 变量面板适配异步化

**Files:**
- Modify: `src/panels/variable_panel.cpp`

- [ ] **Step 1: 修改 `VariablePanel::RefreshValues` 与 `ReadVarValue`**
  定位到 `src/panels/variable_panel.cpp` 的相应方法，将其改成向异步队列投递任务：
  ```cpp
  uint32_t VariablePanel::ReadVarValue(uint32_t addr, uint32_t size) {
      ocd_.TriggerReadMem32(addr);
      return 0; // 异步立即返回 0
  }
  
  void VariablePanel::RefreshValues() {
      last_refresh_ = std::chrono::steady_clock::now();
      for (auto* v : vars_) {
          if (favorites_.count(v->name)) {
              ocd_.TriggerReadMem32(v->address);
          }
      }
  }
  ```

- [ ] **Step 2: 在 `Render` 方法的循环中安全拷贝已缓存的值**
  在 `Render` 函数中更新已 favorites 变量的数值缓存，并将 Connect 替换为异步：
  修改：
  ```cpp
      if (!ocd_connected_) {
          if (ImGui::Button("Connect OCD")) {
              ocd_.ConnectAsync();
          }
  ```
  和：
  ```cpp
          if (ImGui::Button("Disconnect")) {
              ocd_.DisconnectAsync();
              ocd_connected_ = false;
          }
  ```
  在展示表格前批量把已缓存的数据更新到 UI 变量 Map 中：
  ```cpp
      // Fetch memory values from cache
      ocd_connected_ = ocd_.IsConnected();
      if (ocd_connected_) {
          for (auto* v : vars_) {
              uint32_t val = 0;
              if (ocd_.GetCachedMemValue(v->address, val)) {
                  if (v->size < 4) {
                      uint32_t mask = (v->size == 1) ? 0xFFu : 0xFFFFu;
                      val &= mask;
                  }
                  values_[v->address] = val;
              }
          }
      }
  ```

- [ ] **Step 3: 运行本地编译**
  运行：`.\build.bat`
  预期输出：[SUCCESS] Build complete。说明整个通信多线程改造已完全通过编译！

---

### Task 5: UI 视觉强化与日志 InputTextMultiline 样式剥离

**Files:**
- Modify: `src/gui_layer.cpp`
- Modify: `src/panels/terminal_panel.cpp`
- Modify: `src/panels/serial_panel.cpp`

- [ ] **Step 1: 修改主题，提升对比度并显示边框**
  在 `src/gui_layer.cpp` 中更新输入框样式：
  ```cpp
      colors[ImGuiCol_FrameBg]                = ImVec4(0.13f, 0.16f, 0.20f, 1.00f);
      colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
      colors[ImGuiCol_FrameBgActive]          = ImVec4(0.22f, 0.28f, 0.36f, 1.00f);
      // ... 保持原有不变 ...
      colors[ImGuiCol_Border]                 = ImVec4(0.22f, 0.28f, 0.35f, 0.85f);
  
      style.WindowRounding = 6.0f;
      style.FrameRounding = 4.0f;
      style.PopupRounding = 4.0f;
      style.GrabRounding = 4.0f;
      style.FrameBorderSize = 1.0f; // 开启 1 像素控件边框
  ```

- [ ] **Step 2: 重构 Shell 终端日志的渲染方式，实现文本可复制与自动滚动**
  在 `src/panels/terminal_panel.cpp` 文件中（可以放在 `TerminalPanel::Render()` 上方）写入回调函数：
  ```cpp
  static int LogInputCallback(ImGuiInputTextCallbackData* data) {
      bool* p_scroll = (bool*)data->UserData;
      if (p_scroll && *p_scroll) {
          data->CursorPos = data->BufTextLen;
          data->SelectionStart = data->BufTextLen;
          data->SelectionEnd = data->BufTextLen;
      }
      return 0;
  }
  ```
  然后修改 `ScrollingRegion` 内部的渲染逻辑：
  ```cpp
      const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  
      bool need_scroll = state.auto_scroll_;
  
      if (state.term_filter_.IsActive()) {
          std::string filtered_log;
          const char* line_start = state.term_log_.c_str();
          const char* log_end = line_start + state.term_log_.length();
          while (line_start < log_end) {
              const char* line_end = strchr(line_start, '\n');
              if (!line_end) line_end = log_end;
              if (state.term_filter_.PassFilter(line_start, line_end)) {
                  filtered_log.append(line_start, line_end - line_start);
                  filtered_log += "\n";
              }
              line_start = line_end + 1;
          }
          ImGui::InputTextMultiline("##term_log", (char*)filtered_log.c_str(), filtered_log.size(), 
                                    ImVec2(-1, -footer_height_to_reserve), 
                                    ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, 
                                    LogInputCallback, &need_scroll);
      } else {
          ImGui::InputTextMultiline("##term_log", (char*)state.term_log_.c_str(), state.term_log_.size(), 
                                    ImVec2(-1, -footer_height_to_reserve), 
                                    ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, 
                                    LogInputCallback, &need_scroll);
      }
  
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();
  ```

- [ ] **Step 3: 重构串口控制台日志渲染（`serial_panel.cpp`）**
  参照 Step 2 的方式，在 `src/panels/serial_panel.cpp` 中定义 `LogInputCallback` 回调。
  将 `RxFrame` 区域的 `ImGui::BeginChild` 与 `ImGui::TextUnformatted` 替换为无样式的 `ImGui::InputTextMultiline` 形式：
  ```cpp
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f)); // 经典终端绿
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  
      bool need_scroll = auto_scroll_;
  
      ImGui::InputTextMultiline("##rx_log", (char*)rx_display_text_.c_str(), rx_display_text_.size(), 
                                ImVec2(-1, rx_height), 
                                ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, 
                                LogInputCallback, &need_scroll);
  
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
  ```

- [ ] **Step 4: 执行最终编译**
  运行：`.\build.bat`
  预期输出：[SUCCESS] Build complete。整个重构和优化顺利结束。
