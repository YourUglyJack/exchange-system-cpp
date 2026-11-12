#pragma once

#include <string>

#include "common/types.hpp"

namespace exchange {
    // tick_size: 最小变动价格，价格必须是 tick_size 的整数倍
    // lot_size: 最小交易单位，数量是 lot_size 的 整数倍，eg A股 是 100股 一手，买不了 150股

struct SymbolConfig {
    SymbolId id;
    std::string name; // 只在配置和显示的时候用
    Price tick_size;
    Qty lot_size;
};

}

// what is tick ？
// 现实中 价格是 ¥100.25,  但机器会有浮点数误差，所以不能用 double 存储
// 所以 选一个 最小单位，所有价格都是这个 最小单位的整数倍. eg tick = 0.01
// tick = 0.01, price = 10025

// what is tick_size ?
// 交易所 会 规定，每个品种的 最小报价 变动，即 每个品种 规定可能不一样
// A, 每次至少变动 0.01, tick_size = 1
// B, 每次至少变动 0.05, tick_size = 5
// BBB 10027 满足 tick，但 不满足 tick_size 拒绝交易

// summary
// tick 定义 单位， 价格怎么去表示，全系统 规定 1 = 0.01
// tick_size 定义 规则， 某个品种 允许 哪些价格，价格必须是 tick_size 的 整数倍，需要用 engine 去校验
