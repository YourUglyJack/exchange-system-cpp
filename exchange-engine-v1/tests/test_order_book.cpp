#include <gtest/gtest.h> // testing::Test, TEST_F
#include <string>

#include "book/map_orderbook.hpp"

using namespace lob;
// xxx_条件(params)
// 条件成立，什么都不发生
// 条件不成立: 
//      1. EXPECT_* 标记测试失败，打印信息，继续执行
//      2. ASSERT_* 标记测试失败，打印信息，立刻return 退出当前函数

namespace {
class OrderBookTest : public :: testing::Test {
// 子类可以访问，外部代码不可以访问
protected:
    OrderBook book;
    
    // GoogleTest 在每个测试结束后调用
    void TearDown() override {
        std::string why;
        // EXPECT_TRUE 是 宏
        EXPECT_TRUE(book.check_invariants(&why)) << why;
    }

    void rest(OrderId id, Side side, Price price, Qty qty) {
        const AddResult r = book.add(id, side, price, qty);
        
        // 订单被接受了
        ASSERT_TRUE(r.accepted()); 
        // 订单没成交，所以为空； 如果 成交了，那 empty() 为 false，报错
        ASSERT_TRUE(r.trades.empty()) << "setup order" << id << " unexpectedly traded";
    }
};

// Case 1: 买卖价格不交叉
// best_bids < best_asks
TEST_F(OrderBookTest, T1_NoCross_BothSidesRest) {
    // 买单，100
    ASSERT_NO_FATAL_FAILURE(rest(1, Side::Buy, 100, 10));

    // 卖单，101，无法成交
    const AddResult r = book.add(2, Side::Sell, 101, 5);

    EXPECT_TRUE(r.accepted());
    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(book.best_bid(), 100); // EQ, 比较两个值 a==b
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_EQ(book.order_count(), 2u);
}

// Case2: 等量完全成交
TEST_F(OrderBookTest, T2_ExactFill_BookBecomesEmpty) {
    // maker
    ASSERT_NO_FATAL_FAILURE(rest(1, Side::Sell, 100, 10));

    // taker
    const AddResult r = book.add(2, Side::Buy, 100, 10);

    ASSERT_TRUE(r.accepted());
    ASSERT_EQ(r.trades.size(), 1u);

    EXPECT_EQ(r.trades[0], (Trade{1, 2, Side::Buy, 100, 10}));

    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_bid().has_value()); // optional 的成员函数，空的 返回 false
    EXPECT_FALSE(book.best_ask().has_value());
}

// Case 3: taker 数量更大
TEST_F(OrderBookTest, T3_TakerLarger_RemainderRests) {
    // maker
    ASSERT_NO_FATAL_FAILURE(rest(1, Side::Sell, 100, 4));

    // taker, 吃掉 4，还有 6 被挂在 order 里
    const AddResult r = book.add(2, Side::Buy, 100, 10);

    ASSERT_TRUE(r.accepted());
    ASSERT_EQ(r.trades.size(), 1u); // u 无符号, size_t 是 无符号
    ASSERT_EQ(r.trades[0].qty, 4);
    // maker 被吃光，卖盘空，taker剩 6股 挂在 买盘 100
    EXPECT_FALSE(book.best_ask().has_value()); // 卖盘
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.remaining_qty(2), 6);
    EXPECT_FALSE(book.contains(1));
}

// =============================================================================
// 以下为待写的测试骨架
// 每个测试里的 GTEST_SKIP() 会让它显示为 SKIPPED（不算失败），写完后删掉那一行
// =============================================================================

// Case 4: maker 数量更大，maker 剩余量更新，并且仍然排在队首
TEST_F(OrderBookTest, T4_MakerLarger_MakerKeepsQueuePosition) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 同一价位挂两张卖单，先挂大的（id 1），再挂一张（id 2）
    // TODO Act:     一张买单吃掉 id 1 的一部分（数量 < id 1 的数量）
    // TODO Assert:
    //   - 只有 1 笔成交，maker 是 id 1，数量是买单数量
    //   - id 1 还在簿里，remaining_qty 减少了对应数量
    //   - 再来一张买单：先成交的仍然是 id 1，而不是 id 2（部分成交不会让 maker 丢掉队首位置）
}

// Case 5: 一笔订单吃多个价位，按价格从优到劣成交
TEST_F(OrderBookTest, T5_SweepMultipleLevels_BestPriceFirst) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 挂 3 个不同价位的卖单（故意不按价格顺序挂，例如 102、100、101）
    // TODO Act:     一张高价买单，数量足够吃掉其中两个半价位
    // TODO Assert:
    //   - trades 的顺序：价格 100 → 101 → 102，每笔的 maker id / price / qty 都对
    //   - 被吃光的价位消失，best_ask 变成剩下的那个价位
    //   - 最后那个价位上的 maker 剩余量正确
}

// Case 5b: 扫价位时遇到超出限价的价位必须停下，剩余量挂单
TEST_F(OrderBookTest, T5b_SweepStopsAtLimitPrice) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 卖盘挂 100、101、102 三个价位
    // TODO Act:     买单限价 101，数量大于 100 + 101 两个价位的总量
    // TODO Assert:
    //   - 只和 100、101 成交，102 没有被碰到
    //   - 买单剩余量挂在买盘 101；best_bid == 101，best_ask == 102
}

// Case 5c: 卖单吃买盘（反方向），检查 bids_ 是从高价往低价吃
TEST_F(OrderBookTest, T5c_SellTakerSweepsBidsHighestFirst) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 买盘挂 98、100、99 三个价位
    // TODO Act:     一张低价卖单吃掉多个价位
    // TODO Assert:
    //   - trades 顺序是 100 → 99 → 98（高价优先）
    //   - 每笔 Trade 的 taker_side 是 Sell
}

