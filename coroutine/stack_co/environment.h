#ifndef COROUTINE_ENVIRONMENT_H
#define COROUTINE_ENVIRONMENT_H

//本例中实现的协程不支持跨线程，而是每个线程分配一个环境，来维护该线程下运行中的协程之间的层次关系；
#include "coroutine.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>

namespace stack_co {

    class Coroutine;

    class Environment {
        friend class Coroutine;

    public:
        // Thread-local get_instance 单例 懒汉
        static Environment &get_instance();

        // Factory method 为当前线程造一个协程出来
        template<typename Entry, typename ...Args>
        std::shared_ptr<Coroutine> create_coroutine(Entry &&entry, Args &&...arguments);

        // No copy constructor
        Environment(const Environment &) = delete;

        // No Assignment Operator
        Environment &operator=(const Environment &) = delete;

        // Get get_top coroutine in the stack 当前线程的栈顶协程
        Coroutine *get_top();

    private:
        // No explicit constructor
        Environment();

        void push(std::shared_ptr<Coroutine> coroutine);

        void pop();

    private:
        // Coroutine calling stack 这个玩意就是协程之间的调用栈 
        std::array<std::shared_ptr<Coroutine>, 1024> _c_stack;

        // Top of the coroutine calling stack 栈顶指针 该栈顶指针初始化为0
        size_t _c_stack_top;

        // Main coroutine(root) 线程主协程
        std::shared_ptr<Coroutine> _main;
    };

    // A default factory method
    template<typename Entry, typename ...Args>
    inline std::shared_ptr<Coroutine> Environment::create_coroutine(Entry &&entry, Args &&...arguments) { //这里是万能引用
        return std::make_shared<Coroutine>(
            //把函数指针和参数都做了完美转发
                this, std::forward<Entry>(entry), std::forward<Args>(arguments)...);
    }

} // namespace stack_co

#endif //COROUTINE_ENVIRONMENT_H
