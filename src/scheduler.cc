#include "scheduler.h"
// #include "macro.h"
#include "hook.h"       //因为run中的set_hook_enable
#include <cassert>
#include "util.h"
namespace sylar {

//static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 当前线程的调度器，同一个调度器下的所有线程共享同一个实例
static thread_local Scheduler *thread_scheduler = nullptr;

/// 当前线程的调度协程，每个线程都独有一份
static thread_local Fiber *thread_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name) {
    assert(threads > 0);
    //SYLAR_ASSERT(threads > 0);

    m_useCaller = use_caller;
    m_name      = name;

    if (use_caller) {
        --threads;

        //为caller线程搞一个主协程出来 该主协程不设置函数 不设置运行栈空间
        sylar::Fiber::GetThis();

        //SYLAR_ASSERT(GetThis() == nullptr);
        assert(GetThis() == nullptr);
        
        //设置caller线程的thread_scheduler
        thread_scheduler = this;

        /**
         * caller线程的主协程不会被线程的调度协程run进行调度，而且，线程的调度协程停止时，应该返回caller线程的主协程
         * 在user caller为true情况下，把caller线程的主协程暂时保存起来，等调度协程结束时，再resume caller协程
         */

        /*
        调度器内部会初始化一个属于caller线程的调度协程并保存起来
        （比如，在main函数中创建的调度器，如果use_caller为true，那调度器会初始化一个属于main函数线程的调度协程）
        */

       //为caller线程搞一个调度协程出来
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));

        sylar::Thread::SetName(m_name);

        thread_scheduler_fiber = m_rootFiber.get();
        m_rootThread      = sylar::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    } 
    else {
        //提醒一下：如果不调度caller线程，caller线程的thread_scheduler为null
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler *Scheduler::GetThis() { 
    return thread_scheduler; 
}

Fiber *Scheduler::GetMainFiber() { 
    return thread_scheduler_fiber;
}

void Scheduler::setThis() {
    thread_scheduler = this;
}

Scheduler::~Scheduler() {
    //SYLAR_LOG_DEBUG(g_logger) << "Scheduler::~Scheduler()";
    assert(m_stopping);

    //SYLAR_ASSERT(m_stopping);
    if (GetThis() == this) {
        thread_scheduler = nullptr;
    }
}

//启动调度器
void Scheduler::start() {
    std::cout<<"start begins"<<std::endl;
    //SYLAR_LOG_DEBUG(g_logger) << "start";
    MutexType::Lock lock(m_mutex);
    if (m_stopping) {
        //SYLAR_LOG_ERROR(g_logger) << "Scheduler is stopped";
        return;
    }
    assert(m_threads.empty());
    //SYLAR_ASSERT(m_threads.empty());

    //创建工作线程池 该线程池中的所有工作线程负责执行任务 元素类型是Thread类的shared ptr
    m_threads.resize(m_threadCount);
    //std::cout<<"threadcount:"<<m_threadCount<<std::endl;

    for (size_t i = 0; i < m_threadCount; i++) {
        //线程主函数(或者说是线程的主协程)设置为run，即调度，为本线程分配工作
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                      m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);

    //停止位为true 任务队列为空 当前正在工作的工作线程数位0
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle() { 
    //SYLAR_LOG_DEBUG(g_logger) << "ticlke"; 
}

void Scheduler::idle() {
    //SYLAR_LOG_DEBUG(g_logger) << "idle";
    //如果调度器还没终止 每个工作线程的idle协程就一直活着
    while (!stopping()) {
        sylar::Fiber::GetThis()->yield();
    }
}

//该stop只能由new出调度器的线程调用，也就是caller线程，工作线程不能调用该函数，事实上，我们也不会在工作线程中调用该函数，
//但是为了防呆，我们还是做预防性设计
void Scheduler::stop() {
    //std::cout<<"tag:4-1"<<std::endl;
    //SYLAR_LOG_DEBUG(g_logger) << "stop";
    if (stopping()) {
        //如果已经停止了 ok 直接返回
        return;
    }
    //std::cout<<"tag:4-2"<<std::endl;
    //表示我们正在停止
    m_stopping = true;

    /// 如果use caller，那只能由caller线程发起stop
    if (m_useCaller) {
        assert(GetThis() == this);
        //SYLAR_ASSERT(GetThis() == this);
    } 
    else {  //如果不是use caller，那由生成调度器的线程发起stop,而caller 线程的thread_scheduler为NULL，可以去看scheduler构造函数
        //std::cout<<"i think this is impossible"<<std::endl;
        assert(GetThis() != this);
        //SYLAR_ASSERT(GetThis() != this);
    }

    //向所有可以调度的线程发送tickle，通知其他调度线程的调度协程退出调度
    //m_threadCount中不包括caller线程
    for (size_t i = 0; i < m_threadCount; i++) {
        std::cout<<"stop thread:tickle "<<std::endl;
        tickle();
    }

    //通知当前线程的调度协程退出调度
    if (m_rootFiber) {
        std::cout<<"stop root_fiber:tickle "<<std::endl;
        tickle();
    }

    /// 在use caller情况下，caller线程的调度器协程结束时，应该返回到caller线程主协程
    if (m_rootFiber) {
        //std::cout<<"tag:4-3"<<std::endl;
        //caller线程的调度协程
        //之前还是线程主协程，现在resume之后调度权来到调度协程手里
        m_rootFiber->resume();

        //caller线程的调度协程执行完成
        //SYLAR_LOG_DEBUG(g_logger) << "m_rootFiber end";
    }

    std::vector<Thread::ptr> thrs;
    {
        //std::cout<<"tag:4-4"<<std::endl;
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }

    //等所有工作线程结束
    for (auto &i : thrs) {
        //一个一个等待工作线程执行完成
        i->join();
    }

    //std::cout<<"tag:4-5"<<std::endl;
    //当执行到这里，任务队列已经为空，m_stopping为true，m_activeThreadCount为0
}

//起到调度协程作用，是每个工作线程的线程主函数(即每个工作线程主协程的主函数)。也是caller线程的调度协程主函数
void Scheduler::run() {
    std::cout<<"tag:5-1"<<std::endl;
    //SYLAR_LOG_DEBUG(g_logger) << "run";
    
    //默认情况下，协程调度器的调度线程会开启hook，而其他工作线程则不会开启。
    set_hook_enable(true);

    //设置当前线程的调度器 是所有线程共享一个调度器scheduler
    setThis();

    //若当前线程不是caller线程，而是工作线程
    if (sylar::GetThreadId() != m_rootThread) {

        //将工作线程的主协程(当前正在运行)设置为当前协程
        thread_scheduler_fiber = sylar::Fiber::GetThis().get();
    }

    //new出一个空闲协程 该空闲协程也是子协程 默认接收调度器调度
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    
    //当任务是函数时，将其包装成协程
    Fiber::ptr cb_fiber;

    ScheduleTask task;

    //每一个循环体是一轮调度 这里是无限循环 这就是每个线程的调度协程一直在做的事情
    while (true) {
        //重置上一轮的任务 因为本轮还要接着用这个task
        task.reset();

        bool tickle_me = false; // 是否tickle其他线程进行任务调度 因为可能该任务指定线程 或者有剩下的任务
        {
            //搞了把线程锁 这里是一把局部锁 当离开本作用域 自动解锁并且销毁
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();

            // 遍历所有调度任务
            while (it != m_tasks.end()) {
                if (it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，然后跳过这个任务，继续下一个
                    ++it;
                    tickle_me = true;
                    continue;
                }

                // 找到一个未指定线程，或是指定了当前线程的任务
                //该任务不是fiber类型就是cb类型
                //SYLAR_ASSERT(it->fiber || it->cb);
                assert(it->fiber || it->cb);
                // if (it->fiber) {
                //     // 任务队列时的协程一定是READY状态，谁会把RUNNING或TERM状态的协程加入调度呢？
                //     SYLAR_ASSERT(it->fiber->getState() == Fiber::READY);
                // }

                // [BUG FIX]: hook IO相关的系统调用时，在检测到IO未就绪的情况下，会先添加对应的读写事件，再yield当前协程，等IO就绪后再resume当前协程
                // 多线程高并发情境下，有可能发生刚添加事件就被触发的情况，如果此时当前协程还未来得及yield，则这里就有可能出现协程状态仍为RUNNING的情况
                // 这里简单地跳过这种情况，以损失一点性能为代价，否则整个协程框架都要大改
                if(it->fiber && it->fiber->getState() == Fiber::RUNNING) {
                    ++it;
                    continue;
                }

                //fiber的状态为running
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);

                //工作线程数++
                ++m_activeThreadCount;
                break;
            }   //end while
            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }   //局域锁失效

        if (tickle_me) {
            //当前线程通知其他线程
            std::cout<<"run:tickle"<<std::endl;
            tickle();
        }

        //接下来判断该调度协程为本工作线程选中的任务类型
        if (task.fiber) {
            std::cout<<"拿到一个fiber"<<std::endl;
            // resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成了，活跃(工作)线程数减一
            task.fiber->resume();
            --m_activeThreadCount;

            //重置任务
            task.reset();
        } 
        else if (task.cb) {
            std::cout<<"拿到一个cb"<<std::endl;
            if (cb_fiber) {
                cb_fiber->reset(task.cb);
            } 
            else {
                //这里的任务fiber默认接受调度器调度
                cb_fiber.reset(new Fiber(task.cb));
            }
            //重置任务
            task.reset();
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        } 
        else {
            std::cout<<"任务队列为空"<<std::endl;
            // 进到这个分支情况一定是任务队列空了，调度idle协程即可
            if (idle_fiber->getState() == Fiber::TERM) {
                // 如果调度器没有调度任务，那么idle协程会不停地resume/yield，不会结束，如果idle协程结束了，那一定是调度器停止了
                //SYLAR_LOG_DEBUG(g_logger) << "idle fiber term";
                break;      //跳出无限循环
            }
            //调度idle协程，空闲线程数++ 即本线程空闲了
            ++m_idleThreadCount;
            idle_fiber->resume();

            //从idle协程退出回到调度协程，不管其是正常结束 还是 中途yield
            --m_idleThreadCount;
        }
    }  //无限循环结束

    //SYLAR_LOG_DEBUG(g_logger) << "Scheduler::run() exit";
    std::cout<<"run exit"<<std::endl;
}

} // end namespace sylar