#include "engine_fixture.hpp"
#include "testing/reference_engine.hpp"

using namespace exchange;
using namespace exchange::test;
// ---------------------------------------------------------------------------
// v1 回归
// ---------------------------------------------------------------------------
TEST_F(EngineTest, T1_NoCross_BothSidesRest) {
    rest(1, Side::Buy, 100, 10);
    rest(2, Side::Sell, 101, 5);
    EXPECT_EQ(book().best_bid(), 100);
    EXPECT_EQ(book().best_ask(), 101);
}

TEST_F(EngineTest, T2_ExactFill_BookEmpty) {
    rest(1, Side::Sell, 100, 10);
    limit(2, Side::Buy, 100, 10);
    EXPECT_EQ(keys(trades()), (std::vector<TradeKey>{{1, 2, 100, 10}}));
    EXPECT_EQ(book().order_count(), 0u);
}

TEST_F(EngineTest, T3_TakerLarger_RemainderRests) {
    rest(1, Side::Sell, 100, 4);
    limit(2, Side::Buy, 100, 10);
    const Order* taker = book().find(2);
    ASSERT_NE(taker, nullptr);
    EXPECT_EQ(taker->qty, 6);
    EXPECT_EQ(book().best_bid(), 100);
}

TEST_F(EngineTest, T4_MakerLarger_KeepsQueuePosition) {
    rest(1, Side::Sell, 100, 10);
    rest(2, Side::Sell, 100, 10);
    limit(3, Side::Buy, 100, 3);
    limit(4, Side::Buy, 100, 1);
    EXPECT_EQ(keys(trades()), (std::vector<TradeKey>{{1, 3, 100, 3}, {1, 4, 100, 1}}));
}

TEST_F(EngineTest, T5_SweepLevelsBestFirst_StopAtLimit) {
    rest(1, Side::Sell, 102, 5);
    rest(2, Side::Sell, 100, 5);
    rest(3, Side::Sell, 101, 5);
    limit(4, Side::Buy, 101, 20);
    EXPECT_EQ(keys(trades()), (std::vector<TradeKey>{{2, 4, 100, 5}, {3, 4, 101, 5}}));
    EXPECT_EQ(book().best_ask(), 102);
    const Order* taker = book().find(4);
    ASSERT_NE(taker, nullptr);
    EXPECT_EQ(taker->qty, 10);
}

TEST_F(EngineTest, T6_SamePriceFIFO) {
    rest(1, Side::Buy, 100, 3);
    rest(2, Side::Buy, 100, 3);
    rest(3, Side::Buy, 100, 3);
    limit(4, Side::Sell, 100, 7);
    EXPECT_EQ(keys(trades()),
              (std::vector<TradeKey>{{1, 4, 100, 3}, {2, 4, 100, 3}, {3, 4, 100, 1}}));
}

TEST_F(EngineTest, T7_TradePriceIsMakerPrice) {
    rest(1, Side::Sell, 100, 5);
    limit(2, Side::Buy, 105, 5);
    ASSERT_EQ(trades().size(), 1u);
    EXPECT_EQ(trades()[0].price, 100);
    EXPECT_EQ(trades()[0].aggressor, Side::Buy);
}

TEST_F(EngineTest, T8_CanceledOrderDoesNotTrade) {
    rest(1, Side::Sell, 100, 5);
    rest(2, Side::Sell, 100, 5);
    cancel(1);
    limit(3, Side::Buy, 100, 5);
    EXPECT_EQ(keys(trades()), (std::vector<TradeKey>{{2, 3, 100, 5}}));
}

TEST_F(EngineTest, T9_CancelUnknownOrTwice_Rejected) {
    rest(1, Side::Buy, 100, 5);
    cancel(999);
    cancel(1);
    cancel(1);
    const auto e = execs();
    ASSERT_EQ(e.size(), 3u);
    EXPECT_EQ(e[0].reason, RejectReason::UnknownOrder);
    EXPECT_EQ(e[1].type, ET::Canceled);
    EXPECT_EQ(e[2].reason, RejectReason::UnknownOrder);
}

TEST_F(EngineTest, T10_CancelLastOrderAtBest_BestUpdates) {
    rest(1, Side::Buy, 100, 5);
    rest(2, Side::Buy, 99, 5);
    cancel(1);
    EXPECT_EQ(book().best_bid(), 99);
}

// ---------------------------------------------------------------------------
// 校验与拒单：每种 RejectReason 至少一个测试，且拒单不改变订单簿
// ---------------------------------------------------------------------------
class RejectTest : public EngineTest {
protected:
    void SetUp() override {
        EngineTest::SetUp();
        ASSERT_TRUE(engine.add_symbol(SymbolConfig{2, "BBB", /*tick=*/5, /*lot=*/100}));
        rest(1, Side::Sell, 100, 5);
        before = book().snapshot();
    }
    // 断言：只产生了一条 Rejected 回报，原因正确，订单簿不变
    void expect_reject(RejectReason why) {
        const auto e = execs();
        ASSERT_EQ(listener.events().size(), 1u);
        ASSERT_EQ(e.size(), 1u);
        EXPECT_EQ(e[0].type, ET::Rejected);
        EXPECT_EQ(e[0].reason, why);
        EXPECT_EQ(book().snapshot(), before);
    }
    BookSnapshot before;
};

