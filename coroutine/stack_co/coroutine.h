#ifndef COROUTINE_COROUTINE_H
#define COROUTINE_COROUTINE_H
//协程类

#include "status.h"
#include "context.h"

#include <functional>
#include <memory>

namespace stack_co {

    class Environment;          

    class Coroutine : public std::enable_shared_from_this<Coroutine> {
        friend class Environment;

        friend class Context;

    public:
        //当前线程的协程栈中栈顶的协程 但不一定是本协程
        static Coroutine &get_thread_top_coroutine();

        // 测试当前控制流是否位于协程上下文?
        bool test();

        // 获取当前协程运行时信息
        Status runtime() const;

        bool exit() const;

        bool running() const;

        // 核心操作：resume和yield

        // usage: Coroutine::get_thread_top_coroutine().yield()
        static void yield();

        // Note1: 允许处于EXIT状态的协程重入，从而再次resume
        //        如果不使用这种特性，则用exit() / running()判断
        //
        // Note2: 返回值可以得知resume并执行后的运行时状态
        //        但是这个值只适用于简单的场合
        //        如果接下来其它协程的运行也影响了该协程的状态
        //        那建议用runtime()获取
        Status resume();

        Coroutine(const Coroutine &) = delete;

        Coroutine(Coroutine &&) = delete;

        Coroutine &operator=(const Coroutine &) = delete;

        Coroutine &operator=(Coroutine &&) = delete;

    public:
        // 构造Coroutine执行函数，entry为函数入口，对应传参为arguments...
        // Note: 出于可重入的考虑，entry强制为值传递
        template<typename Entry, typename ...Args>
        Coroutine(Environment *master, Entry entry, Args ...arguments)
                : _entry([=] { entry(/*std::move*/(arguments)...); }),//这里使用值捕获
                    //这里对环境指针做浅拷贝
                  _master_env(master) {}

    private:
        //对用户提供的client函数的一层封装
        static void call_when_finish(Coroutine *coroutine);

    private:
        Status _runtime{};

        //每个协程对应一个上下文环境
        Context _context{};

        //这个函数对象是该协程的执行主体
        std::function<void()> _entry;

        //该协程对应的线程环境
        Environment *_master_env;
    };

} // namespace stack_co

#endif //COROUTINE_COROUTINE_H
