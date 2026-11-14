// 指令解析 + listenner
#include <charconv> // std::from_chars
#include <optional>
#include <sstream> // string stream
#include <string>
#include <system_error>
#include <vector>
#include <unordered_map>

#include "driver/command_runner.hpp"
#include "engine/matching_engine.hpp"

namespace exchange {

class TextListener final : public EngineListener {
public:
    TextListener(std::ostream& out, const std::unordered_map<SymbolId, std::string>& names)
        : out_(out), names_(names) 
        {}
    
    void on_trade(const Trade& t) override {
        out_ << '[' << t.seq << "] TRADE " << name(t.symbol) << " maker=" << t.maker_id 
        << " taker=" << t.taker_id << ' ' << to_string(t.aggressor) << ' ' << t.price << 'x' << t.qty << '\n';
    }

    void on_exec(const ExecReport& e) override {
        out_ << '[' << e.seq << "] EXEC  " << name(e.symbol) << " id=" << e.id << ' '
             << to_string(e.type);
        if (e.type == ExecType::Rejected) {
            out_ << " reason=" << to_string(e.reason) << '\n';
            return;
        }
        if (e.type == ExecType::PartialFill || e.type == ExecType::Filled) {
            out_ << " last=" << e.last_price << 'x' << e.last_qty;
        }
        out_ << " cum=" << e.cum_qty << " leaves=" << e.leaves_qty << '\n';
    }

    void on_book(const BookUpdate& b) override {
        out_ << '[' << b.seq << "] BOOK  " << name(b.symbol) << ' '
             << (b.side == Side::Buy ? "BID " : "ASK ") << b.price << " -> " << b.new_total << '\n';
    }

private:
    std::ostream& out_;
    const std::unordered_map<SymbolId, std::string>& names_;

    std::string name(SymbolId id) const {
        const auto it = names_.find(id);
        return it == names_.end() ? "#" + std::to_string(id) : it->second;
    }

};


namespace {
    // 把一行切成 以 空白 为 分割 的 token
std::vector<std::string> split(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) { // istringstream  每次 << 会 跳过空白，读取连续非空白字符，直到遇到 空白字符又停下
        tokens.push_back(token);
    }
    return tokens;
}
// 解析整数
// atoi: asc -> int; "123"->123, "12abc"->12, "abc"->0; 但其实 12abc 是错误输入, 而且无法分清楚 0 是 合法的还是不合法的
// stoll: string -> long long; "123" -> 123, "12abc" -> 12, "abc" 抛异常
template <class Int>
std::optional<Int> parse_int(const std::string& s) {
    Int value{};
    const char* first = s.data();
    const char* last = s.data() + s.size();
    // c++ 17 提供的 chars -> int / uint 的 api, [first, last)
    const auto [ptr, ec] = std::from_chars(first, last, value);
    // ec: error code, 如果 ptr 不等于 last，说明 有未解析完成的字符，有可能是字母有可能是标点符号
    if (ec != std::errc{} || ptr != last) return std::nullopt; // 解析出错
    return value;
}

std::optional<Side> parse_side(const std::string& s) {
    if (s == "BUY") return Side::Buy;
    if (s == "SELL") return Side::Sell;
    return std::nullopt;
}

std::optional<TimeInForce> parse_tif(const std::string& s) {
    if (s == "GTC") return TimeInForce::GTC;
    if (s == "IOC") return TimeInForce::IOC;
    if (s == "FOK") return TimeInForce::FOK;
    return std::nullopt;
}


void report_err(std::ostream& out, RunStats& stats, const std::string& msg) {
    out << "ERROR line " << stats.lines << ": " << msg << "\n";
    ++stats.parse_errors;
}


} // namespace

class Runner {
public:
    explicit Runner(std::ostream& out) 
    : out_(out), listener_(out, names_), engine_(listener_, &fixed_clock) 
    {}

    std::string run(const std::vector<std::string>& tokens) {
        const std::string& cmd = tokens[0];
        if (cmd == "SYMBOL") return cmd_symbol(tokens);
        if (cmd == "NEW") return cmd_new(tokens);
        if (cmd == "CANCEL") return cmd_cancel(tokens);
        if (cmd == "MODIFY") return cmd_modify(tokens);
        if (cmd == "PRINT") return cmd_print(tokens);
        return "unknown command '" + cmd + "'";
    }

private:
    std::ostream& out_;
    std::unordered_map<std::string, SymbolId> ids_;    // 名字 → id（解析时用）
    std::unordered_map<SymbolId, std::string> names_;  // id → 名字（打印时用）
    TextListener listener_;  // 声明顺序很重要：listener_ 引用 names_，必须在其后构造
    MatchingEngine engine_;  // engine_ 引用 listener_，必须在其后构造

    static std::uint64_t fixed_clock() { return 0; }

    // sym -> Id
    std::optional<SymbolId> sym(const std::string& name) const {
        const auto it = ids_.find(name);
        if (it == ids_.end()) return std::nullopt;
        return it->second;
    }

