
//调度器
#ifndef COROUTINE_SCHEDULE_H
#define COROUTINE_SCHEDULE_H

#include "utils.h"
#include "coroutine.h"

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdint>

#if __APPLE__ && __MACH__
#include <sys/ucontext.h>
#else

#include <ucontext.h>

#endif

namespace stackless_co {

    class Coroutine;

    //负责管理用其创建的所有协程
    class Schedule {

    private:
        //当当前运行协程要yield时，将c使用的运行栈从schedule类的共享栈中拷贝到coroutine私有的栈空间之中。top是栈底
        static void _save_stack(Coroutine *C, char *top);

    public:
        //创建并初始化一个协程调度器
        static Schedule *schedule_new();

        //mainfunc 是对用户提供的协程函数的封装，在该函数中执行用户自定义的函数
        static void main_func(uint32_t low32, uint32_t hi32);
        
        //负责销毁协程调度器以及清理其管理的所有协程
        void schedule_close();

        //返回值为新创建的协程在协程数组中的下标
        int coroutine_new(coroutine_func, void *ud);
        
        //当前协程拿到cpu执行权 READY/SUSPEND -> RUNNING ,参数为即将获得cpu的协程在数组中的下标
        void coroutine_resume(int id);

        int coroutine_status(int id);

        int coroutine_running() const;
        
        //让当前运行协程放弃cpu执行权 running->suspend 当前运行协程用running标记
        void coroutine_yield();

    public:

        constexpr static int COROUTINE_DEAD = 0;

        constexpr static int COROUTINE_READY = 1;//就绪态

        constexpr static int COROUTINE_RUNNING = 2;//运行

        constexpr static int COROUTINE_SUSPEND = 3;//暂停

    private:

        //共享栈空间要设置的足够大，所有的栈运行的时候，都使用这个栈空间；
        constexpr static int STACK_SIZE = 1024 * 1024;

        //默认协程数组容量
        constexpr static int DEFAULT_COROUTINE = 16;

    private:
        //所有协程的运行时栈，具体共享栈,全体协程会共享这个栈
        char stack[STACK_SIZE];

        // 主协程的上下文，方便后面协程执行完后切回到主协程
        ucontext_t main;

        //当前数组协程总量 并不包括main主协程
        int nco = 0 ;

        //当前数组协程最大容量 
        int cap_of_vec = 0;

        //标记当前正在运行的协程id
        int running;

        //一个一维数组，存放了目前其管理的所有协程 main这个主协程并没有放到vec里面
        Coroutine **co_vec;
    };

} // namespace stackless_co

#endif //COROUTINE_SCHEDULE_H
