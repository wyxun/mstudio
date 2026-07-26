#include <iostream>
#include <string>
#include <cstring>

// Forward declare command handlers — each returns 0 on success
int shell_main(int argc, char* argv[]);
int wave_main(int argc, char* argv[]);
int ocd_main(int argc, char* argv[]);
int gdb_main(int argc, char* argv[]);
int map_main(int argc, char* argv[]);
int crash_main(int argc, char* argv[]);
int serial_main(int argc, char* argv[]);

static void PrintUsage() {
    std::cout << "aitrace — AI-driven MCU debugging CLI\n\n"
              << "Usage: aitrace <command> [args...]\n\n"
              << "Passive (no intrusion):\n"
              << "  shell [--raw] <cmd...>  Send command via RTT Ch0 (TCP 9090)\n"
              << "                          ([T] log lines filtered unless --raw)\n"
              << "  wave  capture <sec>  Capture waveform to CSV (TCP 9091)\n"
              << "  wave  stat [sec]     Link quality: rate / crc_err / seq_lost\n"
              << "  wave  list|start|stop|rate <n>\n\n"
              << "Halt-based (intrusive):\n"
              << "  ocd   halt|resume|regs|peek <addr>|mdw <addr> [n]|stack [n]\n\n"
              << "GDB (intrusive, requires explicit enable):\n"
              << "  gdb   connect|break <loc>|continue|step|print <expr>|bt|detach\n\n"
              << "Analysis:\n"
              << "  map   resolve <elf> <addr...>|info <elf_or_map>\n"
              << "  crash report --pc=<hex> --lr=<hex> --sp=<hex> --elf=<path>\n"
              << "  serial --port=<COMx> [--baud=<rate>] [--duration=<sec>] [--hex] [--ascii]\n"
              << "                       Listen to serial port data (passive, zero intrusion)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "shell")  return shell_main(argc - 1, argv + 1);
    if (cmd == "wave")   return wave_main(argc - 1, argv + 1);
    if (cmd == "ocd")    return ocd_main(argc - 1, argv + 1);
    if (cmd == "gdb")    return gdb_main(argc - 1, argv + 1);
    if (cmd == "map")    return map_main(argc - 1, argv + 1);
    if (cmd == "crash")  return crash_main(argc - 1, argv + 1);
    if (cmd == "serial") return serial_main(argc - 1, argv + 1);

    std::cerr << "Unknown command: " << cmd << "\n";
    PrintUsage();
    return 1;
}
