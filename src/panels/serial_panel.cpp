#include "serial_panel.h"
#include "../shared_state.h"
#include "imgui.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cctype>

SerialPanel::SerialPanel(SharedState& s) : Panel(s), macros_(kMacroCount) {
    // Initialize default macro presets
    for (int i = 0; i < kMacroCount; ++i) {
        snprintf(macros_[i].label, sizeof(macros_[i].label), "M%d", i + 1);
        snprintf(macros_[i].content, sizeof(macros_[i].content), "Command %d", i + 1);
    }

    // Load configs (serial port, baudrate, custom macros)
    LoadConfig();

    // Scan for physical COM ports on startup
    available_ports_ = ScanPorts();

    // Auto-select first available port if none is loaded or loaded port is empty
    if (strlen(selected_port_) == 0 && !available_ports_.empty()) {
        strncpy(selected_port_, available_ports_[0].c_str(), sizeof(selected_port_) - 1);
    }
}

SerialPanel::~SerialPanel() {
    ClosePort();
}

std::vector<std::string> SerialPanel::ScanPorts() {
    std::vector<std::string> ports;
    char devices[65536];
    // QueryDosDeviceA returns all MS-DOS device names when the first parameter is NULL
    DWORD len = QueryDosDeviceA(NULL, devices, sizeof(devices));
    if (len > 0) {
        for (char* p = devices; *p; p += strlen(p) + 1) {
            std::string name(p);
            // We are looking for COM ports (e.g., COM1, COM2, etc.)
            if (name.rfind("COM", 0) == 0) {
                ports.push_back(name);
            }
        }
    }

    // Sort COM ports numerically (so COM2 comes before COM10)
    std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b) {
        if (a.length() >= 4 && b.length() >= 4) {
            try {
                int numA = std::stoi(a.substr(3));
                int numB = std::stoi(b.substr(3));
                return numA < numB;
            } catch (...) {}
        }
        return a < b;
    });

    return ports;
}

bool SerialPanel::OpenPort(const std::string& port_name, int baudrate) {
    if (is_open_) {
        ClosePort();
    }

    // Larger COM port numbers (>= 10) require the \\.\ prefix in Windows API
    std::string full_path = "\\\\.\\" + port_name;

    // Open port in Overlapped (asynchronous) mode
    hComm_ = CreateFileA(
        full_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, // Exclusive access
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hComm_ == INVALID_HANDLE_VALUE) {
        rx_display_text_ += "\n[ERROR] Failed to open port " + port_name + " (Error: " + std::to_string(GetLastError()) + ")\n";
        return false;
    }

    // Configure COM buffers (1MB RX and TX buffer to prevent any overrun under extreme traffic)
    SetupComm(hComm_, 1024 * 1024, 1024 * 1024);

    // Initialize DCB (Device Control Block)
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hComm_, &dcb)) {
        rx_display_text_ += "\n[ERROR] Failed to get serial state.\n";
        CloseHandle(hComm_);
        hComm_ = INVALID_HANDLE_VALUE;
        return false;
    }

    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(hComm_, &dcb)) {
        rx_display_text_ += "\n[ERROR] Failed to set serial parameters (Baudrate/DataBits).\n";
        CloseHandle(hComm_);
        hComm_ = INVALID_HANDLE_VALUE;
        return false;
    }

    // Configure Timeout - Non-blocking asynchronous configuration
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD; // Return immediately
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(hComm_, &timeouts)) {
        rx_display_text_ += "\n[ERROR] Failed to set serial timeouts.\n";
        CloseHandle(hComm_);
        hComm_ = INVALID_HANDLE_VALUE;
        return false;
    }

    // Clear initial buffer junk
    PurgeComm(hComm_, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

    // Start background threads
    thread_running_ = true;
    read_thread_ = std::thread(&SerialPanel::ThreadReadLoop, this);
    write_thread_ = std::thread(&SerialPanel::ThreadWriteLoop, this);



    is_open_ = true;
    rx_display_text_ += "\n[SYSTEM] Successfully opened port " + port_name + " @ " + std::to_string(baudrate) + " bps\n";

    return true;
}

