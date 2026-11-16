#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "engine/matching_engine.hpp"
#include "testing/recording_listener.hpp"
#include "testing/reference_engine.hpp"

namespace {

using namespace exchange;
using namespace exchange::testing;

const SymbolConfig kSymA{1, "A", 1, 1};
const SymbolConfig kSymB{2, "B", 5, 10};
constexpr SymbolId kUnknownSym = 3;

std::uint64_t zero_clock() { return 0; }

// 把请求写成 file_driver 的指令格式，便于重放。
std::string to_command(const Request& req) {
    auto name = [](SymbolId s) { return s == 1 ? std::string("A") : s == 2 ? "B" : "UNKNOWN"; };
    std::ostringstream os;
    if (const auto* n = std::get_if<NewOrder>(&req)) {
        os << "NEW " << name(n->symbol) << ' ' << n->id << ' ' << to_string(n->side) << ' ';
        if (n->type == OrderType::Limit) {
            os << "LIMIT " << n->price << ' ';
        } else {
            os << "MARKET ";
        }
        os << n->qty << ' ' << to_string(n->tif);
    } else if (const auto* c = std::get_if<CancelOrder>(&req)) {
        os << "CANCEL " << name(c->symbol) << ' ' << c->id;
    } else {
        const auto& m = std::get<ModifyOrder>(req);
        os << "MODIFY " << name(m.symbol) << ' ' << m.id << ' ' << m.new_price << ' ' << m.new_qty;
    }
    return os.str();
}

std::vector<ReferenceEngine::Out> strip(const std::vector<Event>& evs) {
    std::vector<ReferenceEngine::Out> out;
    for (const Event& ev : evs) {
        if (const auto* t = std::get_if<Trade>(&ev)) {
            Trade copy = *t;
            copy.seq = 0;
            copy.ts_ns = 0;
            out.emplace_back(copy);
        } else if (const auto* e = std::get_if<ExecReport>(&ev)) {
            ExecReport copy = *e;
            copy.seq = 0;
            copy.ts_ns = 0;
            out.emplace_back(copy);
        }
    }
    return out;
}

// who: name of engine
void print_out(const char* who, const std::vector<ReferenceEngine::Out>& v) {
    std::cerr << "  " << who << ":\n";
    // 只 print Trade 和 ExecReport
    for (const auto& o : v) {
        if (const auto* trade = std::get_if<Trade>(&o)) {
            std::cerr << "    " << *trade << '\n';
        } else if (const auto* exec = std::get_if<ExecReport>(&o)) {
            std::cerr << "    " << *exec << '\n';
        }
    }
}

class Fuzzer {
public:
    explicit Fuzzer(std::uint64_t seed) 
        : rng_(seed), engine_(rec_, &zero_clock) 
        {
            engine_.add_symbol(kSymA);
            engine_.add_symbol(kSymB);
            ref_.add_symbol(kSymA);
            ref_.add_symbol(kSymB);
        }
    
    std::string step(const Request& req) {
        rec_.clear(); // 清空上一个 req 触发的 events
        // 两个 engine 处理 同一个 req
        engine_.handle(req);
        const auto want = ref_.handle(req);

        if (!rec_.error().empty()) {
            return "event check: " + rec_.error();
        }

        std::string why;
        if (!engine_.check_invariants(&why)) {
            return "invariant: " + why;
        }

        // event 
        const auto got = strip(rec_.events());
        if (got != want) {
            print_out("MatchingEngine", got);
            print_out("ReferenceEngine", got);
            return "event stream differs";
        }

        // TODO BookUpdate

        // snapshot
        for (SymbolId s : {kSymA.id, kSymB.id}) {
            const BookSnapshot snap = engine_.book(s)->snapshot();
            if (snap != ref_.snapshot(s)) {
                return "snapshot differs (symbol " + std::to_string(s) + ")";
            }
        }


        return {};
    }
private:
    std::mt19937_64 rng_;
    RecordingListener rec_;
    MatchingEngine engine_;
    ReferenceEngine ref_;
    OrderId next_id_ = 1;


};
}