#include <gtest/gtest.h>
#include <vector>

#include "book/map_orderbook.hpp"

using namespace exchange;


// 测试 订单簿 原语

namespace {

class BookTest : public :: testing::Test {
protected:
    OrderBook book;
    std::vector<Fill> fills;

    void TearDown() override {
        std::string why;
        EXPECT_TRUE(book.check_invariants(&why)) << why;
    }

    void put(OrderId id, Side s, Price px, Qty q) {
        book.rest(Order{id, s, px, q, 0, 0});
    }
};

// 测试，吃掉多单，会产生多个 Fill，一个Fill 就是一笔交易快照
TEST_F(BookTest, MatchProducesFillsWithMakerStateAndLevelTotal) {
    put(1, Side::Sell, 100, 4);
    put(2, Side::Sell, 100, 6);

    Order taker{3, Side::Buy, 100, 5, 0, 0};
    book.match(taker, fills);

    ASSERT_EQ(fills.size(), 2u);
    // fill 1: maker 1 被吃光
    EXPECT_EQ(fills[0].maker_id, 1);
    EXPECT_EQ(fills[0].qty, 4);
    EXPECT_EQ(fills[0].maker_cum, 4);
    EXPECT_EQ(fills[0].maker_leaves, 0);
    EXPECT_EQ(fills[0].level_total, 6);
    // fill 2: maker 2 被吃了 1股，还剩5股
    EXPECT_EQ(fills[1].maker_id, 2u);
    EXPECT_EQ(fills[1].qty, 1);
    EXPECT_EQ(fills[1].maker_cum, 1);
    EXPECT_EQ(fills[1].maker_leaves, 5);
    EXPECT_EQ(fills[1].level_total, 5);
    // taker
    EXPECT_EQ(taker.qty, 0);
    EXPECT_EQ(taker.cum_qty, 5);
}

// 市价单，全部吃掉，有rest 不挂单
TEST_F(BookTest, MatchWithInfiniteLimitSweepsEverything) {
    put(1, Side::Buy, 90, 1);
    put(2, Side::Buy, 50, 1);
    put(3, Side::Buy, 30, 1);

    Order taker{4, Side::Sell, kMarketSellLimit, 10, 0, 0};
    book.match(taker, fills);

    ASSERT_EQ(fills.size(), 3u);
    EXPECT_EQ(taker.cum_qty, 3);
    EXPECT_EQ(taker.qty, 7); // qty 是剩余 qty
    EXPECT_FALSE(book.contains(1));
    EXPECT_FALSE(book.contains(2));
    EXPECT_FALSE(book.contains(3));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

// remove 某个订单，返回值是 当前 价格档位 还有 多少 qty
TEST_F(BookTest, RestAndRemoveReturnLevelTotal) {
    EXPECT_EQ(book.rest(Order{1, Side::Buy, 100, 3, 0, 0}), 3);
    EXPECT_EQ(book.rest(Order{2, Side::Buy, 100, 7, 0, 0}), 10);

    const auto r = book.remove(1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->order.id, 1);
    EXPECT_EQ(r->level_total, 7);
    EXPECT_FALSE(book.remove(1).has_value());

    const auto r2 = book.remove(2);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->level_total, 0);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(BookTest, ReduceKeepsQueuePosition) {
    put(1, Side::Sell, 100, 10);
    put(2, Side::Sell, 100, 10);

    // order 1 减少到 3
    EXPECT_EQ(book.reduce(1, 3), 13);

    Order taker{3, Side::Buy, 100, 4, 0, 0};
    book.match(taker, fills);

    ASSERT_EQ(fills.size(), 2u);

    EXPECT_EQ(fills[0].maker_id, 1);  // 证明 改单 减少 qty，不会更改 位次
    EXPECT_EQ(fills[0].qty, 3);
    EXPECT_EQ(fills[0].maker_cum, 3);
    EXPECT_EQ(fills[0].maker_leaves, 0);
    
    EXPECT_EQ(fills[1].maker_id, 2u);
    EXPECT_EQ(fills[1].qty, 1);
    EXPECT_EQ(fills[1].maker_cum, 1);
    EXPECT_EQ(fills[1].maker_leaves, 9);
}

TEST_F(BookTest, ReduceUnknownReturnsNullopt) {
    EXPECT_FALSE(book.reduce(1, 10));
}

TEST_F(BookTest, FillableQtyStopsAtLimitAndAtNeed) {
    // 如果 不 cross， 直接 返回 0
    // 如果 cross，但没满足 need， 返回 最多 能被 吃多少
    // 如果 cross，满足 need， 返回 满足 那一刻 的 cum_qty, so 返回值 >= need
    put(1, Side::Sell, 100, 5);
    put(2, Side::Sell, 101, 5);
    put(3, Side::Sell, 103, 5);

    EXPECT_EQ(book.fillable_qty(Side::Buy, 99, 100), 0); // 没 crosses，返回 0
    EXPECT_EQ(book.fillable_qty(Side::Buy, 102, 100), 10); // crosses，返回最多 能吃多少单
    EXPECT_EQ(book.fillable_qty(Side::Buy, 101, 3), 5); // 第一个 maker 就够了，返回
    EXPECT_EQ(book.fillable_qty(Side::Buy, kMarketBuyLimit, 100), 15); // 第一个 maker 就够了，返回
    
    EXPECT_EQ(book.fillable_qty(Side::Sell, 100, 1), 0);
    
    EXPECT_EQ(book.order_count(), 3u); // fillable_qty 只是 检查，不做任何 操作
}

TEST_F(BookTest, SnapshotDepthLimitsLevels) {
    for (Price p = 1; p <= 5; ++p) {
        put(p, Side::Buy, p, 1);
    }
    ASSERT_EQ(book.order_count(), 5u);

    const BookSnapshot s = book.snapshot(2); // 只拷贝 2 个档位
    
    ASSERT_EQ(s.bids.size(), 2u);
    EXPECT_EQ(s.bids[0].price, 5);
    EXPECT_EQ(s.bids[1].price, 4);

    const BookSnapshot s_defulat = book.snapshot();
    EXPECT_EQ(s_defulat.bids.size(), 5u);
}
}
