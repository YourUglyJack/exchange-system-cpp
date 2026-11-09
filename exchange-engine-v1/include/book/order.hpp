#pragma once

#include <cstdint>
#include <ostream>
#include <list>

#include "common/types.hpp"

// Order 和 Trader 是 纯数据聚合，装数据的，所以 可以直接上 struct
// class 和 struct 其实没啥区别，就是 访问权限 和 是否暴露接口 / 成员的问题

namespace lob {

struct Order {
    OrderId id;
    Side side;
    Price price;
    Qty qty; // remaining

    std::uint64_t seq; // 订单顺序号，单调递增
};

// 一笔成交， 一张 taker 订单 可能有很多 trade， 吃掉好几个 maker
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    Price price;
    Qty qty;

    Side aggressor; // 这笔交易中 主动发起 交易的一方，谁主动吃掉了已经有的挂单
};

struct PriceLevel {
    std::list<Order> orders;
    Qty total_qty{0}; // for depth of orderbook, cache 值

};

// 比较两笔成交是否完全相同。测试和对拍程序需要逐笔比较输出。
// 写成非成员函数（而不是成员 operator==）是 C++17 的惯用写法：
// 两边参数地位对称，且保持 Trade 是一个纯聚合体。
inline bool operator ==(const Trade& a, const Trade& b) noexcept {
    return a.maker_id == b.maker_id 
    && a.taker_id == b.taker_id
    && a.aggressor == b.aggressor 
    && a.price == b.price;
}

// TODO: 搞懂 inline
inline bool operator!=(const Trade& a, const Trade& b) noexcept {
    return !(a == b);
}

// 为了让 << 可以识别 Trade 类型
inline std::ostream& operator<<(std::ostream& os, const Trade* a) {
    return os << "[TRADE] maker=" << a->maker_id << " taker=" << a->taker_id << " price=" << a->price << " qty=" << a->qty << "\n";
}

}

// ===============================
// 同一个价位上的订单如何管理？
// list 双端链表， time priority
// ================================
// 同一个档位的订单，需要有以下操作
// a. 队尾追加新订单
// b. 队首成交后弹出
// c. 撤单，从队列 中间 del
// BTW, 为了 撤单 能够快速 del，引入 index_, index_ 存迭代器，所以 迭代器需要稳定，指针悬空就寄了

// 候选数据结构: vector, deque, list; we choose std::list

// deque, vector 对于 撤单删除不是很友好
// std::list 迭代器稳定，增删其他元素的时候，未被删除的元素的迭代器不会失效
// std::vector / std::deque 插入可能会引起扩容导致 迭代器失效；vector 删除 移动元素，迭代器也有失效风险