#pragma once

#include <variant>

#include "common/types.hpp"

// 用 variant 来 存 3 种 Req
// Req 种类固定，目前就 3 种，而且 请求主要是 承载数据，处理逻辑是交给 Engine 进行
// 没必要 整 Request 基类 + 三种 派生类型， 还要 写 虚函数 啥的 

namespace exchange {

struct NewOrder {
    OrderId id;
    SymbolId symbol; // uint32_t
    Side side;
    OrderType type; // 订单类型
    TimeInForce tif; // 时效类型
    Price price;
    Qty qty;
};

struct CancelOrder {
    OrderId id;
    SymbolId symbol;
};

struct ModifyOrder {
    OrderId id;
    SymbolId symbol;
    Price new_price;
    Qty new_qty; // 新的 剩余数量
};

using Request = std::variant<NewOrder, CancelOrder, ModifyOrder>;
}
