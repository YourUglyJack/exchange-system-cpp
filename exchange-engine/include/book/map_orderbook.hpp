// =============================================================================
// v1 → v2 最大的结构变化：
// v1 的 OrderBook::add() 一个函数包办了"校验 → 撮合 → 挂单"。
// v2 有了订单类型和改单，"撮合完要不要挂单" "撮合前要不要先检查量够不够" 都取决于订单类型——这些是【业务规则】，属于 MatchingEngine。
// 所以 OrderBook 被拆成几个不含业务判断的【原语】：
//
//     原语             作用                                   谁会用到
//     ───────────────  ─────────────────────────────────────  ─────────────────
//     match()          用 taker 吃对手盘，产出 Fill 列表         所有新单、改价单
//     rest()           把订单挂到本方队尾                        GTC 剩余、改单剩余
//     remove()         按 id 摘除订单                          撤单、改单（失去优先级）
//     reduce()         原地减少剩余量，不动队列位置               改单（保留优先级）
//     fillable_qty()   只读：限价内对手盘能成交多少               FOK 预检查
//
//   校验（重复 id、tick/lot）也移到了引擎：校验规则依赖品种配置，
//   订单簿不知道配置。订单簿只在 Debug 下用不变量保证自己的状态正确。
//
// 【这种拆分叫什么？】"机制与策略分离"（ mechanism vs policy）：
//   订单簿提供机制（怎么撮合、怎么排队），引擎决定策略（什么时候撮合、剩余怎么办）。
//   新增订单类型（v2 练习的 Post-only、Stop）几乎只改引擎，不改订单簿。
//
// 数据结构与 v1 完全相同：map 价位 + list 队列 + unordered_map 索引。
// =============================================================================

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
#include <vector>

#include "book/order.hpp"
#include "common/types.hpp"
#include "book/snapshot.hpp"

// hpp 在定义 数据结构和接口，没有具体实现

namespace exchange {

class OrderBook {
public:

    static constexpr std::size_t kAllLevels = std::numeric_limits<std::size_t>::max();
    // remove() 返回值： 被摘掉的订单，摘除后 该价位的 qty
    struct Removed {
        Order order;
        Qty level_total;
    };
    
    // 传入 out，Engine 可以一直用同一个 vector
    // 每次 clear 的时候 把size = 0，已经分配的容量孩子啊，热路径上就不会 频繁 堆分配
    // V1 的 add 每次都要 创建 vector
    void match(Order& taker, std::vector<Fill>& out);

    // 把 order 挂到 队尾，返回挂单后，该价位的总量
    // 给 Engine调用的入口，rest_impl是内部实现
    // Engine 不需要 知道 内部 的 bid_, asks_ , 这些 都由 订单簿完成
    // Engine 只需要 告诉 挂 那一个订单，其他的事情 由 订单簿去处理
    Qty rest(Order order);

    // 按照 id 摘掉订单，不存在返回 nullopt
    std::optional<Removed> remove(OrderId id);

    // 改单
    // 直接把剩余量 改成 new_qty, 0 < new_qty <= cur remainning
    std::optional<Qty> reduce(OrderId id, Qty new_qty);

    // ------------- 只读查询 ----------------------------
    // FOK 预检查
    Qty fillable_qty(Side taker_side, Price limit, Qty nned) const;
    const Order* find(OrderId id) const;

    std::optional<Price> best_bid() const; // 返回 Price 或者 没有 Price, 因为可能没有买单 std::nullopt
    std::optional<Price> best_ask() const;
    std::optional<Qty> remaining_qty(OrderId id) const;

    std::size_t order_count() const noexcept { return index_.size(); }
    bool contains(OrderId id) const { return index_.count(id) != 0; }

    void print(std::ostream& os, std::size_t depth = kAllLevels) const;
    
    BookSnap snapshot(std::size_t depth = kAllLevels) const;
    // 检查 orderbook 内 所有不变量
    bool check_invariants(std::string* why = nullptr) const;

private:
    // 买盘，价格高优先, greater
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    // 卖盘，价格低优先，less
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;
    // 快速定位某个 挂单
    struct Locator { 
        Side side;
        Price price; // O(logP)
        std::list<Order>::iterator it; // list 的迭代器 只有被删除的才会失效
    };

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, Locator> index_;
    std::uint64_t next_seq_{0};

    template <class OppositeLevels>
    void match_impl(Order& taker, OppositeLevels& opposite, std::vector<Fill>& out);

    template <class OwnLevels>
    Qty rest_impl(const Order& order, OwnLevels& own);

    template <class Levels>
    Qty erase_impl(Levels& levels, const Locator& loc);

    template <class Levels>
    Qty fillable_impl(Levels& opposite, Side taker_side, Price limit, Qty need) const;

    template <class Levels>
    bool check_side(const Levels& levels, Side side, std::size_t& order_total, std::string* why) const;
};
}


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
