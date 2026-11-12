// Engine 的 输出， 引擎干了啥
// v2 处理三类 事件

// Trade 成交纪录，行情里面的 逐笔成交
// ExecReport 某张订单的处理结果，ac了还是 cancel 了 还是 被拒绝了
// BookUpdate 某一侧 某个 档位 还有多少挂单

// 一次 Req 也许会同时产生 上述 三种事件

// ExecReport 是 私有的，只有 订单主人才能看
// BookUpdate 是 公开的，只有价格聚合，没有任何id

// 每个 events 都有 seq, ts_ns
// seq 全局严格递增
// ts_ns 纳秒时间戳，同一个 req 的 所有 events 共享

# pragma once

#include <cstdint>
#include <ostream>

#include "common/types.hpp"

namespace exchange {
// =======================
// ExecType 执行回报描述的事件
// =======================
enum class ExecType : std::uint8_t {
    Accepted,
    PartialFill, // 部分成交，还有剩余
    Filled,
    Canceled, // 撤销，主动撤单 / IOC 剩余 / 市价 剩余 / FOK 不满足
    Replaced, // 改单
    Rejected,
};

// =======================
// RejectReason 拒绝原因
// =======================
enum class RejectReason : std::uint8_t {
    None, 
    DuplicateId,   // 重复 order id
    UnknownOrder,  // 撤单 / 改单 的目标订单 不存在
    UnknowSymbol,  // 品种未注册
    InvalidQty,  // qty <= 0
    InvalidPrice,  // 限价单 价格 <= 0
    BadTickSize, // 价格不是 tick_size 的整数倍，不符合交易规则
    BadLotSize, // 下单量 不是 lot_size 的整数倍
    InvalidTif, // market + GTC 不存在这种组合
};

// “Accepted” 字符串字面量，本质是以 \0 结尾的 const char数组，在整个程序运行期间都有效
// 返回时，数组转化成 指向 首字符的 const char*
constexpr const char* to_string(ExecType t) noexcept {
    switch (t) {
        case ExecType::Accepted: return "Accepted";
        case ExecType::Canceled: return "Canceled";
        case ExecType::Filled: return "Filled";
        case ExecType::PartialFill: return "PartialFill";
        case ExecType::Rejected: return "Rejected";
        case ExecType::Replaced: return "Replaced";
    }
    return "?";
}

constexpr const char* to_string(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None: return "None";
        case RejectReason::DuplicateId: return "DuplicateId";
        case RejectReason::UnknownOrder: return "UnknownOrder";
        case RejectReason::UnknowSymbol: return "UnknowSymbol";
        case RejectReason::InvalidQty: return "InvalidQty";
        case RejectReason::InvalidPrice: return "InvalidPrice";
        case RejectReason::BadTickSize: return "BadTickSize";
        case RejectReason::BadLotSize: return "BadLotSize";
        case RejectReason::InvalidTif: return "InvalidTif";
    }
    return "?";
}

// 一笔成交， 一张 taker 订单 可能有很多 trade， 吃掉好几个 maker
struct Trade {
    std::uint64_t seq;
    std::uint64_t ts_ns; // timestamp
    SymbolId symbol; // 品种
    OrderId maker_id;
    OrderId taker_id;
    Side aggressor; // 这笔交易中 主动发起 交易的一方，谁主动吃掉了已经有的挂单
    Price price;
    Qty qty;
};

// ==========================================
// ExecReport 执行回报
// order_qty:   订单当前总量， 新单 = 原始数量，改单后 = cum_qty + new_qty
// cum_qty:     累计已经成交的 qty
// leaves_qty:  在 订单簿上，等待成交的 qty

// Accepted / PartialFilled / Filled / Replaced: cum_qty + leaves_qty == order_qty
// Canceled: leaves_qty == 0, order_qty - cum_qty = 撤单数量
// Rejected: all is 0
// ==========================================
struct ExecReport {
    std::uint64_t seq; // 事件流水号，为了 明确， Engine 先发哪条 事件，并且 一个订单会产生 很多 events，尤其是 吃 很多单的时候
    std::uint64_t ts_ns;
    OrderId id;
    SymbolId symbol;
    ExecType type;
    RejectReason reason; // 仅 Rejected 的时候有意义，其他时候为 None
    Price last_price;  // 本次 成交价格
    Qty last_qty; // 本次成交量
    Qty leaves_qty;
    Qty cum_qty;
    Qty order_qty;
};

// ==========================================
// BookUpdate: 盘口发生了什么，后续负责广播
// 发新的 总量， 发 变化量 容易 违背 难以处理 幂等性
// 而且 V5 UDP 广播行情，丢包是常态
// ==========================================
struct BookUpdate {
    std::uint64_t seq;
    std::uint64_t ts_ns;
    SymbolId symbol;
    Side side; // 什么盘
    Price price;
    Qty new_total; // 新的总量
};

inline bool operator==(const Trade& a, const Trade& b) noexcept {
    return a.seq == b.seq
    && a.ts_ns == b.ts_ns
    && a.symbol == b.symbol
    && a.maker_id == b.maker_id
    && a.taker_id == b.taker_id
    && a.aggressor == b.aggressor
    && a.price == b.price
    && a.qty == b.qty;
};

inline bool operator==(const ExecReport& a, const ExecReport& b) noexcept {
    return a.seq == b.seq
    && a.ts_ns == b.ts_ns
    && a.id == b.id
    && a.symbol == b.symbol
    && a.type == b.type
    && a.reason == b.reason
    && a.last_price == b.last_price
    && a.last_qty == b.last_qty
    && a.leaves_qty == b.leaves_qty
    && a.cum_qty == b.cum_qty
    && a.order_qty == b.order_qty;
};

inline bool operator==(const BookUpdate& a, const BookUpdate& b) noexcept {
    return a.seq == b.seq
    && a.ts_ns == b.ts_ns
    && a.symbol == b.symbol
    && a.side == b.side
    && a.price == b.price
    && a.new_total == b.new_total;
};

inline std::ostream& operator<<(std::ostream& os, const Trade& t) {
    return os << "TRADE sym=" << t.symbol << " maker=" << t.maker_id << " taker=" << t.taker_id << ' ' << to_string(t.aggressor)
    << ' ' << t.price << 'x' << t.qty;
}

inline std::ostream& operator<<(std::ostream& os, const ExecReport& e) {
    os << "EXEC sym=" << e.symbol << " id=" << e.id << ' ' << to_string(e.type);
    if (e.type == ExecType::Rejected) {
        return os << " reason=" << to_string(e.reason);
    }
    if (e.type == ExecType::PartialFill || e.type == ExecType::Filled) {
        os << " last=" << e.last_price << 'x' << e.last_qty;
    }
    return os << " cum=" << e.cum_qty << " leaves=" << e.leaves_qty << " qty=" << e.order_qty;
}

inline std::ostream& operator<<(std::ostream& os, const BookUpdate& b) {
    return os << "BOOK sym=" << b.symbol << ' ' << (b.side == Side::Buy ? "BID " : "ASK ")
              << b.price << " -> " << b.new_total;
}

}



