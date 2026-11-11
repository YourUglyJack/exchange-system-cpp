// 文本解析指令
#pragma once

#include <cstddef>
#include <istream>
#include <ostream>

#include "book/map_orderbook.hpp"

namespace lob {

struct RunStats {
    std::size_t lines = 0; // 读取行数
    std::size_t commands = 0; // 成功解析并运行的 cmd
    std::size_t parse_errors = 0; // 无法解析的 cmd
};

// 逐行读取 in 中的指令，在 book 上执行，结果 写入到 out
RunStats run_commands(std::istream& in, std::ostream& out, OrderBook& book);
}