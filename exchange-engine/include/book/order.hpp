// ============================================
// v1 -> v2
// [修改] Order 新增 累计成交量
// [移动] Trade 移动到 engien/events.hpp, 对外事件
// [新增] Fill 订单簿每撮合一笔，就产出一个 Fill 交给 Engine
// ============================================
#pragma once

#include <cstdint>
#include "common/types.hpp"

// class 和 struct 其实没啥区别，就是 访问权限 和 是否暴露接口 / 成员的问题

namespace exchange {

// why 不需要 type / tif / symbol ?
// 能挂在 订单簿上的肯定是 limit + GTC
// 订单簿是按照品种分开的，一个簿里的 Order 都是同一个 Symbol的
struct Order {
    OrderId id;
    Side side;
    Price price; // 限价
    Qty qty;    // remaining
    Qty cum_qty; // 累计成交量，for modify 
    std::uint64_t seq; // 订单顺序号，单调递增
};

struct PriceLevel {
    std::list<Order> orders;
    Qty total_qty{0}; // for depth of orderbook, cache 值

};

// Fill 订单簿撮合时，产出的
// 订单簿 不再负责 产出 Trade，而是 gen Fill 然后交给 Engine 去处理
// 订单簿 应该 只 focus on 价格 数量 队列， 其他的 都不用管
struct Fill {
    // fill 是一次 match(taker, fills) 的内部结果，Engine 知道taker 是谁
    OrderId maker_id;
    Price price; // maker 价格
    Qty qty; // 本笔成交量
    Qty maker_leaves; // 成交后，maker 的剩余量
    Qty maker_cum;
    Qty level_total; // 成交后 该价位的总量
};

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
