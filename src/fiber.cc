/**
 * @file fiber.cpp
 * @brief 协程实现
 * @version 0.1
 * @date 2021-06-15
 */

#include <atomic>
#include <cassert>
#include "fiber.h"
// #include "config.h"
// #include "log.h"
// #include "macro.h"
#include "scheduler.h"      
//按理来说在fiber中不应该考虑scheduler相关，
//但是我们需要考虑协程是否参与调度器调度，如果参与调度器调度，其返回时cpu给调度协程，如果不参与，其返回时cpu给线程主协程
//所以这里引入scheduler.h
namespace sylar {

//static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};

/// 全局静态变量，用于统计当前的协程数
static std::atomic<uint64_t> s_fiber_count{0};

/// 线程局部变量，当前线程正在运行的协程
static thread_local Fiber *thread_fiber = nullptr;

/// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_thread_fiber = nullptr;

//协程栈大小，可通过配置文件获取，默认128k
// static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
//     Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

const int g_fiber_stack_size = 128 * 1024;

/**
 * @brief malloc栈内存分配器
 */
class MallocStackAllocator {
public:
    static void *Alloc(size_t size) { return malloc(size); }
    static void Dealloc(void *vp, size_t size) { return free(vp); }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    if (thread_fiber) {
        return thread_fiber->getId();
    }
    return 0;
}

Fiber::Fiber() {

    //设置为当前线程正在运行的协程 也就是设置t_fiber
    SetThis(this);
    
    m_state = RUNNING;

    if (getcontext(&m_ctx)) {
        // SYLAR_ASSERT2(false, "getcontext");
        assert(false);
    }
    /*
    关于线程主协程的构建。线程主协程代表线程入口函数或是main函数所在的协程，
    这两种函数都不是以协程的手段创建的，
    所以它们只有ucontext_t上下文，但没有入口函数，也没有分配栈空间。
    可以这样理解 线程主协程的运行空间在线程栈区
    */
    ++s_fiber_count;
    m_id = s_fiber_id++; // 协程id从0开始，用完加1

    //SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() main id = " << m_id;
}

//设置当前正在运行的协程
void Fiber::SetThis(Fiber *f) { 
    thread_fiber = f; 
}

/**
 * 获取当前正在运行的协程，同时充当初始化当前线程主协程的作用，这个函数在使用协程之前要调用一下
 */
Fiber::ptr Fiber::GetThis() {
    if (thread_fiber) {
        return thread_fiber->shared_from_this();
    }
    //走到这里说明thread_fiber为空 也就是当前线程没有正在运行的协程 那么我们new出第一个协程
    //调用fiber无参构造函数
    Fiber::ptr main_fiber(new Fiber);

    //SYLAR_ASSERT(thread_fiber == main_fiber.get());
    assert(thread_fiber == main_fiber.get());
    
    //设置当前线程主协程
    t_thread_fiber = main_fiber;
    return thread_fiber->shared_from_this();
}

/**
 * 带参数的构造函数用于创建工作子协程，需要分配栈 这里体现出独立栈的特点
 * run_in_scheduler表示是否参与调度器调度
 */
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler)
    : m_id(s_fiber_id++)
    , m_cb(cb)
    , m_runInScheduler(run_in_scheduler) {
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size;
    m_stack     = StackAllocator::Alloc(m_stacksize);

    if (getcontext(&m_ctx)) {
        //SYLAR_ASSERT2(false, "getcontext");
        assert(false);
    }

    m_ctx.uc_link          = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    //设置协程的函数
    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    //SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() id = " << m_id;
}

/**
 * 线程的主协程析构时需要特殊处理，因为主协程没有分配栈和cb
 */
Fiber::~Fiber() {
    //SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber() id = " << m_id;
    --s_fiber_count;
    if (m_stack) {
        // 有栈，说明是子协程，需要确保子协程一定是结束状态
        assert(m_state == TERM);
        //SYLAR_ASSERT(m_state == TERM);
        StackAllocator::Dealloc(m_stack, m_stacksize);
        //SYLAR_LOG_DEBUG(g_logger) << "dealloc stack, id = " << m_id;
    } 
    else {
        // 没有栈，说明是线程的主协程
        //SYLAR_ASSERT(!m_cb);              // 主协程没有cb
        assert(!m_cb);
        assert(m_state == RUNNING);
        //SYLAR_ASSERT(m_state == RUNNING); // 主协程一定是执行状态

        Fiber *cur = thread_fiber; // 当前协程就是自己，也就是线程主协程
        if (cur == this) {
            SetThis(nullptr);
        }
    }
}

/**
 * 这里为了简化状态管理，强制只有TERM状态的协程才可以重置，但其实刚创建好但没执行过的协程也应该允许重置的(但是这里不做)
 */
void Fiber::reset(std::function<void()> cb) {
    //SYLAR_ASSERT(m_stack);
    assert(m_stack);
    assert(m_state == TERM);
    // SYLAR_ASSERT(m_state == TERM);
    m_cb = cb;
    if (getcontext(&m_ctx)) {
        assert(false);
        //SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link          = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = READY;
}

//key
void Fiber::resume() {
    //std::cout<<"resume begin"<<std::endl;
    //SYLAR_ASSERT(m_state != TERM && m_state != RUNNING);
    assert(m_state != TERM && m_state != RUNNING);
    SetThis(this);
    m_state = RUNNING;
    //std::cout<<"tag1"<<std::endl;
    // 如果协程参与调度器调度，那么应该和线程的调度协程进行swap，而不是线程主协程
    //注意：在工作线程(也就是非caller线程)中，调度协程与线程主协程是一样的
    //但是在caller线程中，两者是不一样的概念
    if (m_runInScheduler) {
        if ( swapcontext( &(Scheduler::GetMainFiber()->m_ctx), &(this->m_ctx) ) ) {
            assert(false);
            //SYLAR_ASSERT2(false, "swapcontext");
        }
    } 
    else {
        if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
            assert(false);
            //SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

//key
void Fiber::yield() {
    /// 协程运行完之后会自动yield一次，用于回到主协程，此时状态已为结束状态
    assert(m_state == RUNNING || m_state == TERM);
    //SYLAR_ASSERT(m_state == RUNNING || m_state == TERM);
    SetThis(t_thread_fiber.get());
    if (m_state != TERM) {
        m_state = READY;
    }

    // 如果协程参与调度器调度，那么应该和线程的调度协程进行swap，而不是线程主协程
    if (m_runInScheduler) {
        if (swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx))) {
            assert(false);
            //SYLAR_ASSERT2(false, "swapcontext");
        }
    } else {
        if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) {
            assert(false);
            //SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

/**
 * 这里没有处理协程函数出现异常的情况，同样是为了简化状态管理，并且个人认为协程的异常不应该由框架处理，应该由开发者自行处理
 */
//这里是对client指定的cb的一层封装
void Fiber::MainFunc() {
    //std::cout<<"main func begin"<<std::endl;

    //拿到当前正在执行的协程
    Fiber::ptr cur = GetThis(); 
    
    //SYLAR_ASSERT(cur);
    assert(cur);

    cur->m_cb();        //真正执行用户指定的client函数
    //执行完成
    cur->m_cb    = nullptr;
    cur->m_state = TERM;    //该协程将用户指定函数执行完成，将自身状态改为TERM

    auto raw_ptr = cur.get(); 
    cur.reset();        // 手动让t_fiber的引用计数减1
    raw_ptr->yield();   //子协程结束，将cpu返回，至于将cpu返回到哪里分两种情况讨论
    std::cout<<"main func end"<<std::endl;
}

} // namespace sylar