// Case 6: 同价位 FIFO，先到的 maker 先成交
TEST_F(OrderBookTest, T6_SamePriceFIFO) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 同一价位按 id 1、2、3 的顺序挂 3 张卖单
    // TODO Act:     一张买单，数量刚好吃完 id 1 再吃掉 id 2 的一部分
    // TODO Assert:
    //   - trades 依次是 maker 1、maker 2
    //   - id 1 已经不在簿里；id 2 的剩余量正确；id 3 没有被碰到
}

// Case 7: 成交价 = maker 的价格，而不是 taker 的价格（两个方向都要测）
TEST_F(OrderBookTest, T7_TradePriceIsMakerPrice) {
    GTEST_SKIP() << "TODO";
    // TODO 场景 a：卖单挂 100，买单出价 105 来吃 → 成交价应该是多少？
    // TODO 场景 b：买单挂 100，卖单要价 95 来吃 → 成交价应该是多少？
    // 提示：两个场景可以在同一个测试里做，注意第二个场景开始前簿的状态
}

// Case 8: 撤单后再撮合，被撤的订单不参与成交
TEST_F(OrderBookTest, T8_CanceledOrderDoesNotTrade) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 同一价位挂两张卖单 id 1、id 2，然后撤掉 id 1
    // TODO Assert cancel: cancel 的返回值；contains(1)
    // TODO Act:     一张买单来吃
    // TODO Assert:  成交的 maker 是 id 2
}

// Case 8b: 撤掉队列中间的订单，其余订单的先后顺序不变
TEST_F(OrderBookTest, T8b_CancelMiddleKeepsOthersOrder) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 同一价位挂 id 1、2、3，撤掉 id 2
    // TODO Act:     一张买单把这个价位吃光
    // TODO Assert:  trades 依次是 maker 1、maker 3；该价位消失
}

// Case 9: 撤不存在的单 / 重复撤单，返回 false，簿的状态不变
TEST_F(OrderBookTest, T9_CancelUnknownOrTwice_Fails_StateUnchanged) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 挂几张单，用 snapshot() 记下撤单前的状态
    // TODO Act + Assert:
    //   - cancel 一个从没出现过的 id → 返回 false，snapshot 不变
    //   - cancel 一个真实 id → true；再 cancel 同一个 id → false
    // 提示：BookSnap 已经重载了 ==，可以直接 EXPECT_EQ 两个 snapshot
    //       （不过 gtest 打印不出 BookSnap 的内容，失败时只会显示字节，用 EXPECT_TRUE(a == b) 也可以）
}

// Case 9b: 已全部成交的订单不能再撤
TEST_F(OrderBookTest, T9b_CancelFilledOrderFails) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 挂一张卖单，再用买单把它完全吃掉
    // TODO Assert:  cancel 这张卖单 → false
}

// Case 10: 撤掉最优价位的最后一单，价位被删除，best price 更新
TEST_F(OrderBookTest, T10_CancelLastOrderAtBestLevel_BestUpdates) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 买盘挂 100（只有 1 张）和 99
    // TODO Act:     撤掉 100 那张
    // TODO Assert:
    //   - best_bid 变成 99
    //   - snapshot 里买盘只剩 99 这一档
    //   - 再把 99 撤掉 → best_bid 变成空
}

// =============================================================================
// 输入校验
// =============================================================================

// qty <= 0 被拒绝
TEST_F(OrderBookTest, Reject_InvalidQty) {
    GTEST_SKIP() << "TODO";
    // TODO: 数量为 0、为负数各下一单
    // TODO Assert: status 是 InvalidQty；accepted() 为 false；没有成交；簿是空的
    // 思考：如果簿里有对手盘，非法订单会不会误吃掉对手盘？可以加一个对手盘再测
}

// price <= 0 被拒绝
TEST_F(OrderBookTest, Reject_InvalidPrice) {
    GTEST_SKIP() << "TODO";
    // TODO: 价格为 0、为负数各下一单
    // TODO Assert: status 是 InvalidPrice；簿没有变化
}

// 重复 id 被拒绝，而且不能有任何副作用
TEST_F(OrderBookTest, Reject_DuplicateIdOfRestingOrder_NoSideEffects) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 买单 id 1 挂在 100；卖盘挂一张别的单
    // TODO Act:     用 id 1 再下一张能和卖盘成交的单
    // TODO Assert:
    //   - status 是 DuplicateId，trades 为空
    //   - 卖盘没有被吃掉；原来的 id 1 的价格、剩余量都没变
}

// v1 已知局限：已经离开簿的 id 可以复用（见 map_orderbook.cpp 里 add() 的注释）
// 用测试把当前行为固定下来，以后改了规则这个测试会提醒你
TEST_F(OrderBookTest, KnownLimitation_IdOfFilledOrderCanBeReused) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: id 1 挂单后被完全吃掉（或被撤掉）
    // TODO Act:     再用 id 1 下一张单
    // TODO Assert:  accepted() 为 true
}

// =============================================================================
// 观察接口
// =============================================================================

// snapshot 按价位聚合，最优价格在前
TEST_F(OrderBookTest, Snapshot_AggregatesLevelsBestFirst) {
    GTEST_SKIP() << "TODO";
    // TODO Arrange: 买盘：100 挂 2 张、99 挂 1 张；卖盘：101、102 各 1 张（打乱挂单顺序）
    // TODO Assert:  snapshot().bids / .asks 和你手写的 std::vector<LevelView> 相等
    //   - bids 从高到低，asks 从低到高
    //   - 每档的 total_qty 是该档所有订单剩余量之和，order_count 是订单数
    // 提示：LevelView 是聚合体，可以写 LevelView{100, 15, 2}
}

}