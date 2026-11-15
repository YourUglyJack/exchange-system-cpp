// =============================================================================
// tests/engine_fixture.hpp —— 引擎测试共用夹具
// -----------------------------------------------------------------------------
// 每个测试得到：一个注册了品种 AAA（id=1, tick=1, lot=1）的全新引擎、
// 一个 RecordingListener、固定时钟（ts 恒为 1000，方便断言）。
// TearDown 自动检查：订单簿不变量 + 事件流不变量（seq 连续、回报数量守恒）。
// ========================================================================
#pragma once

#include <gtest/gtest.h>
#include <vector>

#include "engine/matching_engine.hpp"
#include "testing/recording_listener.hpp"

namespace exchange::test {
using testing::Event;
using testing::RecordingListener;

inline constexpr SymbolId AAA = 1;
inline std::uint64_t fake_clock() { return 1000; }

class EngineTest : public :: testing::Test {
protected:
    RecordingListener listener;
    MatchingEngine engine{listener, &fake_clock};

    void SetUp() override {
        ASSERT_TRUE(engine.add_symbol(SymbolConfig{AAA, "AAA", 1, 1}));
    }

    void TearDown() override {
        std::string why;
        EXPECT_TRUE(engine.check_invariants(&why)) << why;
        EXPECT_EQ(listener.error(), "") << " event stream check failed";
    }

    // 挂 limit 单
    void limit(OrderId id, Side side, Price px, Qty qty, TimeInForce tif = TimeInForce::GTC, SymbolId sym = AAA) {
        NewOrder o{id, sym, side, OrderType::Limit, tif, px, qty};
        engine.handle(o);
    }

    // 挂 市价单
    void market(OrderId id, Side side, Qty qty, TimeInForce tif = TimeInForce::IOC, SymbolId sym = AAA) {
        Price px = side == Side::Buy ? kMarketBuyLimit : kMarketSellLimit;
        NewOrder o{id, sym, side, OrderType::Market, tif, px, qty};
        engine.handle(o);
    }

    // 撤单
    void cancel(OrderId id, SymbolId sym = AAA) {
        CancelOrder o{id, sym};
        engine.handle(o);
    }

    // 改单
    void modify(OrderId id, Price px, Qty qty, SymbolId sym = AAA) {
        ModifyOrder o{id, sym, px, qty};
        engine.handle(o);
    }

    void rest(OrderId id, Side side, Price px, Qty qty) {
        limit(id, side, px, qty);
        ASSERT_TRUE(listener.only<Trade>().empty()) << "setup order " << id << "traded";
        listener.clear();
    }

    const OrderBook& book(SymbolId sym = AAA) const {
        return *engine.book(sym);
    }

    std::vector<Trade> trades() const {
        return listener.only<Trade>();
    }

    std::vector<ExecReport> execs() const {
        return listener.only<ExecReport>();
    }

    // 某张订单收到的回报类型序列，例如 {Accepted, PartialFill, Canceled}
    std::vector<ExecType> exec_types(OrderId id) const {
        std::vector<ExecType> out;
        for (const auto& e : execs()) {
            if (e.id == id) out.push_back(e.type);
        }
        return out;
    }

    // 某张订单的最后一条回报
    ExecReport last_exec(OrderId id) const {
        ExecReport last{};
        for (const auto& e : execs()) {
            if (e.id == id) last = e;
        }
        return last;
    }
};

using Levels = std::vector<LevelView>;
using ET = ExecType;

// 只比较成交的业务字段（maker, taker, price, qty），忽略 seq/ts/symbol/aggressor。
struct TradeKey {
    OrderId maker, taker;
    Price price;
    Qty qty;

    bool operator==(const TradeKey& o) const {
        return maker == o.maker && taker == o.taker && price == o.price && qty == o.qty;
    }
};

inline std::ostream& operator<<(std::ostream& os, const TradeKey& k) {
    return os << "{maker=" << k.maker << " taker=" << k.taker << " " << k.price << "x" << k.qty << "}";
}
inline std::vector<TradeKey> keys(const std::vector<Trade>& ts) {
    std::vector<TradeKey> out;
    for (const auto& t : ts) out.push_back({t.maker_id, t.taker_id, t.price, t.qty});
    return out;
}

}
