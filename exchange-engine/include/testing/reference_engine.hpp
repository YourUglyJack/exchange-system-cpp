// 用简单的数据结构，再实现一次撮合逻辑，与 Engine 对拍

// 所有品种的挂单都在 vector里，每次 找 best price 都 linear scan

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <variant>
#include <vector>

#include "book/snapshot.hpp"
#include "common/config.hpp"
#include "engine/events.hpp"
#include "engine/requests.hpp"

namespace exchange::testing {

class ReferenceEngine {
public:
    using Out = std::variant<Trade, ExecReport>;

    bool add_symbol(const SymbolConfig& cfg) {
        if (cfg.tick_size <= 0 || cfg.lot_size <= 0) return false;
        return cfgs_.try_emplace(cfg.id, cfg).second;
    }

    std::vector<Out> handle(const Request& req) {
        out_.clear();
        std::visit([this](const auto& r) { on(r); }, req);
        return out_;

    }

    BookSnapshot snapshot(SymbolId sym) const {
        std::map<Price, LevelView, std::greater<Price>> bids;
        std::map<Price, LevelView> asks;
        for (const Ord& o : book_) {
            if (o.sym != sym) continue;
            // [] 不存在则创建，lv 则 引用 bids / asks 里的 对象
            // 所以 bids / asks 可以同步更新
            LevelView& lv = o.side == Side::Buy ? bids[o.price] : asks[o.price];
            lv.price = o.price;
            lv.total_qty += o.leaves;
            lv.order_count += 1;
        }
        BookSnapshot s;
        for (const auto& kv : bids) s.bids.push_back(kv.second);
        for (const auto& kv : asks) s.asks.push_back(kv.second);
        return s;
    }

private:
    struct Ord {
        SymbolId sym;
        OrderId id;
        Side side;
        Price price;
        Qty leaves;
        Qty cum;
        std::uint64_t seq;
    };

    std::map<SymbolId, SymbolConfig> cfgs_;
    std::vector<Ord> book_; 
    std::uint64_t next_seq_ = 0;
    std::vector<Out> out_;  // std::variant<Trade, ExecReport>

    void exec(SymbolId sym, OrderId id, ExecType t, Price px, Qty qty, Qty leaves, Qty cum, Qty total) {
        ExecReport e{0, 0, id, sym, t, RejectReason::None, px, qty, leaves, cum, total};
        out_.push_back(e);
    }

    void reject(SymbolId sym, OrderId id, RejectReason why) {
        ExecReport e{0, 0, id, sym, ExecType::Rejected, why, 0, 0, 0, 0, 0};
        out_.push_back(e);
    }

    Ord* find(SymbolId sym, OrderId id) {
        for (Ord& o : book_) {
            if (o.sym == sym && o.id == id) {
                return &o;
            }
        }
        return nullptr;
    }

    void erase(SymbolId sym, OrderId id) {
        auto find_out = [&](const Ord& o) {
            return o.sym == sym && o.id == id;
        };
        // remove_if 把需要的elem 往前面 移动，返回 新的 end；此时只是移动元素
        auto new_end = std::remove_if(book_.begin(), book_.end(), find_out);
        // 删除 整理后，多余的 尾部
        book_.erase(new_end, book_.end());
    }
    
    // 价格是否交叉
    static bool can_trade(Side taker_side, Price limit, Price maker_px) {
        return taker_side == Side::Buy ? limit >= maker_px : limit <= maker_px;
    }

    // 返回的是 下标
    long best_maker(SymbolId sym, Side taker_side, Price limit) const {
        long best = -1;
        for (std::size_t i = 0; i < book_.size(); ++i) {
            const Ord& o = book_[i];
            if (o.sym != sym || o.side == taker_side || !can_trade(taker_side, limit, o.price)) {
                continue;
            }
            if (best < 0) {
                best = static_cast<long>(i);
                continue;
            }
            const Ord& b = book_[static_cast<std::size_t>(best)];
            // taker 买 找 卖盘， 卖盘 价格越低越好
            const bool better_price = taker_side == Side::Buy ? o.price < b.price : o.price > b.price;
            // 价格优先，当价格相同的时候，时间优先，也就是 seq 小的那个
            if (better_price || (o.price == b.price && o.seq < b.seq)) best = static_cast<long>(i);
        }
        return best;
    }

