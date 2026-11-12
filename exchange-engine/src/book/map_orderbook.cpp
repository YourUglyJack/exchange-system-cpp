#include "book/map_orderbook.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator> 
#include <string>
#include <vector>


namespace exchange {

#ifndef NDEBUG
#define LOB_CHECK_INVARIANTS() \
    do { \
        std::string lob_why_; \
        if (!check_invariants(&lob_why_)) { \
            std::cerr << "[exchange] invariant violated: " << lob_why_ << "\n"; \
            std::abort(); \
        } \
    } while (0)
#else
#define LOB_CHECK_INVARIANTS() ((void)0)
#endif

namespace {
    // 买单，去卖盘check, taker_price >= best_asks，就可以 take； 卖盘 best 是 min，卖得越low越容易成交
    // 卖单，去买盘check, taker_price <= best_bids, 就可以 take；买盘 best 是max，出价越高越容易成交
    bool crosses(Side taker_side, Price taker_price, Price level_price) noexcept {
        return taker_side == Side::Buy ? taker_price >= level_price : taker_price <= level_price;
    }

    // LevelView，某个价格档位的快照, price, total_qty, order_count
    // 把 asks / bids 盘的 所有价格档位，弄成列表，方便check
    template <class Levels>
    std::vector<LevelView> collect_levels(const Levels& levels) {
        std::vector<LevelView> out;
        out.reserve(levels.size());
        for (const auto& [price, level] : levels) {
            out.push_back(LevelView{price, level.total_qty, level.orders.size()});
        }
        return out;
    }

} // namespace

AddResult OrderBook::add(OrderId id, Side side, Price price, Qty qty) {
    AddResult res;
        
        // 1. Check
    if (qty <= 0) {
        res.status = AddStatus::InvalidQty;
        return res;
    }
    if (price <= 0) {
        res.status = AddStatus::InvalidPrice;
        return res;
    }
    // V1: 只检查 当前挂单中，是否有重复 Id
    // Limit: 一张 全部成交 / 全部 cancel 的 Order，Id 已经从 Index_  中删除，可以被重新使用
    if (index_.count(id) > 0) {
        res.status = AddStatus::DuplicateId;
        return res;
    }

    // 2. Create taker
    Order taker{id, side, price, qty, 0};

    if (side == Side::Buy) {
        match(taker, asks_, res.trades);
    } else {
        match(taker, bids_, res.trades);
    }

    if (taker.qty > 0) {
        taker.seq = next_seq_++; // 先赋值，后++
        if (side == Side::Buy) {
            rest(taker, bids_);
        } else {
            rest(taker, asks_);
        }

    }
    LOB_CHECK_INVARIANTS();
    return res;
}
// =============================================================================
// Match
// 两层循环
//      外层：iter 价位，从对手盘最优价格开始
//      内层：同一个价位内，从队头开始，挨个 taker，直到 吃饱
// 每一次 内层 taker，都会 gen 一个 Trade
// 循环终止条件：
// 1. taker 全部成交， taker.qty == 0
// 2. 对手盘空了， opposite.empty()
// 3. 对手盘最优价格 已经超过 taker的限价，!crosses, 也就是没办法成交，价格太垃圾了
// =============================================================================
template <class OppositeLevels>
void OrderBook::match(Order& taker, OppositeLevels& opposite, std::vector<Trade>& out) {
    while (taker.qty > 0 && !opposite.empty()) {
        auto level_it = opposite.begin(); // 获取对手盘的最优价格的iter, map<Price, LevelPrice>
        const Price level_price = level_it->first; // 获取 具体价格
        
        // 价格是否 可以 taker，最优都无法满足，之后只会更差
        // Buy，check asks 卖盘，price >= best_asks
        // Sell, check bids 买盘, price <= best_bids
        if(!crosses(taker.side, taker.price, level_price)) {
            break;
        }

        // PriceLevel: orders, total_qty
        PriceLevel& level = level_it->second; // double list，存的是同一个价位的 order

        // 按照时间顺序，逐个成交
        // taker 没吃饱 + 该价格档位 还有得 吃
        while(taker.qty > 0 && !level.orders.empty()) {
            Order& maker = level.orders.front();
            
            // 成交量是 两者最小的
            const Qty fill = std::min(taker.qty, maker.qty);

            out.push_back(Trade{maker.id, taker.id, taker.side, level_price, fill});

            taker.qty -= fill;
            maker.qty -= fill;
            level.total_qty -= fill;

            if(maker.qty == 0) {
                // 当前 maker 全部成交，删除 index_，弹出队列
                index_.erase(maker.id);
                level.orders.pop_front();
            }
            // 如果 maker 没有成交完，说明 taker 已经吃饱了，fill == taker.qty
        }

        // 这个价位是否被吃空
        if (level.orders.empty()) {
            opposite.erase(level_it);
        }
    }
}

template <class OwnLevels>
void OrderBook::rest(const Order& order, OwnLevels& own) {
    // 存在就取出，不存在就创建
    // own: bids_ / asks_
    PriceLevel& level = own[order.price];
    
    level.orders.push_back(order);
    level.total_qty += order.qty;

    // 获取 刚刚插入Order 的 迭代器，写入 index_
    const auto it = std::prev(level.orders.end());
    index_.emplace(order.id, Locator{order.side, order.price, it});
}

bool OrderBook::cancel(OrderId id) {
    const auto found = index_.find(id); // 迭代器
    if (found == index_.end()) {
        return false; // 不存在，已经撤销，已经完成交易
    }
    const Locator loc = found->second;

    if(loc.side == Side::Buy) {
        erase_order(bids_, loc);
    } else {
        erase_order(asks_, loc);
    }
    index_.erase(found);

    LOB_CHECK_INVARIANTS();
    return true;
}

// Levels: bids_ / asks_
template <class Levels>
void OrderBook::erase_order(Levels& levels, const Locator& loc) {
    const auto level_it = levels.find(loc.price); // 找到对应档位
    PriceLevel& level = level_it->second; // 获取价位信息，LevelPrice: order, total_qty

    // loc 的 it 是 list 的迭代器，存放着 order
    level.total_qty -= loc.it->qty;
    level.orders.erase(loc.it);

    // 该价格档位空了就要删除这个价格档位
    if(level.orders.empty()) {
        levels.erase(level_it);
    }
}
// =============================================================================
// 查询接口
// =============================================================================
std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first; // key is price
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::optional<Qty> OrderBook::remaining_qty(OrderId id) const {
    // index_, OrderId -> Locator{side, price, literator}
    const auto found = index_.find(id); // list node
    if (found == index_.end()) {
        return std::nullopt;
    }
    return found->second.it->qty;
}

BookSnap OrderBook::snapshot() const {
    return BookSnap{collect_levels(bids_), collect_levels(asks_)};
}

void OrderBook::print(std::ostream& os) const {
    os << "========== OrderBook ============\n";
    // asks 卖盘，用 rbegin 从高到低打印，最优（最低）卖价在最后一行，紧贴 ----
    for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
        os << "ASK " << it->first << " " << it->second.total_qty << " (" << it->second.orders.size() << ")\n";
    }
    os << "------------------\n";
    // bids 买盘，降序，刚好是 iter from positive direction
    for (const auto& [price, level] : bids_) {
        os << "BID " << price << " " << level.total_qty << " (" << level.orders.size() << ")\n";
    }
    os << "================================\n";
}