    std::string cmd_symbol(const std::vector<std::string>& tokens) {
        if (tokens.size() != 4) return "usage: SYMBOL <name> <tick_size> <lot_size>";
        const auto tick = parse_int<Price>(tokens[2]);
        const auto lot = parse_int<Qty>(tokens[3]);
        if (!tick || !lot) return "bad SYMBOL arguments";
        if (ids_.count(tokens[1])) return "symbol " + tokens[1] + " already defined";
        const SymbolId id = static_cast<SymbolId>(ids_.size() + 1);
        if(!engine_.add_symbol(SymbolConfig{id, tokens[1], *tick, *lot})) {
            return "tick_size and lot_size must be positive";
        }
        ids_[tokens[1]] = id;
        names_[id] = tokens[1];
        return {};
    }

    std::string cmd_new(const std::vector<std::string>& tokens) {
        // NEW sym id side LIMIT price qty [tif]   (7–8 个 token)
        // NEW sym id side MARKET qty [tif]        (6–7 个 token)
       int n = static_cast<int>(tokens.size());
        if (n < 6) return "usage: NEW <sym> <id> <BUY|SELL> LIMIT <price> <qty> [tif]";
        const auto sym_id = sym(tokens[1]);
        if (!sym_id) return "unknown symbol '" + tokens[1] + "'";
        const auto id = parse_int<OrderId>(tokens[2]);
        const auto side = parse_side(tokens[3]);
        if(!id || !side) return "bad New arguments";

        // 封装请求
        NewOrder o{*id, *sym_id, *side, OrderType::Limit, TimeInForce::GTC, 0, 0};
        std::size_t tif_idx = 0;
        const std::string& order_type_s = tokens[4];
        // 更改参数
        if (order_type_s == "LIMIT" && (n == 7 || n == 8)) {
            const auto px = parse_int<Price>(tokens[5]);
            const auto qty = parse_int<Qty>(tokens[6]);
            if (!px || !qty) return "bad LIMIT price/qty";
            o.price = *px;
            o.qty = *qty;
            tif_idx = 7;
        } else if (order_type_s == "MARKET" && (n == 6 || n == 7)) {
            const auto q = parse_int<Qty>(tokens[5]);
            if (!q) return "bad MARKET qty";
            o.type = OrderType::Market;
            o.tif = TimeInForce::IOC; 
            o.qty = *q;
            tif_idx = 6;
        } else {
            return "usage: NEW <sym> <id> <side> LIMIT <price> <qty> [tif] | MARKET <qty> [tif]";
        }

        // 解析 TIF
        if (n > tif_idx) {
            const auto tif = parse_tif(tokens[tif_idx]);
            if (!tif) return "bad time-in-force '" + tokens[tif_idx] + "'";
            o.tif = *tif;
        }

        engine_.handle(o); // 处理请求
        return {};
    }

    std::string cmd_cancel(const std::vector<std::string>& t) {
        if (t.size() != 3) return "usage: CANCEL <sym> <id>";
        const auto s = sym(t[1]);
        if (!s) return "unknown symbol '" + t[1] + "'";
        const auto id = parse_int<OrderId>(t[2]);
        if (!id) return "bad CANCEL id";
        engine_.handle(CancelOrder{*id, *s});
        return {};
    }

    std::string cmd_modify(const std::vector<std::string>& t) {
        if (t.size() != 5) return "usage: MODIFY <sym> <id> <new_price> <new_qty>";
        const auto s = sym(t[1]);
        if (!s) return "unknown symbol '" + t[1] + "'";
        const auto id = parse_int<OrderId>(t[2]);
        const auto px = parse_int<Price>(t[3]);
        const auto q = parse_int<Qty>(t[4]);
        if (!id || !px || !q) return "bad MODIFY arguments";
        engine_.handle(ModifyOrder{*id, *s, *px, *q});
        return {};
    }

    std::string cmd_print(const std::vector<std::string>& t) {
        if (t.size() != 2 && t.size() != 3) return "usage: PRINT <sym> [depth]";
        const auto s = sym(t[1]);
        if (!s) return "unknown symbol '" + t[1] + "'";
        std::size_t depth = OrderBook::kAllLevels;
        if (t.size() == 3) {
            const auto d = parse_int<std::size_t>(t[2]);
            if (!d || *d == 0) return "bad depth";
            depth = *d;
        }
        out_ << "=== " << t[1] << " ===\n";
        engine_.book(*s)->print(out_, depth);
        out_ << "==================\n";
        return {};
    }

};

RunStats run_commands(std::istream& in, std::ostream& out) {
    RunStats stats;
    Runner runner(out);
    std::string line;
    while (std::getline(in, line)) {
        ++stats.lines;
        const auto tokens = split(line);
        if (tokens.empty() || tokens[0][0] == '#') continue;

        const std::string err = runner.run(tokens);
        if (err.empty()) {
            ++stats.commands;
        } else {
            out << "ERROR line " << stats.lines << ": " << err << '\n';
            ++stats.parse_errors;
        }
    }
    return stats;
}

} // namespace
