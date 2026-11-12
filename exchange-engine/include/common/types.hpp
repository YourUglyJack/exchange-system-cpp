// 在编译一个.cpp 的时候，该头文件只会被展开一次；因为头文件有可能会被间接或直接引用多次而导致多次展开
// A xxx.hpp : include types.hpp; B .cpp: inlcude types, include A; 那 types 就有可能被展开两次
#pragma once 

#include <cstdint> // 提供固定宽度整数类型的头文件，int 在不同平台 bytes不一样，但是 int64_t 保证了一样

// 价格用 tick 表示，拒绝 double 避免 浮点数误差
namespace lob {
    // 提高可读性， 也方便后续优化时修改类型
    // 不过 using 只是 alias，strong type 还是 struct 比较好
    using Price = std::int64_t;
    using Qty = std::int64_t;
    using OrderId = std::uint64_t; // 只有 cmp & hash，不做减法，无符号

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
}
