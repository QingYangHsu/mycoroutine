#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include "fiber.h"
// #include "log.h"
#include "thread.h"
#include <vector>
#include<iostream>

namespace sylar {
//好好理解下面这句话 这是核心
//在非caller线程里，调度协程就是调度线程的主线程，
//但在caller线程里，调度协程并不是caller线程的主协程，而是相当于caller线程的子协程
//也就是说在caller线程里，会有三种协程角色，主协程，调度协程，工作子协程

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器 N个线程运行M个协程 
 *          协程相当于是用户指定的任务
 *          协程可以在线程之间进行切换，也可以绑定到指定线程运行(用户指定)
 *          内部有一个线程池,支持协程在线程池里面切换
 */

class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    /**
     * @brief 创建调度器
     * @param[in] threads 线程数
     * @param[in] use_caller 是否将当前线程(main函数所在线程)也作为调度线程
     * @param[in] name 名称
     */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "Scheduler");

    /**
     * @brief 析构函数
     */
    virtual ~Scheduler();

    /**
     * @brief 获取调度器名称
     */
    const std::string &getName() const { return m_name; }

    /**
     * @brief 获取当前线程调度器指针
     */
    static Scheduler *GetThis();

    /**
     * @brief 获取当前线程(工作线程)的主协程，注意：caller线程的主协程不通过该方法获得,而是通过fiber.cc的t_thread_fiber获得
     * 因为在工作线程中，调度协程等于主协程，在caller线程中，两者不相等
     */
    static Fiber *GetMainFiber();

    /**
     * @brief 添加调度任务 添加调度任务的行为由客户完成，或者是caller线程的主协程，其完成客户添加调度任务的角色
     * @tparam FiberOrCb 调度任务类型，可以是协程对象或函数指针
     * @param[] fc 协程对象或指针
     * @param[] thread 指定运行该任务的线程号，-1表示任意线程
     */
    template <class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;           //一个标志位 是否需要通知工作线程有活了 如果原来任务队列为空会起作用
        {
            //局域锁 是一把线程锁 也就是协程一旦lock 整个线程都会阻塞
            MutexType::Lock lock(m_mutex);
            //如果原本队列为空，need_tickle为true
            need_tickle = scheduleNoLock(fc, thread);
        }

        if (need_tickle) {
            std::cout<<"schedule :tickle"<<std::endl;
            tickle(); // 通知scheduler有任务了
        }
    }

    /**
     * @brief 启动调度器
     */
    void start();

    /**
     * @brief 停止调度器，等所有调度任务都执行完了再返回
     */
    void stop();

protected:
    /**
     * @brief 通知协程调度器有任务了
     */
    virtual void tickle();

    /**
     * @brief 协程调度函数
     */
    void run();

    /**
     * @brief 无任务调度时就resume idle协程
     */
    virtual void idle();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();

    /**
     * @brief 设置当前的协程调度器
     */
    void setThis();

    /**
     * @brief 返回是否有空闲线程
     * @details 当调度协程进入idle时空闲线程数加1，从idle协程返回时空闲线程数减1
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0; }

private:
    /**
     * @brief 添加调度任务，无锁(因为该函数的上一层调用时已经加锁了，所以进该函数一定是独立的，无竞态问题)
     * @tparam FiberOrCb 调度任务类型，可以是协程对象或函数指针
     * @param[] fc 协程对象或指针
     * @param[] thread 指定运行该任务的线程号，-1表示任意线程
     */
    template <class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        //如果原本队列为空，需要tickle
        bool need_tickle = m_tasks.empty();
        ScheduleTask task(fc, thread);
        
        //对task进行任务判断
        if (task.fiber || task.cb) {
            m_tasks.push_back(task);
        }
        return need_tickle;
    }

private:
    /**
     * @brief 调度任务，协程/函数二选一，可指定在哪个线程上调度
     */
    struct ScheduleTask {
        Fiber::ptr fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask(Fiber::ptr f, int thr) {
            fiber  = f;
            thread = thr;
        }

        ScheduleTask(Fiber::ptr *f, int thr) {
            fiber.swap(*f);
            thread = thr;
        }

        ScheduleTask(std::function<void()> f, int thr) {
            cb     = f;
            thread = thr;
        }

        ScheduleTask() { thread = -1; }

        void reset() {
            fiber  = nullptr;
            cb     = nullptr;
            thread = -1;
        }
    };

private:
    /// 协程调度器名称
    std::string m_name;

    /// 互斥锁 其底层就是pthread_mutex_t的锁
    MutexType m_mutex;
    
    /// 线程池
    std::vector<Thread::ptr> m_threads;
    
    /// 任务队列
    std::list<ScheduleTask> m_tasks;
    
    /// 线程池的线程ID数组
    std::vector<int> m_threadIds;
    
    /// 工作线程数量，不包含use_caller的主线程
    size_t m_threadCount = 0;
    
    /// 活跃线程数 工作线程数 也就是当前正在执行任务的线程数量
    std::atomic<size_t> m_activeThreadCount = {0};
    
    /// idle线程数 空闲的工作线程数量
    std::atomic<size_t> m_idleThreadCount = {0};

    /// caller线程是否参与调度
    bool m_useCaller;

    /// use_caller为true时，调度器所在线程的调度协程
    Fiber::ptr m_rootFiber;
    
    /// use_caller为true时，调度器所在线程的id
    int m_rootThread = 0;

    /// 是否正在停止
    bool m_stopping = false;
};

} // end namespace sylar

#endif