void SerialPanel::ClosePort() {
    if (!is_open_) return;

    thread_running_ = false;

    // Notify write thread to exit
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
    }
    tx_cv_.notify_all();

    // Cancel all pending I/O on the handle and close
    if (hComm_ != INVALID_HANDLE_VALUE) {
        CancelIo(hComm_);
        CloseHandle(hComm_);
        hComm_ = INVALID_HANDLE_VALUE;
    }

    // Join IO threads
    if (read_thread_.joinable()) read_thread_.join();
    if (write_thread_.joinable()) write_thread_.join();

    // Clean up TX queue
    std::queue<std::vector<uint8_t>> empty_q;
    std::swap(tx_queue_, empty_q);

    is_open_ = false;
    rx_display_text_ += "\n[SYSTEM] Port closed.\n";
}

void SerialPanel::SendData(const std::vector<uint8_t>& data) {
    if (!is_open_ || data.empty()) return;

    std::lock_guard<std::mutex> lock(tx_mutex_);
    tx_queue_.push(data);
    tx_cv_.notify_one();
}

void SerialPanel::SendDataString(const std::string& str, bool is_hex) {
    if (!is_open_ || str.empty()) return;

    std::vector<uint8_t> bytes;
    if (is_hex) {
        bytes = HexStringToBytes(str);
        if (bytes.empty()) {
            rx_display_text_ += "\n[ERROR] Invalid HEX String sequence.\n";
            return;
        }
    } else {
        bytes.assign(str.begin(), str.end());
    }

    // Print TX echo in the console log
    std::stringstream ss;
    if (show_timestamp_) {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
        std::tm tm = *std::localtime(&now_time_t);
        ss << "[" << std::put_time(&tm, "%H:%M:%S") << "." 
           << std::setfill('0') << std::setw(6) << now_ms.count() << "] TX -> ";
    } else {
        ss << "TX -> ";
    }

    if (is_hex) {
        ss << BytesToHexString(bytes.data(), bytes.size()) << " [HEX]";
    } else {
        ss << str;
    }
    ss << "\n";
    rx_display_text_ += ss.str();

    // Push into async tx queue
    SendData(bytes);
}

void SerialPanel::ThreadReadLoop() {
    // Elevate Read thread priority internally to guarantee zero-drop performance
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    OVERLAPPED ovRead = {0};
    ovRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ovRead.hEvent) return;

    std::vector<uint8_t> local_buf(8192);

    while (thread_running_) {
        ResetEvent(ovRead.hEvent);
        DWORD bytes_read = 0;
        BOOL success = ReadFile(hComm_, local_buf.data(), (DWORD)local_buf.size(), &bytes_read, &ovRead);

        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait for async read completion up to 50ms
                DWORD wait_res = WaitForSingleObject(ovRead.hEvent, 50);
                if (wait_res == WAIT_OBJECT_0) {
                    if (GetOverlappedResult(hComm_, &ovRead, &bytes_read, FALSE)) {
                        // Successfully read async
                    }
                } else if (wait_res == WAIT_TIMEOUT) {
                    // Timeout is natural when no data is received, just loop again
                    continue;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
            } else {
                // Hardware disconnection or generic port error
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }

        if (bytes_read > 0) {
            rx_count_ += bytes_read;
            std::lock_guard<std::mutex> lock(rx_mutex_);
            rx_raw_buffer_.insert(rx_raw_buffer_.end(), local_buf.begin(), local_buf.begin() + bytes_read);
        } else {
            // Under Immediate-return timeout settings, we sleep slightly to eliminate 100% CPU spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Cancel pending IO operations
    CancelIo(hComm_);
    CloseHandle(ovRead.hEvent);
}

void SerialPanel::ThreadWriteLoop() {
    OVERLAPPED ovWrite = {0};
    ovWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ovWrite.hEvent) return;

    while (thread_running_) {
        std::vector<uint8_t> local_tx_buf;
        {
            std::unique_lock<std::mutex> lock(tx_mutex_);
            tx_cv_.wait(lock, [this]() { return !tx_queue_.empty() || !thread_running_; });
            if (!thread_running_ && tx_queue_.empty()) {
                break;
            }
            if (!tx_queue_.empty()) {
                local_tx_buf = std::move(tx_queue_.front());
                tx_queue_.pop();
            }
        }

        if (local_tx_buf.empty()) continue;

        ResetEvent(ovWrite.hEvent);
        DWORD bytes_written = 0;
        BOOL success = WriteFile(hComm_, local_tx_buf.data(), (DWORD)local_tx_buf.size(), &bytes_written, &ovWrite);

        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait indefinitely until transmission completes
                DWORD wait_res = WaitForSingleObject(ovWrite.hEvent, INFINITE);
                if (wait_res == WAIT_OBJECT_0) {
                    GetOverlappedResult(hComm_, &ovWrite, &bytes_written, FALSE);
                }
            }
        }

        if (bytes_written > 0) {
            tx_count_ += bytes_written;
        }
    }

    CloseHandle(ovWrite.hEvent);
}

