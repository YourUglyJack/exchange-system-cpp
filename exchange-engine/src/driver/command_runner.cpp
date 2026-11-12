// 解析 文本指令
#include <charconv> // std::from_chars
#include <optional>
#include <sstream> // string stream
#include <string>
#include <system_error>
#include <vector>

#include "driver/command_runner.hpp"

namespace exchange {
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

void report_err(std::ostream& out, RunStats& stats, const std::string& msg) {
    out << "ERROR line " << stats.lines << ": " << msg << "\n";
    ++stats.parse_errors;
}
} // namespace

RunStats run_commands(std::istream& in, std::ostream& out, OrderBook& book) {
    RunStats stats;
    std::string line;

    while (std::getline(in, line)) {
        ++stats.lines;
        
        // getline 只会 pop 掉 \n
        // 但在 windows 系统，换行是 \r\n, 会保留 \r
        // if (!line.empty() && line.back() == '\r') {
        //     line.pop_back();
        // }

        const auto tokens = split(line);
        
        // 空行 或者 注释
        if (tokens.empty() || tokens[0][0] == '#') { 
            // "" 是 string, '' 是char
            continue;
        }

        const std::string& cmd = tokens[0];

        // string 选择 不能用 switch，swich 只 接受 数字 / enum
        if (cmd == "ADD") {
            // OrderId, Side, Price, Qty
            if (tokens.size() != 5) {
                report_err(out, stats, "usage: ADD <id> <BUY|SELL> <price> <qty>");
                continue;
            }
            const auto id = parse_int<OrderId>(tokens[1]);
            const auto side = parse_side(tokens[2]);
            const auto price = parse_int<Price>(tokens[3]);
            const auto qty = parse_int<Qty>(tokens[4]);
            if (!id || !side || !price || !qty) {
                // 任何一个 解析 错误 就 err, 因为有些有 optional
                report_err(out, stats, "bad ADD args");
                continue;
            }
            
            // 因为 parse 的时候 是 optional，可能会 nullopt，所以这里 要 *， 取出来
            const AddResult res = book.add(*id, *side, *price, *qty);
            ++stats.commands;

            if (!res.accepted()) {
                out << "REJECT id=" << *id << " reason=" << to_string(res.status) << "\n";
            }
            for (const Trade& t: res.trades) {
                out << t;
            }

        } else if (cmd == "CANCEL") {
            if (tokens.size() != 2) {
                report_err(out, stats, "usage: CANCEL <id>");
                continue;
            }
            const auto id = parse_int<OrderId>(tokens[1]);
            if (!id) {
                report_err(out, stats, "bad CANCEL id");
                continue;
            }
            if (book.cancel(*id)) {
                out << "CANCEL id=" << *id << "\n";
            } else {
                out << "CANCEL_REJECT id=" << *id << " reason=UnknownOrder\n";;
            }
            ++stats.commands;

        } else if (cmd == "PRINT") {
            if (tokens.size() != 1) {
                report_err(out, stats, "usage: PRINT");
                continue;
            }
            book.print(out);
            ++stats.commands;
        } else {
            report_err(out, stats, "unknown command '" + cmd + "'");
        }
    }
    return stats;
}

} // namespace
