#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator> 
#include <string>
#include <vector>

#include "book/map_orderbook.hpp"

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
    std::vector<LevelView> collect_levels(const Levels& levels, std::size_t depth) {
        std::vector<LevelView> out;
        out.reserve(std::min(levels.size(), depth));
        for (const auto& [price, level] : levels) {
            if (out.size() >= depth) break; // Top-N
            out.push_back(LevelView{price, level.total_qty, level.orders.size()});
        }
        return out;
    }

} // namespace

// ==========================
//      match 撮合
// =========================
void OrderBook::match(Order& taker, std::vector<Fill>& out) {
    if (taker.side == Side::Buy) {
        // 在成员函数内部，可以直接访问当前对象的其他成员
        // 编译器会隐式生成 this->
        match_impl(taker, asks_, out);
    } else {
        match_impl(taker, bids_, out);
    }
    LOB_CHECK_INVARIANTS();
}


template <class OppositeLevels> // 对手盘
void OrderBook::match_impl(Order& taker, OppositeLevels& opposite, std::vector<Fill>& out) {
    // taker 还有 余量 && 对手盘 没空; opposite is map, Price: PriceLevel
    while (taker.qty > 0 && !opposite.empty()) {
        // iter 最优档位，不断 吃
        auto level_it = opposite.begin(); // 获取最优价格的迭代器
        const Price level_price = level_it->first;
        // 价格没有交叉，无法成交
        if (!crosses(taker.side, taker.price, level_price)) {
            // buy, 去 check 卖盘:  taker.price >= level_price
            // sell, 去 check 买盘: taker.price <= level_price
            break;
        } 
        // 开始吃, taker 还有余量，且这个档位的 order 没有吃完
        PriceLevel& level = level_it->second; // PriceLevel: orders, total_qty
        while (taker.qty > 0 && !level.orders.empty()) {
            Order& maker = level.orders.front(); // level 是 PriceLevel 结构体，链表在 orders里，取出 最优 Order FIFO
            const Qty fill = std::min(taker.qty, maker.qty);

            taker.qty -= fill;
            taker.cum_qty += fill; // v2 累计成交量
            maker.qty -= fill;
            maker.cum_qty += fill; // v2 累计成交量
            level.total_qty -= fill;

            // v1 产出 Trade
            // 只产出 Fill，把 Fill 交给 Engine 自行去 决定怎么用
            out.push_back(Fill{
                maker.id,
                level_price,
                fill, // 该笔 成交量 
                maker.qty, // 剩余量
                maker.cum_qty,
                level.total_qty // 成交后，该价位 剩余总挂单量，方便 BookUpdae 播报
            });

            if (maker.qty == 0) {
                // 该 maker 单 被吃完了，需要 删除 该 node
                index_.erase(maker.id);
                level.orders.pop_front();
            }
        }
        if (level.orders.empty()) {
            // 删除这个 key-value
            opposite.erase(level_it);
        }
    }
}

// ==========================
//      rest 挂单
// =========================
Qty OrderBook::rest(Order order) {
    // order 的 seq 主要是用来 检查 排队顺序
    order.seq = next_seq_++; // v1 是在 add 里分配， v2 在订单簿内部分配
    const Qty total = order.side == Side::Buy ? rest_impl(order, bids_) : rest_impl(order, asks_);
    LOB_CHECK_INVARIANTS();
    return total;
}

template <class OwnLevels>
Qty OrderBook::rest_impl(const Order& order, OwnLevels& own) {
    // 存在就取出，不存在就创建
    // own: bids_ / asks_
    PriceLevel& level = own[order.price]; // 取出这个价位
    
    level.orders.push_back(order);
    level.total_qty += order.qty;

    // 获取 刚刚插入Order 的 迭代器，写入 index_
    const auto it = std::prev(level.orders.end());
    index_.emplace(order.id, Locator{order.side, order.price, it});
    return level.total_qty;
}

// ===================================================
//      remove 摘掉某个订单 // TODO 有空再写一次
// ==================================================
std::optional<OrderBook::Removed> OrderBook::remove(OrderId id) {
    // 在 index_ 中，根据 OrderId 找到 Order的迭代器，也就是 list 中 的 某个 node
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return std::nullopt;
    }
    // index_: OrderId -> Locator{side, price, list_it}
    Locator& loc = found->second;
    Order o = *(loc.it);
    Qty total = loc.side == Side::Buy ? erase_impl(bids_, loc) : erase_impl(asks_, loc);
    // 索引 也要删除这个 node
    index_.erase(found);

    LOB_CHECK_INVARIANTS();
    return Removed{o, total};
}

template <class Levels>
Qty OrderBook::erase_impl(Levels& levels, const Locator& loc) {
    // 找到 价格档位
    const auto level_it = levels.find(loc.price); // 肯定存在，调用方已经验证
    // 提出 orders 对 it 进行删除, 
    PriceLevel& level = level_it->second; // 提取 value
    level.total_qty -= loc.it->qty; // 更新 剩余 该档位 订单数
    level.orders.erase(loc.it); // 删掉 Order node
    const Qty total = level.total_qty; // 删除 level_it 会引用悬空，所以先保存 total
    if (level.orders.empty()) {
        // 如果这个价格档位空了，就要删除 key-value
        levels.erase(level_it); 
    }
    return total;
}

