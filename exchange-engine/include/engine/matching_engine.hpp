// v2 撮合引擎：多品种路由 + 业务规则 + 事件
//
// MatchingEngine 通过 调用 OrderBook 的 原语 完成 业务
//
//   ┌──────────────── MatchingEngine（业务规则）────────────────┐
//   │ 按 symbol 路由    校验（tick/lot/重复 id…）    订单类型与 TIF 逻辑  │
//   │ 改单规则          事件序号与时间戳             把 Fill 翻译成事件   │
//   └───────────────────────────────┬──────────────────────────────────┘
//                                   │ 调用原语
//   ┌──────────────── OrderBook（机制 / 数据结构）─────────────────────┐
//   │ match   rest   remove   reduce   fillable_qty   snapshot          │
//   └──────────────────────────────────────────────────────────────────┘
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "book/map_orderbook.hpp"
#include "common/config.hpp"
#include "engine/events.hpp"
#include "engine/requests.hpp"
#include "engine/listener.hpp"

namespace exchange {

using ClockFn = std::uint64_t (*) (); // 函数指针
std::uint64_t steady_clock_ns(); // 默认时钟

class MatchingEngine {
public:
    // 构造函数
    explicit MatchingEngine(EngineListener& listener, ClockFn clock = &steady_clock_ns);
    // 注册 symbol
    bool add_symbol(const SymbolConfig& cfg);
    // 处理请求, 所有结果通过 listenner 以 事件形式 emit
    void handle(const Request& req);
    
    // 感觉 const OrderBook& 也可以； 两者 对 订单簿不存在的表达方式不一样; OrderBook* nullptr， OrderBook& 异常
    const OrderBook* book(SymbolId symbol) const; // 找不到就返回 nullptr
    const SymbolConfig* config(SymbolId symbol) const;

    bool check_invariants(std::string* why = nullptr) const;

private:

    struct SymbolState {
        SymbolConfig cfg;
        OrderBook book;
    };

    std::unordered_map<SymbolId, SymbolState> symbols_;
    EngineListener& listener_;
    ClockFn clock_;
    std::uint64_t next_event_seq_ = 1;
    std::uint64_t now_ns_ = 0; // 当前 req 的时间戳
    std::vector<Fill> fills_; // 服用的撮合输出缓冲，避免每次请求分配内存

    SymbolState* find_symbol(SymbolId symbol);

    void on_request(const NewOrder& r);
    void on_request(const CancelOrder& r);
    void on_request(const ModifyOrder& r);

    void execute(SymbolState& s, Order& taker, TimeInForce tif);

    void emit_trade(SymbolId sym, OrderId maker, OrderId taker, Side aggressor, Price px, Qty q);
    void emit_exec(SymbolId sym, OrderId id, ExecType type, Price last_px, Qty last_qty, Qty leaves, Qty cum, Qty order_qty);
    void emit_reject(SymbolId sym, OrderId id, RejectReason reason);
    void emit_book(SymbolId sym, Side side, Price px, Qty new_total);
};
}