// check_invariants
// 1. 没有空价位，即 每个价位至少有一个订单
// 2. 每张挂单的 qty > 0
// 3. 每个价位的 total_qty == 该价位所有订单 qty 之和
// 4. 订单 是否consistency side price 等
// 5. 同一个价位内的 seq 严格递增
// 6. 索引 与 OrderBook 一样，Orderbook的每个 Order 都在 index_ 里面，Locator 指向的 Order 没出错，# index_ == # orderbook 
// 7. best_bid < best_ask, 最高买价格 低于 最低卖价
template <class Levels>
bool OrderBook::check_side(const Levels& levels, Side side, std::size_t& order_total, std::string* why) const {
    // 记录失败原因并且返回 false
    auto fail = [why](const std::string& msg) {
        if (why) *why = msg;
        return false;
    };

    for (const auto& [price, level] : levels) {
        // BUY level 100 ？
        const std::string where = std::string(to_string(side)) + " level " + std::to_string(price);
        
        if (level.orders.empty()) {
            return fail("[I1] empty " + where);
        }

        Qty sum = 0;
        bool first = true;
        std::uint64_t prev_seq = 0;

        // 迭代 同一个 price 档位 的 Order 双向链表
        for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
            const Order& o = *it;
            const std::string who = where + " order " + std::to_string(o.id);

            if (o.qty <= 0) return fail("[I2] non-positive qty at " + who);
            if (o.side != side) return fail("[I4] wrong side at " + who);
            if (o.price != price) return fail("[I4] wrong price at " + who);
            if (!first && o.seq <= prev_seq) return fail("[I5] FIFO broken at " + who);

            // [I6] 订单在 index_ 里面，且 index_ 的 Locator 刚好指向这个 elem
            const auto found = index_.find(o.id);
            if (found == index_.end()) return fail("[I6] missing from index_ " + who);
            const Locator& loc = found->second; // found: id:Locator(side, price, list_it)
            if (loc.side != side || loc.price != price || &*loc.it != &o) {
                // loc.it is std::list<Order>::iterator
                // *loc.it 得到 it 指向的 Order, Order&
                // &(*loc.it) 取地址
                return fail("[I6] stale locator for " + who);
            }
            sum += o.qty;
            prev_seq = o.seq;
            first = false;
            ++order_total;
        }

        if (sum != level.total_qty) {
            return fail("[I3] total_qty mismatch at " + where + ": cached " + std::to_string(level.total_qty) + ", actual " + std::to_string(sum));
        }
    }

    return true;
}

bool OrderBook::check_invariants(std::string* why) const {
    std::size_t order_total = 0;
    if (!check_side(bids_, Side::Buy, order_total, why)) return false;
    if (!check_side(asks_, Side::Sell, order_total, why)) return false;

    if (order_total != index_.size()) {
        if (why) {
            *why = "[I6] index size " + std::to_string(index_.size()) + "!= orders in book " + std::to_string(order_total);
        }
        return false;
    }

    if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first) {
        if (why) {
            *why = "[I7] crossed book: best bid " + std::to_string(bids_.begin()->first) + " >= best ask " + std::to_string(asks_.begin()->first);        
        }
        return false;
    }
    return true;
}
}
