// 在编译一个.cpp 的时候，该头文件只会被展开一次；因为头文件有可能会被间接或直接引用多次而导致多次展开
// A xxx.hpp : include types.hpp; B .cpp: inlcude types, include A; 那 types 就有可能被展开两次
#pragma once 

#include <cstdint> // 提供固定宽度整数类型的头文件，int 在不同平台 bytes不一样，但是 int64_t 保证了一样
#include <limits>

// ==================================
// v1 -> v2
// 新增 symbolId, OrderType, TimeInForce, 两个 无穷价格
// ==================================


// 价格用 tick 表示，拒绝 double 避免 浮点数误差
namespace exchange {
    // 提高可读性， 也方便后续优化时修改类型
    // 不过 using 只是 alias，strong type 还是 struct 比较好
    using Price = std::int64_t;
    using Qty = std::int64_t;
    using OrderId = std::uint64_t; // 只有 cmp & hash，不做减法，无符号
    // v2
    using SymbolId = std::uint32_t; // uint32_t 无符号整数 非负fixed 4 bytes, 热路径，用 整数 省了很多处理 和 存储开销

    enum class Side : std::uint8_t { Buy, Sell };  // 可读性，类型安全
    
    // 对手盘，买单的对手盘是卖单
    // constexpr 的 func，可以在编译期计算，也可以在 runtime 时计算；constexpr 的变量 必须在编译期间知道结果 否则 编译出错
    // 让 该函数具备 编译期 就能确定结果的能力
    constexpr Side opposite(Side s) noexcept {
        return s == Side::Buy ? Side::Sell : Side::Buy;
    }

    // 返回字符串字面量，直接用 "", include的部分，可以理解为直接写出来的 eg: "123", 123, 3.14
    // 字符串字面量，整个程序运行期间都存在
    constexpr const char* to_string(Side s) noexcept {
        // 安全，函数结束后这段字符串 still 存在，字符串字面量的性质
        return s == Side::Buy ? "BUY" : "SELL";
    }

    // v2
    // 订单类型: 限价 / 市价; 回答价格怎么定义
    enum class OrderType : std::uint8_t { Limit, Market };
    // 有效期 TIF; 回答 订单的有效期 即 没成交完的订单 怎么处理？
    // Limit: GTC, IOC, FOK
    // Market: IOC, FOK
    enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

    constexpr const char* to_string(OrderType t) noexcept {
        return t == OrderType::Limit ? "LIMIT" : "MARKET";
    }

    constexpr const char* to_string(TimeInForce tif) noexcept {
        switch (tif) {
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
        }
        return "?"; // 防止有 神人 写 static_cast<TimeInForce>(7)
    }

// v2
// 无穷价格，为了 Market 单 准备， 
// 市价单 相当于 限价无穷的 IOC 单, 但是 市价单 不会挂进 Orderbook 里面， 否则就会出现 INFINIT 价格在 订单上
// 买单愿意出任意高价， 卖单愿意出任意低价
inline constexpr Price kMarketBuyLimit = std::numeric_limits<Price>::max();
inline constexpr Price KMarketSellLimit = std::numeric_limits<Price>::min();

} // namespace