    void run(SymbolId sym, OrderId id, Side side, Price limit, Qty leaves, Qty cum, TimeInForce tif, bool can_rest) {
        const Qty total = leaves + cum; // 剩余 + 累计成交
        // FOK 预检查
        if (tif == TimeInForce::FOK) {
            Qty avail = 0;
            for (const Ord& o : book_) {
                if (o.sym == sym && o.side != side && can_trade(side, limit, o.price)) {
                    avail += o.leaves;
                }
            }
            if (avail < leaves) {
                return exec(sym, id, ExecType::Canceled, 0, 0, 0, cum, total);
            }
        }
        // taker 开始吃单
        while (leaves > 0) {
            const long i = best_maker(sym, side, limit);
            if (i < 0) {
                break;
            }
            Ord m = book_[static_cast<std::size_t>(i)];
            const Qty q = std::min(leaves, m.leaves);
            leaves -= q;
            cum += q;
            // 更新 maker 单
            m.leaves -= q;
            m.cum += q;
            Trade t{0, 0, sym, m.id, id, side, m.price, q};
            out_.push_back(t);
            // maker order 播报
            exec(sym, m.id, m.leaves == 0 ? ExecType::Filled : ExecType::PartialFill, m.price, q, m.leaves, m.cum, m.leaves + m.cum);
            // taker order 播报
            exec(sym, id, leaves == 0 ? ExecType::Filled : ExecType::PartialFill, m.price, q, leaves, cum, total);
            
            if (m.leaves == 0) {
                // maker 被吃完了，删除 
                book_.erase(book_.begin() + i);
            } else {
                // maker 没有被吃完，更新
                book_[static_cast<std::size_t>(i)] = m;
            }
        }

        if (leaves == 0) {
            return;
        }
        // 有剩余，是否可以挂单
        if (tif != TimeInForce::GTC || !can_rest) {
            return exec(sym, id, ExecType::Canceled, 0, 0, 0, cum, total);
        }
        // 挂单
        book_.push_back(Ord{sym, id, side, limit, leaves, cum, next_seq_++});
    }

    // 创建新订单
    void on(const NewOrder& r) {
        const auto c = cfgs_.find(r.symbol);
        if (c == cfgs_.end()) return reject(r.symbol, r.id, RejectReason::UnknownSymbol);
        const SymbolConfig& cfg = c->second;
        const bool mkt = r.type == OrderType::Market;
        if (mkt && r.tif == TimeInForce::GTC) return reject(r.symbol, r.id, RejectReason::InvalidTif);
        if (r.qty <= 0) return reject(r.symbol, r.id, RejectReason::InvalidQty);
        if (r.qty % cfg.lot_size != 0) return reject(r.symbol, r.id, RejectReason::BadLotSize);
        if (!mkt && r.price <= 0) return reject(r.symbol, r.id, RejectReason::InvalidPrice);
        if (!mkt && r.price % cfg.tick_size != 0) return reject(r.symbol, r.id, RejectReason::BadTickSize);
        if (find(r.symbol, r.id)) return reject(r.symbol, r.id, RejectReason::DuplicateId);

        exec(r.symbol, r.id, ExecType::Accepted, 0, 0, r.qty, 0, r.qty);
        const Price limit = mkt ? (r.side == Side::Buy ? kMarketBuyLimit : kMarketSellLimit) : r.price;
        run(r.symbol, r.id, r.side, limit, r.qty, 0, r.tif, !mkt);
    }

    void on(const CancelOrder& r) {
        if (!cfgs_.count(r.symbol)) return reject(r.symbol, r.id, RejectReason::UnknownSymbol);
        const Ord* o = find(r.symbol, r.id);
        if (!o) return reject(r.symbol, r.id, RejectReason::UnknownOrder);
        exec(r.symbol, r.id, ExecType::Canceled, 0, 0, 0, o->cum, o->cum + o->leaves);
        erase(r.symbol, r.id);
    }

    void on(const ModifyOrder& r) {
        const auto c = cfgs_.find(r.symbol);
        if (c == cfgs_.end()) return reject(r.symbol, r.id, RejectReason::UnknownSymbol);
        const SymbolConfig& cfg = c->second;

        // 与 MatchingEngine 一致：先校验请求参数，再检查目标订单。
        if (r.new_qty <= 0) return reject(r.symbol, r.id, RejectReason::InvalidQty);
        if (r.new_qty % cfg.lot_size != 0) return reject(r.symbol, r.id, RejectReason::BadLotSize);
        if (r.new_price <= 0) return reject(r.symbol, r.id, RejectReason::InvalidPrice);
        if (r.new_price % cfg.tick_size != 0) return reject(r.symbol, r.id, RejectReason::BadTickSize);

        Ord* o = find(r.symbol, r.id);
        if (!o) return reject(r.symbol, r.id, RejectReason::UnknownOrder);

        // 价格和剩余量都不变时拒绝本次改单，保留原订单。
        if (r.new_price == o->price && r.new_qty == o->leaves) {
            return reject(r.symbol, r.id, RejectReason::NoChange);
        }

        exec(r.symbol, r.id, ExecType::Replaced, 0, 0, r.new_qty, o->cum, o->cum + r.new_qty);
        if (r.new_price == o->price && r.new_qty < o->leaves) {
            o->leaves = r.new_qty;  // 保留 seq → 保留时间优先级
            return;
        }
        const Ord old = *o;
        erase(r.symbol, r.id);
        run(r.symbol, r.id, old.side, r.new_price, r.new_qty, old.cum, TimeInForce::GTC, true);
    }
};


}
