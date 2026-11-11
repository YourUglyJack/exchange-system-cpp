// 读取指令文件，驱动 Orderbook
// 用法
//  ./file_driver data/sample_orders.txt 从文件读取
//  ./file driver 从标准输入读取，命令行交互

#include <fstream>
#include <iostream>

#include "book/map_orderbook.hpp"
#include "driver/command_runner.hpp"

// argc 参数数量
// argv 参数值 arg vector
int main(int argc, char** argv) {
    // 关闭 C++ 标准流 与 C 标准输入之间的同步
    // 如果只用 cin cout，真的可以关掉
    std::ios::sync_with_stdio(false);

    lob::OrderBook book;
    lob::RunStats stats;

    if (argc >= 2) {
        std::ifstream file(argv[1]); // input file stream
        if (!file) {
            std::cerr << "cannot open " << argv[1] << "\n";
            return 1;
        }
        stats = lob::run_commands(file, std::cout, book);
    } else {
        stats = lob::run_commands(std::cin, std::cout, book);    
    }

    // 统计信息写到 stderr，不污染 stdout 上的正式输出，
    // 这样 stdout 可以直接和期望输出文件做 diff。
    std::cerr << "[file_driver] lines=" << stats.lines << " commands=" << stats.commands
              << " parse_errors=" << stats.parse_errors
              << " resting_orders=" << book.order_count() << '\n';

    // 有解析错误时返回非零退出码，方便在脚本 / CI 中检测。
    return stats.parse_errors == 0 ? 0 : 2;
}