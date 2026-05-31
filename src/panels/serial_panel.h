#ifndef SERIAL_PANEL_H
#define SERIAL_PANEL_H

#include "../panel_base.h"
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <string>
#include <atomic>

struct SerialMacro {
    char label[64] = {0};
    char content[256] = {0};
};

class SerialPanel : public Panel {
public:
    explicit SerialPanel(SharedState& s);
    ~SerialPanel() override;

    const char* Name() const override { return "Serial Console"; }
    void Render() override;

private:
    // Core physical port controls
    std::vector<std::string> ScanPorts();
    bool OpenPort(const std::string& port_name, int baudrate);
    void ClosePort();
    void SendData(const std::vector<uint8_t>& data);
    void SendDataString(const std::string& str, bool is_hex);

    // Async thread loops
    void ThreadReadLoop();
    void ThreadWriteLoop();

    // Configuration Manager
    void LoadConfig();
    void SaveConfig();

    // Helper functions for RX processing
    void ProcessRxData();
    std::string FormatBytes(const uint8_t* data, size_t size, bool hex, bool show_ts);
    std::string SaveLogFileDialog();

    // Hex helper helper
    static std::vector<uint8_t> HexStringToBytes(const std::string& hex_str);
    static std::string BytesToHexString(const uint8_t* data, size_t size);

private:
    // Win32 Comm Handle and Overlapped contexts
    HANDLE hComm_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> thread_running_{false};
    std::thread read_thread_;
    std::thread write_thread_;

    // Thread-safe RX buffers
    std::mutex rx_mutex_;
    std::vector<uint8_t> rx_raw_buffer_;
    std::string rx_display_text_;

    // Thread-safe TX queue
    std::mutex tx_mutex_;
    std::condition_variable tx_cv_;
    std::queue<std::vector<uint8_t>> tx_queue_;

    // UI Port Settings state
    std::vector<std::string> available_ports_;
    char selected_port_[64] = {0};
    int selected_baudrate_ = 115200;
    bool is_open_ = false;

    // Statistics counts
    std::atomic<size_t> rx_count_{0};
    std::atomic<size_t> tx_count_{0};

    // UI Option states
    bool show_hex_ = false;
    bool auto_scroll_ = true;
    bool show_timestamp_ = true;
    bool paused_ = false;

    // Direct Tx Input buffer
    char tx_input_buf_[1024] = {0};
    int tx_hex_mode_ = 0;

    // 16 Custom Macros
    std::vector<SerialMacro> macros_;
    static constexpr int kMacroCount = 16;
    static constexpr const char* kConfigFile = "mstudio.cfg";
};

#endif // SERIAL_PANEL_H