void SerialPanel::ProcessRxData() {
    std::vector<uint8_t> local_rx;
    {
        std::lock_guard<std::mutex> lock(rx_mutex_);
        if (!rx_raw_buffer_.empty()) {
            local_rx = std::move(rx_raw_buffer_);
            rx_raw_buffer_.clear();
        }
    }

    if (local_rx.empty()) return;

    if (!paused_) {
        std::string formatted = FormatBytes(local_rx.data(), local_rx.size(), show_hex_, show_timestamp_);
        rx_display_text_ += formatted;

        // Prevent console logs from leaking memory, keep max 200,000 chars
        if (rx_display_text_.size() > 200000) {
            rx_display_text_.erase(0, rx_display_text_.size() - 100000);
        }
    }
}

std::string SerialPanel::FormatBytes(const uint8_t* data, size_t size, bool hex, bool show_ts) {
    std::stringstream ss;
    if (show_ts) {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
        std::tm tm = *std::localtime(&now_time_t);
        ss << "[" << std::put_time(&tm, "%H:%M:%S") << "." 
           << std::setfill('0') << std::setw(6) << now_ms.count() << "] RX -> ";
    }

    if (hex) {
        ss << BytesToHexString(data, size) << " ";
    } else {
        // Guard against breaking UTF8 panels or printing binary raw controls
        for (size_t i = 0; i < size; ++i) {
            uint8_t c = data[i];
            if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c <= 126)) {
                ss << (char)c;
            } else {
                ss << "."; // Neutral placeholder for raw bytes
            }
        }
    }

    // Append trailing newline for event-like packages to ensure layout symmetry
    if (show_ts || hex) {
        ss << "\n";
    }

    return ss.str();
}

std::vector<uint8_t> SerialPanel::HexStringToBytes(const std::string& hex_str) {
    std::vector<uint8_t> bytes;
    std::string cleaned;
    for (char c : hex_str) {
        if (std::isxdigit(c)) cleaned += c;
    }

    if (cleaned.length() % 2 != 0) {
        // If odd character count, ignore the last trailing digit
        cleaned.pop_back();
    }

    for (size_t i = 0; i < cleaned.length(); i += 2) {
        std::string byte_str = cleaned.substr(i, 2);
        uint8_t byte = (uint8_t)std::strtol(byte_str.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

std::string SerialPanel::BytesToHexString(const uint8_t* data, size_t size) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) {
        ss << std::setw(2) << (int)data[i] << " ";
    }
    std::string res = ss.str();
    if (!res.empty()) res.pop_back(); // Remove trailing space
    // Convert to upper case
    std::transform(res.begin(), res.end(), res.begin(), ::toupper);
    return res;
}

std::string SerialPanel::SaveLogFileDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Log Files\0*.log\0Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "log";
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
#endif
    return "";
}

