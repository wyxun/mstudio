#include "serial_cmd.h"
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <chrono>
#include <thread>
#include <iomanip>
#include <cctype>
#include <cstdlib>

static std::vector<std::string> ScanAvailablePorts() {
    std::vector<std::string> ports;
    char target[5000]; // Buffer for QueryDosDeviceA
    for (int i = 1; i <= 256; ++i) {
        std::string port_name = "COM" + std::to_string(i);
        DWORD res = QueryDosDeviceA(port_name.c_str(), target, 5000);
        if (res != 0) {
            ports.push_back(port_name);
        }
    }
    return ports;
}

static void PrintSerialUsage() {
    std::cout << "Usage: aitrace serial --port <COMx> [--baud <rate>] [--duration <sec>] [--hex] [--ascii] [--send <str>] [--send-hex <hex>]\n\n"
              << "Options:\n"
              << "  --port <COMx>      Specify the serial port (e.g. COM3)\n"
              << "  --baud <rate>      Specify baud rate (default: 115200)\n"
              << "  --duration <sec>   Stop capture after <sec> seconds (default: infinite)\n"
              << "  --hex              Display incoming bytes in HEX format\n"
              << "  --ascii            Display incoming bytes as clean ASCII text (default)\n"
              << "  --send <str>       Transmit ASCII string before listening for feedback\n"
              << "  --send-hex <hex>   Transmit HEX bytes (e.g. \"A5 5A 01\") before listening\n\n";

    // Helpful Port Scanner
    std::vector<std::string> available_ports = ScanAvailablePorts();
    if (available_ports.empty()) {
        std::cout << "[SYSTEM] No active serial ports detected in your Windows system.\n";
    } else {
        std::cout << "[SYSTEM] Available serial ports currently detected:\n";
        for (const auto& p : available_ports) {
            std::cout << "  * " << p << "\n";
        }
        std::cout << std::endl;
    }
}

static std::vector<uint8_t> HexStringToBytes(const std::string& hex_str) {
    std::vector<uint8_t> bytes;
    std::string cleaned;
    for (char c : hex_str) {
        if (isxdigit((unsigned char)c)) cleaned += c;
    }
    if (cleaned.length() % 2 != 0) {
        cleaned.pop_back(); // Ignore trailing odd character
    }
    for (size_t i = 0; i < cleaned.length(); i += 2) {
        std::string byte_str = cleaned.substr(i, 2);
        uint8_t byte = (uint8_t)std::strtol(byte_str.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

int serial_main(int argc, char* argv[]) {
    std::string port = "";
    int baud = 115200;
    int duration = -1; // -1 means infinite
    bool hex_mode = false;
    std::string send_str = "";
    std::string send_hex_str = "";

    // Command line argument parser
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = argv[++i];
        } else if (arg == "--baud" && i + 1 < argc) {
            baud = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::stoi(argv[++i]);
        } else if (arg == "--hex") {
            hex_mode = true;
        } else if (arg == "--ascii") {
            hex_mode = false;
        } else if (arg == "--send" && i + 1 < argc) {
            send_str = argv[++i];
        } else if (arg == "--send-hex" && i + 1 < argc) {
            send_hex_str = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            PrintSerialUsage();
            return 0;
        }
    }

    if (port.empty()) {
        std::cerr << "[ERROR] --port <COMx> is required!\n\n";
        PrintSerialUsage();
        return 1;
    }

    // Windows requires "\\\\.\\" prefix for ports greater than COM9
    std::string full_port_path = "\\\\.\\" + port;

    HANDLE hComm = CreateFileA(
        full_port_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, // Exclusive access
        NULL,
        OPEN_EXISTING,
        0, // Non-overlapped (Synchronous) I/O
        NULL
    );

    if (hComm == INVALID_HANDLE_VALUE) {
        DWORD dwErr = GetLastError();
        std::cerr << "[ERROR] Failed to open serial port " << port << " (Error: " << dwErr << ")\n\n";
        PrintSerialUsage();
        return 1;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hComm, &dcb)) {
        std::cerr << "[ERROR] GetCommState failed.\n";
        CloseHandle(hComm);
        return 1;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    if (!SetCommState(hComm, &dcb)) {
        std::cerr << "[ERROR] SetCommState failed. Check if baud rate " << baud << " is supported.\n";
        CloseHandle(hComm);
        return 1;
    }

    // Set non-blocking timeouts so ReadFile returns quickly if no data
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = 50; // Max time between read bytes (ms)
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 100; // Timeout constant for read (ms)
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 100;

    if (!SetCommTimeouts(hComm, &timeouts)) {
        std::cerr << "[ERROR] SetCommTimeouts failed.\n";
        CloseHandle(hComm);
        return 1;
    }

    // Convert send payloads if defined
    std::vector<uint8_t> tx_bytes;
    if (!send_str.empty()) {
        tx_bytes.assign(send_str.begin(), send_str.end());
    } else if (!send_hex_str.empty()) {
        tx_bytes = HexStringToBytes(send_hex_str);
        if (tx_bytes.empty()) {
            std::cerr << "[ERROR] Invalid send-hex string sequence.\n";
            CloseHandle(hComm);
            return 1;
        }
    }

    // Perform atomic Write before Read Loop
    if (!tx_bytes.empty()) {
        DWORD bytes_written = 0;
        BOOL success = WriteFile(hComm, tx_bytes.data(), (DWORD)tx_bytes.size(), &bytes_written, NULL);
        if (!success || bytes_written != tx_bytes.size()) {
            DWORD dwErr = GetLastError();
            std::cerr << "[WARNING] WriteFile failed or partial write (Error: " << dwErr << ")\n";
        } else {
            std::cout << "[SYSTEM] Sent " << bytes_written << " bytes: ";
            if (!send_str.empty()) {
                std::cout << "\"" << send_str << "\" [ASCII]\n";
            } else {
                std::cout << "\"" << send_hex_str << "\" [HEX]\n";
            }
        }
    }

    std::cout << "[SYSTEM] Listening on " << port << " @ " << baud << " bps..." << std::endl;
    if (duration > 0) {
        std::cout << "[SYSTEM] Capturing for " << duration << " seconds..." << std::endl;
    } else {
        std::cout << "[SYSTEM] Press Ctrl+C to stop." << std::endl;
    }

    auto start_time = std::chrono::steady_clock::now();
    std::vector<char> buffer(2048);

    while (true) {
        // Check duration timeout
        if (duration > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time
            ).count();
            if (elapsed >= duration) {
                std::cout << "\n[SYSTEM] Capture duration reached. Exiting." << std::endl;
                break;
            }
        }

        DWORD bytes_read = 0;
        BOOL success = ReadFile(hComm, buffer.data(), (DWORD)buffer.size(), &bytes_read, NULL);

        if (success && bytes_read > 0) {
            if (hex_mode) {
                // Print in HEX format
                for (DWORD i = 0; i < bytes_read; ++i) {
                    printf("%02X ", (uint8_t)buffer[i]);
                }
                fflush(stdout);
            } else {
                // Print in clean ASCII
                for (DWORD i = 0; i < bytes_read; ++i) {
                    char c = buffer[i];
                    if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c <= 126)) {
                        std::cout << c;
                    } else {
                        std::cout << ".";
                    }
                }
                std::cout.flush();
            }
        } else if (!success) {
            DWORD dwErr = GetLastError();
            std::cerr << "\n[ERROR] ReadFile failed (Error: " << dwErr << "). Exiting.\n";
            break;
        }

        // Avoid CPU hot looping
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CloseHandle(hComm);
    return 0;
}