TEST_F(RejectTest, UnknownSymbol) { limit(9, Side::Buy, 100, 1, TimeInForce::GTC, 77); expect_reject(RejectReason::UnknownSymbol); }
TEST_F(RejectTest, InvalidQty) { limit(9, Side::Buy, 100, 0); expect_reject(RejectReason::InvalidQty); }
TEST_F(RejectTest, InvalidPrice) { limit(9, Side::Buy, -1, 1); expect_reject(RejectReason::InvalidPrice); }
TEST_F(RejectTest, BadTickSize) { limit(9, Side::Buy, 1003, 100, TimeInForce::GTC, 2); expect_reject(RejectReason::BadTickSize); }
TEST_F(RejectTest, BadLotSize) { limit(9, Side::Buy, 1000, 150, TimeInForce::GTC, 2); expect_reject(RejectReason::BadLotSize); }
TEST_F(RejectTest, InvalidTif_MarketGtc) { market(9, Side::Buy, 1, TimeInForce::GTC); expect_reject(RejectReason::InvalidTif); }
TEST_F(RejectTest, UnknownOrderOnModify) { modify(42, 100, 1); expect_reject(RejectReason::UnknownOrder); }

// 重复 id：即使这张单"本可以成交"，也必须在撮合前被拒绝
TEST_F(RejectTest, DuplicateId_NoTrade) { limit(1, Side::Buy, 100, 5); expect_reject(RejectReason::DuplicateId); }

// 一个请求同时犯多个错误时，按规格顺序只报第一个（参考实现依赖这个顺序）
TEST_F(RejectTest, FirstErrorWins) {
    limit(1, Side::Buy, -5, 0);  // 重复 id + 非法价格 + 非法数量 → 先报数量
    expect_reject(RejectReason::InvalidQty);
}

// 改单被拒时，原订单原样保留
TEST_F(RejectTest, RejectedModifyLeavesOrderUntouched) {
    modify(1, 100, 0);
    expect_reject(RejectReason::InvalidQty);
    const Order* order = book().find(1);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->qty, 5);
}

// 价格和剩余量都不变时拒绝改单，原订单和盘口保持不变。
TEST_F(RejectTest, ModifyNoChangeLeavesOrderUntouched) {
    modify(1, 100, 5);
    expect_reject(RejectReason::NoChange);
}

// ---------------------------------------------------------------------------
// 多品种
// ---------------------------------------------------------------------------
TEST_F(EngineTest, SymbolsAreIsolated) {
    ASSERT_TRUE(engine.add_symbol(SymbolConfig{2, "BBB", 1, 1}));
    limit(1, Side::Sell, 100, 5, TimeInForce::GTC, AAA);
    limit(1, Side::Buy, 100, 5, TimeInForce::GTC, 2);  // 同 id、可交叉价格，但在另一个品种
    EXPECT_TRUE(trades().empty());
    EXPECT_EQ(book(AAA).best_ask(), 100);
    EXPECT_EQ(book(2).best_bid(), 100);
}

TEST_F(EngineTest, AddSymbolRejectsDuplicateAndBadConfig) {
    EXPECT_FALSE(engine.add_symbol(SymbolConfig{AAA, "dup", 1, 1}));
    EXPECT_FALSE(engine.add_symbol(SymbolConfig{5, "zero-tick", 0, 1}));
    EXPECT_FALSE(engine.add_symbol(SymbolConfig{6, "zero-lot", 1, 0}));
}

TEST_F(EngineTest, ReferenceRegistrationRejectsInvalidConfigWithoutReservingSymbol) {
    exchange::testing::ReferenceEngine ref;
    for (const SymbolConfig& cfg : {
        SymbolConfig{2, "zero-tick", 0, 1},
        SymbolConfig{2, "negative-tick", -1, 1},
        SymbolConfig{2, "zero-lot", 1, 0},
        SymbolConfig{2, "negative-lot", 1, -1},
    }) {
        EXPECT_FALSE(engine.add_symbol(cfg));
        EXPECT_FALSE(ref.add_symbol(cfg));
    }
    const SymbolConfig valid{2, "BBB", 5, 10};
    EXPECT_TRUE(engine.add_symbol(valid));
    EXPECT_TRUE(ref.add_symbol(valid));
}

TEST_F(EngineTest, ReferenceDuplicateRegistrationKeepsConfigurationAndOrders) {
    exchange::testing::ReferenceEngine ref;
    ASSERT_TRUE(ref.add_symbol(SymbolConfig{AAA, "AAA", 1, 1}));
    rest(1, Side::Sell, 100, 5);
    ref.handle(NewOrder{1, AAA, Side::Sell, OrderType::Limit, TimeInForce::GTC, 100, 5});
    const BookSnapshot before = ref.snapshot(AAA);

    const SymbolConfig duplicate{AAA, "changed", 5, 10};
    EXPECT_FALSE(engine.add_symbol(duplicate));
    EXPECT_FALSE(ref.add_symbol(duplicate));
    EXPECT_EQ(ref.snapshot(AAA), before);

    // 此单只符合原配置，用实际下单验证重复注册没有覆盖 tick/lot。
    limit(2, Side::Buy, 99, 1);
    const auto events = ref.handle(
        NewOrder{2, AAA, Side::Buy, OrderType::Limit, TimeInForce::GTC, 99, 1});
    ASSERT_EQ(events.size(), 1u);
    const auto* report = std::get_if<ExecReport>(&events.front());
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->type, ExecType::Accepted);
    EXPECT_EQ(ref.snapshot(AAA), book().snapshot());
}
