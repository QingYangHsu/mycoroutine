//
// Created by Jasonkay on 2022/6/4.
//

#include "environment.h"

namespace stack_co {

    Environment &Environment::get_instance() {
        static thread_local Environment env;
        return env;
    }

    Coroutine *Environment::get_top() {
        return this->_c_stack[this->_c_stack_top - 1].get();
    }

    void Environment::push(std::shared_ptr<Coroutine> coroutine) {
        _c_stack[_c_stack_top++] = std::move(coroutine);
    }

    void Environment::pop() {
        _c_stack_top--;
    }

    Environment::Environment() : _c_stack_top(0) {
        //搞一个线程主协程出来
        _main = std::make_shared<Coroutine>(this, []() {});
        push(_main);
    }

} // namespace stack_co