void SerialPanel::LoadConfig() {
    std::ifstream file(kConfigFile);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "serial_port") {
            strncpy(selected_port_, val.c_str(), sizeof(selected_port_) - 1);
        } else if (key == "serial_baudrate") {
            try {
                selected_baudrate_ = std::stoi(val);
            } catch (...) {}
        } else if (key.rfind("serial_macro_label_", 0) == 0) {
            int idx = std::stoi(key.substr(19));
            if (idx >= 0 && idx < kMacroCount) {
                strncpy(macros_[idx].label, val.c_str(), sizeof(macros_[idx].label) - 1);
            }
        } else if (key.rfind("serial_macro_content_", 0) == 0) {
            int idx = std::stoi(key.substr(21));
            if (idx >= 0 && idx < kMacroCount) {
                strncpy(macros_[idx].content, val.c_str(), sizeof(macros_[idx].content) - 1);
            }
        }
    }
}

void SerialPanel::SaveConfig() {
    // 1. Read existing configuration to prevent erasing unrelated configs
    std::map<std::string, std::string> configs;
    {
        std::ifstream file(kConfigFile);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.empty()) continue;
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                configs[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }

    // 2. Overwrite / Update serial properties
    configs["serial_port"] = selected_port_;
    configs["serial_baudrate"] = std::to_string(selected_baudrate_);

    for (int i = 0; i < kMacroCount; ++i) {
        configs["serial_macro_label_" + std::to_string(i)] = macros_[i].label;
        configs["serial_macro_content_" + std::to_string(i)] = macros_[i].content;
    }

    // 3. Write back complete configs safely
    std::ofstream file(kConfigFile, std::ios::out | std::ios::trunc);
    if (file.is_open()) {
        for (const auto& pair : configs) {
            file << pair.first << "=" << pair.second << "\n";
        }
        file.close();
    }
}

