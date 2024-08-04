//
// Created by Jasonkay on 2022/6/5.
//

#include "coroutine.h"
#include "environment.h"
#include <iostream>

namespace stack_co {

    Coroutine &Coroutine::get_thread_top_coroutine() {
        return *Environment::get_instance().get_top();
    }

    //暂时没太看懂
    bool Coroutine::test() {
        //return get_thread_top_coroutine()._context.test();              //这里感觉需要改一下 
        return _context.test();
    }

    Status Coroutine::runtime() const {
        return _runtime;
    }

    bool Coroutine::exit() const {
        return _runtime & Status::EXIT;
    }

    bool Coroutine::running() const {
        return _runtime & Status::RUNNING;
    }
    
    //当前coroutine拿到cpu执行权
    Status Coroutine::resume() {
        if (!(_runtime & Status::RUNNING)) {
            //预备当前协程的上下文
            //this是call when finish函数的参数指针 也标定了要拿到cpu执行权的协程个体
            _context.prepare_context(Coroutine::call_when_finish, this);
            //running位 置1
            _runtime |= Status::RUNNING;
            //exit位清0
            _runtime &= ~Status::EXIT;
        }

        //当前环境的栈顶协程
        auto previous = _master_env->get_top();

        //将当前要拿到cpu执行权的协程push进environment的栈顶
        _master_env->push(shared_from_this());

        //cpu上下文从previous的栈顶协程换到this新push的当前协程
        _context.switch_from(&previous->_context);

        //返回更新的coroutine status
        return _runtime;
    }

    void Coroutine::yield() {
        auto &coroutine = get_thread_top_coroutine();    //当前要让出cpu执行权的协程 tmd也就是本协程
        auto &currentContext = coroutine._context;

        coroutine._master_env->pop();

        auto &previousContext = get_thread_top_coroutine()._context; //即将获得cpu执行权的目标协程

        //cpu上下文从current的栈顶协程换到previous的当前协程
        previousContext.switch_from(&currentContext);
    }

    void Coroutine::call_when_finish(Coroutine *coroutine) {
        auto &func = coroutine->_entry;
        auto &runtime_status = coroutine->_runtime;
        if (func) func();

        //执行client函数完成

        //由run变exit(yield) 或者由exit变run(resume)
        runtime_status ^= (Status::EXIT | Status::RUNNING);
        
        // coroutine->yield();
        yield();
    }

} // namespace stack_co
