// 把处理结果封装
// 方便： 可视化结果，打印，测试，后续处理
#pragma once

#include <cstddef> // std::size_t
#include <vector>

#include "order.hpp"
#include "common/types.hpp"

namespace exchange {
    
// 订单薄快照 snap，每个价位一行
// print 输出， 单元测试，对拍比较
struct LevelView{
    Price price;
    Qty total_qty;
    std::size_t order_count;
};

inline bool operator==(const LevelView& a, const LevelView& b) {
    return a.price == b.price && a.total_qty == b.total_qty && a.order_count == b.order_count;
}

struct BookSnap {
    std::vector<LevelView> bids;
    std::vector<LevelView> asks;
};

inline bool operator==(const BookSnap& a, const BookSnap& b) {
    return a.asks == b.asks && a.bids == b.bids;
}

inline bool operator!=(const BookSnap& a, const BookSnap& b) {
    return !(a == b);
}
}