// ===================================================
//                reduce 修改订单
// ==================================================
// 减量 可以保持优先级，因为减量 对后面的人来说 只有好处的，
// 等待时间变短，加量不行，加量就是插队了
// Loctor {side, price, list_it}, 可以 直接修改 list_it
// 和 PriceLevel 对应档位 的 total_qty
std::optional<Qty> OrderBook::reduce(OrderId id, Qty new_qty) {
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return std::nullopt;
    }
    // 更新 Order的
    Locator& loc = found->second;
    const Qty delta = loc.it->qty - new_qty;
    loc.it->qty = new_qty;
    
    // 更新 价格档位的
    PriceLevel& level = loc.side == Side::Buy ? bids_[loc.price] : asks_[loc.price];
    level.total_qty -= delta;
    const Qty total = level.total_qty; // 怕之后 level 悬空，还是 保存值 比较保险

    LOB_CHECK_INVARIANTS();
    return total;
}

// ===================================================
//            fillable_qty FOK 预检查
// ==================================================
// 为啥不边撮合边检查？
// 撮合到最后，发现 不够吃，就要回滚， 回滚操作的代价太大了
// maker 要回复原状，恢复在队列原本的位置啥的，太麻烦
// 直接 read-only check 不好吗
Qty OrderBook::fillable_qty(Side taker_side, Price limit, Qty need) const {
    // 调用方 只需要 传入 side, limit，need； 其他的细节不用管
    return taker_side == Side::Buy ? fillable_impl(asks_, taker_side, limit, need) : fillable_impl(bids_, taker_side, limit, need);
}

template <class Levels>
Qty OrderBook::fillable_impl(Levels& opposite, Side taker_side, Price limit, Qty need) const {
    Qty sum = 0;
    for (const auto& [price, level] : opposite) { //opposit 是 map，迭代是有顺序的，所以这里从最优价格开始迭代
        if (!crosses(taker_side, limit, price)) {
            break;
        }
        sum += level.total_qty;
        if (sum >= need) {
            break;
        }
    }
    return sum;
}


// ====================================
//           查询接口
// ====================================
const Order* OrderBook::find(OrderId id) const {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &*(found->second.it);
}

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

BookSnap OrderBook::snapshot(std::size_t depth) const {
    return BookSnap{collect_levels(bids_, depth), collect_levels(asks_, depth)};
}

void OrderBook::print(std::ostream& os, std::size_t depth) const {
    os << "========== OrderBook ============\n";
    // asks 卖盘，用 rbegin 从高到低打印，最优（最低）卖价在最后一行，紧贴 ----
    const BookSnap& s = snapshot(depth);
    for (auto it = s.asks.rbegin(); it != s.asks.rend(); ++it) {
        os << "ASK " << it->price << " " << it->total_qty << " (" << it->order_count << ")\n";
    }
    os << "------------------\n";
    // bids 买盘，降序，刚好是 iter from positive direction
    for (const auto& level_view : s.bids) {
        os << "BID " << level_view.price << " " << level_view.total_qty << " (" << level_view.order_count << ")\n";
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
// 8. cum_qty Order 的累计成交量不能是 负数
template <class Levels>
bool OrderBook::check_side(const Levels& levels, Side side, std::size_t& order_total, std::string* why) const {
    auto fail = [why](const std::string& msg) {
        if(why) *why = msg;
        return false;
    };

    for (const auto& [price, level] : levels) {
        const std::string where = std::string(to_string(side)) + " level" + std::to_string(price);
        
        if (level.orders.empty()) {
            return fail("[I1] empty" + where);
        }

        Qty sum = 0;
        bool first = true;
        std::uint64_t prev_seq = 0;
        for (const Order& o : level.orders) {
            const std::string who = where + " order " + std::to_string(o.id);
            
            if (o.qty <= 0) return fail("[I2] non-positive qty at " + who);
            if (o.cum_qty < 0) return fail("[I8] negative cum_qty at " + who);
            if (o.side != side || o.price != price) return fail("[I4] misplaced " + who);
            if (!first && o.seq <= prev_seq) return fail("[I5] FIFO broken at " + who);

            const auto found = index_.find(o.id);
            if (found == index_.end()) return fail("[I6] missing from index: " + who);
            const Locator& loc = found->second;
            if (loc.side != o.side || loc.price != o.price || &*loc.it != &o) {
                return fail("[I6] stale locator for " + who);
            }

            sum += o.qty;
            prev_seq = o.seq;
            first = false;
            ++order_total;
        }
        if (sum != level.total_qty) return fail("[I3] total_qty mismatch at " + where);
    }
    return true;
}

bool OrderBook::check_invariants(std::string* why) const {
    std::size_t order_total = 0;
    if (!check_side(bids_, Side::Buy, order_total, why)) return false;
    if (!check_side(asks_, Side::Sell, order_total, why)) return false;
    if (order_total != index_.size()) {
        if (why) *why = "[I6] index size != orders in book";
        return false;
    }
    if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first) {
        if (why) *why = "[I7] crossed book";
        return false;
    }
    return true;
}
}
