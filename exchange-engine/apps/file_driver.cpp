// 读取指令文件，驱动 Orderbook
// 用法
//  ./file_driver data/sample_orders.txt 从文件读取
//  ./file driver 从标准输入读取，命令行交互

#include <fstream>
#include <iostream>

#include "driver/command_runner.hpp"

// argc 参数数量
// argv 参数值 arg vector
int main(int argc, char** argv) {
    // 关闭 C++ 标准流 与 C 标准输入之间的同步
    // 如果只用 cin cout，真的可以关掉
    std::ios::sync_with_stdio(false);
    exchange::RunStats stats;
    if (argc >= 2) {
        std::ifstream file(argv[1]);
        if (!file) {
            std::cerr << "cannot open " << argv[1] << '\n';
            return 1;
        }
        stats = exchange::run_commands(file, std::cout);
    } else {
        stats = exchange::run_commands(std::cin, std::cout);
    }
    std::cerr << "[file_driver] lines=" << stats.lines << " commands=" << stats.commands << " parse_errors=" << stats.parse_errors << '\n';
    return stats.parse_errors == 0 ? 0 : 2;
}
