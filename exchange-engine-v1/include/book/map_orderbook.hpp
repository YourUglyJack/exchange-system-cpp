// =================================
// std::map + std::list + unordered_map
// =================================
// 获取最优价格: best_bid / best_ask, O(1)
// map 是红黑树，找价格的时间复杂度应当是 O(logP)
// 同一个价位 FIFO，优先处理队头，后来的 从队尾插入，time priority
// 按照 id 撤单，需要index_ 辅助 快速 locate 到 node

// 快速获取价格，为啥不用 heap？
// 堆只能 高效访问 / 删除 最值， 撤单 需要 del 中间值，堆不好搞
// 虽然 lazy delete 可以实现，但是会让逻辑复杂

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <list>
#include <map>
#include <string>
#include <unordered_map>

#include "book/order.hpp"
#include "book/results.hpp"
#include "common/types.hpp"

// hpp 在定义 数据结构和接口，没有具体实现

namespace lob {

class OrderBook {
public:
    // 添加 未成交的订单
    AddResult add(OrderId id, Side side, Price price, Qty qty);
    // 撤单，false: id 不在 orderbook 里面，从未存在 / 全部成交 / 已经被撤回
    bool cancel(OrderId id);
    std::optional<Price> best_bid() const; // 返回 Price 或者 没有 Price, 因为可能没有买单 std::nullopt
    std::optional<Price> best_ask() const;
    // 某个挂单的剩余数量，测试用，检查 maker 被部分成交
    std::optional<Qty> remaining_qty(OrderId id) const;
    // 挂单数
    std::size_t order_count() const noexcept { return index_.size(); }
    // 某 order 是否还在 orderbook 上
    bool contains(OrderId id) const { return index_.count(id) != 0; }
    // 按价位聚合的快照，最优价格在前，测试用
    BookSnap snapshot() const;
    // show orderbook
    void print(std::ostream& os) const;
    // 检查 orderbook 内 所有不变量
    bool check_invariants(std::string* why = nullptr) const;

private:
    // 买盘，价格高优先, greater
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    // 卖盘，价格低优先，less
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    BidLevels bids_;
    AskLevels asks_;

    // 快速定位某个 挂单
    struct Locator { 
        Side side;
        Price price; // O(logP)
        std::list<Order>::iterator it; // list 的迭代器 只有被删除的才会失效
    };

    // 索引
    std::unordered_map<OrderId, Locator> index_;

    // 下一个可分配的 seq，只有 order 进入 orderbook 的时候 递增
    std::uint64_t next_seq_{0};

    // ================
    //     helper
    // ================

    // taker 去吃 对手盘，直到 taker 吃饱，对手盘空 / 不再满足成交条件
    template <class OppositeLevels>
    void match(Order& taker, OppositeLevels& opposite, std::vector<Trade>& out);

    template <class OwnLevels>
    void reset(const Order& other, OwnLevels& own);

    // 删除 order，当 该 price 档位 没有 order得时候，删除该 档位
    template <class Levels>
    void erase_order(Levels& levels, const Locator& loc);

    template <class Levels>
    bool check_side(const Levels& levels, Side side, std::size_t& order_total, std::string* why) const;
};
}