void SerialPanel::Render() {
    // First, process any incoming physical serial buffers
    ProcessRxData();

    ImGui::Begin("Serial Console");

    // ==========================================
    // TOP BAR: Connection parameters
    // ==========================================
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Port:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);

    if (ImGui::BeginCombo("##Ports", selected_port_)) {
        for (const auto& port : available_ports_) {
            bool is_selected = (strcmp(selected_port_, port.c_str()) == 0);
            if (ImGui::Selectable(port.c_str(), is_selected)) {
                strncpy(selected_port_, port.c_str(), sizeof(selected_port_) - 1);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Text("Baud:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);

    const int baudrates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1500000, 2000000};
    const int baud_count = sizeof(baudrates) / sizeof(baudrates[0]);
    char baud_preview[32];
    snprintf(baud_preview, sizeof(baud_preview), "%d", selected_baudrate_);

    if (ImGui::BeginCombo("##Bauds", baud_preview)) {
        for (int i = 0; i < baud_count; ++i) {
            bool is_selected = (selected_baudrate_ == baudrates[i]);
            char item_name[32];
            snprintf(item_name, sizeof(item_name), "%d", baudrates[i]);
            if (ImGui::Selectable(item_name, is_selected)) {
                selected_baudrate_ = baudrates[i];
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();

    // Toggle Port state
    if (is_open_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Close Port", ImVec2(100, 0))) {
            ClosePort();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
        if (ImGui::Button("Open Port", ImVec2(100, 0))) {
            OpenPort(selected_port_, selected_baudrate_);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();
    if (ImGui::Button("Scan Ports")) {
        available_ports_ = ScanPorts();
        if (!available_ports_.empty() && strlen(selected_port_) == 0) {
            strncpy(selected_port_, available_ports_[0].c_str(), sizeof(selected_port_) - 1);
        }
    }

    ImGui::Separator();

    // ==========================================
    // COLUMNS: Left (Console) & Right (Macros)
    // ==========================================
    ImGui::Columns(2, "SerialColumns", true);

    // Set proportional initial widths: 70% console, 30% macro panels
    static bool first_run = true;
    if (first_run) {
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.68f);
        first_run = false;
    }

    // ------------------------------------------
    // LEFT COLUMN: Console and TX Transmitter
    // ------------------------------------------
    ImGui::Checkbox("HEX RX", &show_hex_);
    ImGui::SameLine();
    ImGui::Checkbox("Timestamp", &show_timestamp_);
    ImGui::SameLine();
    ImGui::Checkbox("AutoScroll", &auto_scroll_);
    ImGui::SameLine();
    ImGui::Checkbox("Pause RX", &paused_);

    ImGui::SameLine();
    if (ImGui::Button("Clear RX")) {
        rx_display_text_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Counter")) {
        rx_count_ = 0;
        tx_count_ = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Log")) {
        std::string path = SaveLogFileDialog();
        if (!path.empty()) {
            std::ofstream log_file(path, std::ios::out | std::ios::binary);
            if (log_file.is_open()) {
                log_file.write(rx_display_text_.c_str(), rx_display_text_.size());
                log_file.close();
            }
        }
    }

    // Rx Display console
    ImGui::BeginChild("RxFrame", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.5f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f)); // Vintage terminal green tint
    ImGui::TextUnformatted(rx_display_text_.c_str());
    ImGui::PopStyleColor();

    if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    // Tx Sender Area
    ImGui::Spacing();
    ImGui::Text("Transmitter:");
    ImGui::SameLine();
    ImGui::RadioButton("ASCII TX", &tx_hex_mode_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("HEX TX", &tx_hex_mode_, 1);

    ImGui::SetNextItemWidth(-120.0f);
    bool enter_pressed = ImGui::InputText("##TxText", tx_input_buf_, sizeof(tx_input_buf_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    
    if (ImGui::Button("Send Packet", ImVec2(110, 0)) || enter_pressed) {
        SendDataString(tx_input_buf_, tx_hex_mode_ == 1);
        // Retain text for convenience but highlight
    }

    // Status strip
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Status: %s | RX Count: %zu Bytes | TX Count: %zu Bytes", 
                       is_open_ ? "CONNECTED" : "DISCONNECTED", rx_count_.load(), tx_count_.load());

    // ------------------------------------------
    // RIGHT COLUMN: 16 SSCOM-style Macro Panel
    // ------------------------------------------
    ImGui::NextColumn();

    ImGui::Text("Quick Macros (16 Presets)");
    ImGui::Separator();

    ImGui::BeginChild("MacrosFrame", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);
    for (int i = 0; i < kMacroCount; ++i) {
        ImGui::PushID(i);

        // 1. Label column (max width: 60)
        ImGui::SetNextItemWidth(60);
        ImGui::InputText("##M_Label", macros_[i].label, sizeof(macros_[i].label));
        
        ImGui::SameLine();

        // 2. Command input (flexible width)
        ImGui::SetNextItemWidth(ImGui::GetColumnWidth() - 170.0f);
        ImGui::InputText("##M_Cmd", macros_[i].content, sizeof(macros_[i].content));

        ImGui::SameLine();

        // 3. Send button
        if (ImGui::Button("Send", ImVec2(60, 0))) {
            SendDataString(macros_[i].content, tx_hex_mode_ == 1);
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    // Persist Macros button
    if (ImGui::Button("Save Macros & Config", ImVec2(-FLT_MIN, 0))) {
        SaveConfig();
    }

    ImGui::Columns(1); // Restore columns
    ImGui::End();
}
