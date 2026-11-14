#include <cassert>
#include <chrono>
#include <variant>

#include "engine/matching_engine.hpp"

namespace exchange {

std::uint64_t steady_clock_ns() {
    // steady_clock: 单调递增 不受 系统改时间影响，适合测量和排序
    using namespace std::chrono;
    const auto elapsed = steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(duration_cast<nanoseconds>(elapsed).count());
}

MatchingEngine::MatchingEngine(EngineListener& listener, ClockFn clock) 
    : listener_(listener), clock_(clock) {
        fills_.reserve(64);
    }

bool MatchingEngine::add_symbol(const SymbolConfig& cfg) {
    if (cfg.tick_size <= 0 || cfg.lot_size <= 0) return false;
    // try_emplace: key 存在的时候啥也不做，返回 {迭代器， false }
    // 比 find 再 insert 少一次 查找
    return symbols_.try_emplace(cfg.id, SymbolState{cfg, OrderBook{}}).second;
}

MatchingEngine::SymbolState* MatchingEngine::find_symbol(SymbolId symbol) {
    const auto it = symbols_.find(symbol);
    return it == symbols_.end() ? nullptr : &it->second;
}

const OrderBook* MatchingEngine::book(SymbolId symbol) const {
    const auto it = symbols_.find(symbol);
    return it == symbols_.end() ? nullptr : &it->second.book;
}

const SymbolConfig* MatchingEngine::config(SymbolId symbol) const {
    const auto it = symbols_.find(symbol);
    return it == symbols_.end() ? nullptr : &it->second.cfg;
}

// =============================================================================
// handle：请求分发
// -----------------------------------------------------------------------------
// std::visit 会根据 variant 里实际存的类型，调用 lambda 并传入正确类型的引用。
// lambda 的参数是 auto（泛型 lambda），编译器为三种类型各生成一份，
// 每份里 on_request(r) 按重载决议选中对应的成员函数——全部在编译期完成。
// =============================================================================
void MatchingEngine::handle(const Request& req) {
    now_ns_ = clock_(); // 每个请求只取一次事件，同一个请求的事件共享时间戳
    auto visitor = [this](const auto& r) {
        // [this] 让 lambda 可以访问 MatchingEngine 对象，从而调用 on_request
        on_request(r);
    };
    // std::visit 用于访问 std::variant 的值
    // std::visit(处理函数, variant对象)， 取出 req 给 visitor 调用
    std::visit(visitor, req);
}
// ===============================
//           新订单
// ===============================
// 检验参数，发送 Accepted 事件，创建 taker 订单，执行
void MatchingEngine::on_request(const NewOrder& r) {
    // check 是否注册 该 symbol 的订单簿
    SymbolState* s = find_symbol(r.symbol);
    if (!s) {
        return emit_reject(r.symbol, r.id, RejectReason::UnknowSymbol);
    }
    
    // check 参数
    const SymbolConfig& cfg = s->cfg;
    const bool is_market = r.type == OrderType::Market;
    if (is_market && r.tif == TimeInForce::GTC) {
        return emit_reject(r.symbol, r.id, RejectReason::InvalidTif);
    }
    if (r.qty <= 0) return emit_reject(r.symbol, r.id, RejectReason::InvalidQty);
    if (r.qty % cfg.lot_size != 0) return emit_reject(r.symbol, r.id, RejectReason::BadLotSize);
    if (!is_market) { // 市价单的 理论上没有 price，跳过检查
        if (r.price <= 0) return emit_reject(r.symbol, r.id, RejectReason::InvalidPrice);
        if (r.price % cfg.tick_size != 0) return emit_reject(r.symbol, r.id, RejectReason::BadTickSize);
    }
    if (s->book.contains(r.id)) return emit_reject(r.symbol, r.id, RejectReason::DuplicateId);

    // Accpet
    emit_exec(r.symbol, r.id, ExecType::Accepted, 0, 0, r.qty, 0, r.qty);
    
    const Price limit = !is_market ? r.price : r.side == Side::Buy ? kMarketBuyLimit : kMarketSellLimit;
    Order taker{r.id, r.side, limit, r.qty, 0, 0};
    execute(*s, taker, r.tif);
}

// 调用 OrderBook 提供的 match 原语， 并对 返回值 Fills 列表 进行 事件处理
void MatchingEngine::execute(SymbolState& s, Order& taker, TimeInForce tif) {
    const SymbolId sym = s.cfg.id;
    // 订单总量
    const Qty order_qty = taker.qty + taker.cum_qty;

    // FOK 预判段
    if (tif == TimeInForce::FOK && s.book.fillable_qty(taker.side, taker.price, taker.qty) < taker.qty) {
        // 库存不足，FOK 失败
        return emit_exec(sym, taker.id, ExecType::Canceled, 0, 0, 0, taker.cum_qty, order_qty);
    }

    Qty taker_cum = taker.cum_qty;
    fills_.clear(); // 清空元素
    // 开始 撮合， fills_ 有 每一笔成交 过程； 因为 一个 taker 可以连续吃 很多 maker 的 单
    s.book.match(taker, fills_); // 完成 本轮 所有 撮合

    // 把 Fill 翻译成 Events
    for (const Fill& f : fills_) {
        // taker.cum_qty 是最终的；一个 taker 订单 有可能 产生多笔 成交
        // 在此，我们想要 还原每一笔成交，所以 重新累加
        taker_cum += f.qty; // 还原这一笔 Fill 的 累计成交量
        const Qty taker_leaves = order_qty - taker_cum;
        
        const ExecType maker_exec_ty = f.maker_leaves == 0 ? ExecType::Filled : ExecType::PartialFill;
        const ExecType taker_exec_ty = taker_leaves == 0 ? ExecType::Filled : ExecType::PartialFill;
        // maker 的 id 是从 订单簿里弄出来的
        emit_trade(sym, f.maker_id, taker.id, taker.side, f.price, f.qty);
        // maker events
        emit_exec(sym, f.maker_id, maker_exec_ty, f.price, f.qty, f.maker_leaves, f.maker_cum, f.maker_leaves + f.maker_cum);
        // taker events
        emit_exec(sym, taker.id, taker_exec_ty, f.price, f.qty, taker_leaves, taker_cum, order_qty);
        // 盘口信息变化
        emit_book(sym, opposite(taker.side), f.price, f.level_total);
    }

    // 全部成交，不需要挂单
    if (taker.qty == 0) return; 

    // 没吃完，如果不是 GTC 就撤单
    if (tif != TimeInForce::GTC) {
        // IOC, FOK, 市价 剩余部分撤销
        assert(tif != TimeInForce::FOK && "FOK passed pre-check but did not fully fill");
        return emit_exec(sym, taker.id, ExecType::Canceled, 0, 0, 0, taker.cum_qty, order_qty);
    }

    // GTC，继续挂单
    assert(taker.price != kMarketBuyLimit && taker.price != kMarketSellLimit); // 市价单不可能走到这里
    // 挂到队尾
    const Qty total = s.book.rest(taker); 
    // 更新盘口信息
    emit_book(sym, taker.side, taker.price, total);
}

// ================================
// 撤单
// ================================
void MatchingEngine::on_request(const CancelOrder& r) {
    SymbolState* s = find_symbol(r.symbol);
    if(!s) return emit_reject(r.symbol, r.id, RejectReason::UnknowSymbol);
    
    const auto removed = s->book.remove(r.id);
    if (!removed) return emit_reject(r.symbol, r.id, RejectReason::UnknownOrder);

    const Order& o = removed->order;
    emit_exec(r.symbol, r.id, ExecType::Canceled, 0, 0, 0, o.cum_qty, o.cum_qty + o.qty);
    emit_book(r.symbol, o.side, o.price, removed->level_total);
}

// ================================
// 改单
// ================================
void MatchingEngine::on_request(const ModifyOrder& r) {
    SymbolState* s = find_symbol(r.symbol);
    if(!s) return emit_reject(r.symbol, r.id, RejectReason::UnknowSymbol);
    const SymbolConfig& cfg = s->cfg;

    // 静态错误在前
    if (r.new_qty <= 0) return emit_reject(r.symbol, r.id, RejectReason::InvalidQty);
    if (r.new_qty % cfg.lot_size != 0) return emit_reject(r.symbol, r.id, RejectReason::BadLotSize);
    if (r.new_price <= 0) return emit_reject(r.symbol, r.id, RejectReason::InvalidPrice);
    if (r.new_price % cfg.tick_size != 0) return emit_reject(r.symbol, r.id, RejectReason::BadTickSize);

    const Order* o = s->book.find(r.id);
    if(!o) return emit_reject(r.symbol, r.id, RejectReason::UnknownOrder);

    // 啥也没变就提交了
    if (r.new_price == o->price && r.new_qty == o->qty) return emit_reject(r.symbol, r.id, RejectReason::NoChange);

    // 减量
    if (r.new_price == o->price && r.new_qty < o->qty) {
        const Qty cum = o->cum_qty;
        const Side side = o->side;
        const bool changed = r.new_qty != o->qty;
        const Qty total = *s->book.reduce(r.id, r.new_qty);

        emit_exec(r.symbol, r.id, ExecType::Replaced, 0, 0, r.new_qty, cum, cum + r.new_qty);
        if (changed) emit_book(r.symbol, side, r.new_price, total);
        return;
    }

    // 撤单重新下
    const OrderBook::Removed removed = *s->book.remove(r.id);
    const Order& old = removed.order;

    emit_exec(r.symbol, r.id, ExecType::Replaced, 0, 0, r.new_qty, old.cum_qty, old.cum_qty + r.new_qty);
    emit_book(r.symbol, old.side, old.price, removed.level_total);

    // 重新撮合挂单
    Order taker{r.id, old.side, r.new_price, r.new_qty, old.cum_qty, 0};
    execute(*s, taker, TimeInForce::GTC); // 撮合 + 挂单 走在 execute 里面 TODO 要不要拆开啊

}

void MatchingEngine::emit_trade(SymbolId sym, OrderId maker, OrderId taker, Side aggressor, Price px, Qty q) {
    // 借助 listener 去完成 通知，
    listener_.on_trade(Trade{next_event_seq_++, now_ns_, sym, maker, taker, aggressor, px, q});
}

void MatchingEngine::emit_exec(SymbolId sym, OrderId id, ExecType type, Price last_px, Qty last_qty, Qty leaves, Qty cum, Qty order_qty) {
    listener_.on_exec(ExecReport{next_event_seq_++, now_ns_, id, sym, type, RejectReason::None, last_px, last_qty, leaves, cum, order_qty});
}

void MatchingEngine::emit_reject(SymbolId sym, OrderId id, RejectReason reason) {
    listener_.on_exec(ExecReport{next_event_seq_++, now_ns_, id, sym, ExecType::Rejected, reason, 0, 0, 0, 0, 0});
}

void MatchingEngine::emit_book(SymbolId sym, Side side, Price px, Qty new_total) {
    listener_.on_book(BookUpdate{next_event_seq_++, now_ns_, sym, side, px, new_total});
}

bool MatchingEngine::check_invariants(std::string* why) const {
    for (const auto& [id, s] : symbols_) {
        if (!s.book.check_invariants(why)) {
            if (why) *why = "symbol " + std::to_string(id) + ": " + *why;
            return false;
        }
    }
    return true;
}

}