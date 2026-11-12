// [V2] 事件接收者
// v1：你调用订单簿 → 拿到返回值 → 自己处理。
// v2：你调用引擎 → 引擎处理请求 → 引擎调用你提供的 Listener
#pragma once

#include "engine/events.hpp"

namespace exchange {
// =0 纯虚函数，具体接受者需要提供实现
struct EngineListener {
    // 有 虚函数的类，都必须有 虚折构
    virtual ~EngineListener() = default;

    virtual void on_trade(const Trade&) = 0;
    virtual void on_exec(const ExecReport&) = 0;
    virtual void on_book(const BookUpdate&) = 0;
};

// 啥也不干
struct NullListener final : EngineListener {
    void on_trade(const Trade&) override {}
    void on_exec(const ExecReport&) override {}
    void on_book(const BookUpdate&) override{}
};

}  // namespace





// Virtual Function（虚函数）
// 允许派生类 override。
// 通过基类指针或引用调用虚函数时，
// 会根据 runtime 时 的 类型，调用 具体的函数

// Virtual Destructor（虚析构函数）
// 当通过基类指针 delete 派生类对象时，
// 通常需要基类析构函数为 virtual。
// 否则属于 Undefined Behavior（未定义行为），
// 可能导致派生类资源未正确释放。

// 注意：虚析构与普通虚函数解决的是不同问题。
// 虚函数：正确调用哪个实现。
// 虚析构：正确执行完整的对象析构过程。