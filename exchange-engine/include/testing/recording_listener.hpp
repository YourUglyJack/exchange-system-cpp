#pragma once

#include <string>
#include <variant>
#include <vector>

#include "engine/listener.hpp"

namespace exchange::testing {

using Event = std::variant<Trade, ExecReport, BookUpdate>;

inline std::string check_exec_report(const ExecReport& e) {
    // [&] 可以引用 外层变量，而不需要赋值
    const auto bad = [&](const char* what) {
        return std::string(what) + " (id=" + std::to_string(e.id) + " " + to_string(e.type) + ")";
    };

    if (e.leaves_qty < 0 || e.cum_qty < 0) return bad("negative qty");

    switch (e.type) {
        case ExecType::Rejected:
            if (e.reason == RejectReason::None) return bad("reject without reason");
            if (e.leaves_qty || e.cum_qty || e.order_qty) return bad("reject with quantities");
            return {};
        case ExecType::Canceled:
            if (e.leaves_qty != 0) return bad("canceled with leaves");
            if (e.cum_qty >= e.order_qty) return bad("canceled but nothing left to cancel");
            break;
        case ExecType::Filled:
            if (e.leaves_qty != 0) return bad("filled with leaves");
            [[fallthrough]];
        case ExecType::PartialFill:
            if (e.last_qty <= 0) return bad("fill without last_qty");
            if (e.type == ExecType::PartialFill && e.leaves_qty == 0) return bad("partial w/o leaves");
            [[fallthrough]];
        case ExecType::Accepted:
        case ExecType::Replaced:
            if (e.cum_qty + e.leaves_qty != e.order_qty) return bad("cum + leaves != order_qty");
            break;
    }

    if (e.reason != RejectReason::None) return bad("non-reject with reason");
    return {};
}

class RecordingListener final : public EngineListener {
public:
    void on_trade(const Trade& t) override { 
        record(t, t.seq); 
    }

    void on_exec(const ExecReport& e) override {
        const std::string why = check_exec_report(e);
        if (!why.empty() && error_.empty()) error_ = why;
        record(e, e.seq);
    }

    void on_book(const BookUpdate& b) override { 
        record(b, b.seq); 
    }

    const std::vector<Event>& events() const { 
        return events_; 
    }

    void clear() { 
        events_.clear(); 
    }

    // 第一个流级别错误（空串表示没有）。
    const std::string& error() const { 
        return error_; 
    }

    // 便捷筛选：只要某一类事件。
    template <class T>
    std::vector<T> only() const {
        std::vector<T> out;
        for (const Event& ev : events_) {
            if (const T* p = std::get_if<T>(&ev)) out.push_back(*p);
        }
        return out;
    }

private:
    std::vector<Event> events_;
    std::uint64_t last_seq_ = 0;
    std::string error_;

    void record(const Event& ev, std::uint64_t seq) {
        if (last_seq_ != 0 && seq != last_seq_ + 1 && error_.empty()) {
            error_ = "seq gap: " + std::to_string(last_seq_) + " -> " + std::to_string(seq);
        }
        last_seq_ = seq;
        events_.push_back(ev);
    }
};

}