// 把处理结果封装
// 方便： 可视化结果，打印，测试，后续处理
#pragma once

#include <cstddef> // std::size_t
#include <cstdint>
#include <vector>

#include "order.hpp"
#include "../common/types.hpp"

namespace lob {

enum class AddStatus : std::uint8_t {
    Accepted,
    DuplicateId,
    InvalidQty,
    InvalidPrice,
};

constexpr const char* to_string(AddStatus s) {
    switch (s) {
    case AddStatus::Accepted:
        return "Accepted";
    case AddStatus::DuplicateId:
        return "DuplicateId";
    case AddStatus::InvalidQty:
        return "InvalidQty";
    case AddStatus::InvalidPrice:
        return "InvalidPrice";
    default:
        return "Unknown";
    }
}


struct AddResult {
    AddStatus status{AddStatus::Accepted};
    std::vector<Trade> trades; // 本次产生的所有交易，按照交易时间排序，因为可能 taker多个单，所以 需要 vector 存 trade

    bool accepted() const noexcept {
        return status == AddStatus::Accepted;
    }
};

inline bool operator==(const AddResult& a, const AddResult& b) {
    // trade 的 operator == 已经重载了
    return a.status == b.status && a.trades == b.trades;
}

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
