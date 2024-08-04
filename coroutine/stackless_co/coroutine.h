//
// Created by Jasonkay on 2022/6/6.
//

#ifndef COROUTINE_COROUTINE_H
#define COROUTINE_COROUTINE_H

#include "utils.h"
#include "schedule.h"

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdint>

#if __APPLE__ && __MACH__
#include <sys/ucontext.h>
#else

#include <ucontext.h>

#endif

namespace stackless_co {

    class Schedule;

    class Coroutine {
    public:
        //创建并初始化一个新协程对象，同时将该协程对象放到协程调度器里面
        static Coroutine *new_co(Schedule *s, coroutine_func func, void *ud);

        //关闭当前协程，并释放协程中的资源
        void delete_co();

        inline coroutine_func get_func() {
            return func;
        }

        inline ucontext_t *get_ctx() {
            return &ctx;
        }

        inline int get_status() {
            return status;
        }

        inline ptrdiff_t get_size() {
            return this->size;
        }

        inline char *get_stack() {
            return this->stack;
        }

        inline ptrdiff_t get_cap() {
            return this->cap;
        }

        inline void *get_ud() {
            return this->ud;
        }

        inline void set_status(int status) {
            this->status = status;
        }

        inline void set_stack(char *stack) {
            this->stack = stack;
        }

        inline void set_cap(ptrdiff_t cap) {
            this->cap = cap;
        }

        inline void set_size(ptrdiff_t size) {
            this->size = size;
        }

    private:
        //coroutine_func是函数指针类型
        coroutine_func func;

        void *ud;   //the parameters of the func
        
        
        
        ptrdiff_t cap;      //已经分配的运行时栈容量
        
        ptrdiff_t size;     //当前协程运行时栈，保存起来后的大小
        
        int status;         //协程当前的状态

        //下面两个成员最为关键
        ucontext_t ctx;     //context 上下文信息 包括一些寄存器的值 
        
        char *stack;        //当前协程的保存起来的运行时栈
    };

} // namespace stackless_co

#endif //COROUTINE_COROUTINE_H
