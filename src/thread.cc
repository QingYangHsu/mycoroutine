#include "thread.h"
// #include "log.h"
#include "util.h"

namespace sylar {

//存储当前线程thread指针
static thread_local Thread *t_thread          = nullptr;

//存储当前线程名
static thread_local std::string t_thread_name = "UNKNOW";

//static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread *Thread::GetThis() {
    return t_thread;
}

const std::string &Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string &name) {
    if (name.empty()) {
        return;
    }
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string &name)
    : m_cb(cb)
    , m_name(name) {
    if (name.empty()) {
        m_name = "UNKNOW";
    }
    //创建线程 并且设置线程主函数为run
    std::cout<<"new 一个thread"<<std::endl;
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) {
        // SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
        //                           << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    //信号量值减一 如果小于0就阻塞直到信号值大于0
    m_semaphore.wait();
}

Thread::~Thread() {
    if (m_thread) {
        //设置线程脱离，线程结束后其资源由os回收
        pthread_detach(m_thread);
    }
}

void Thread::join() {
    if (m_thread) {
        //等待线程执行完成
        int rt = pthread_join(m_thread, nullptr);
        if (rt) {
            // SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
            //                           << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void *Thread::run(void *arg) {
    //解传入的this指针，其是一个Thread*类型指针
    Thread *thread = (Thread *)arg;
    
    //同步到全局变量上
    t_thread       = thread;
    t_thread_name  = thread->m_name;
    
    thread->m_id   = sylar::GetThreadId();
    
    //设置线程名称
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb);

    
    //信号量值++，准备执行client函数
    thread->m_semaphore.notify();

    cb();           //真正执行client函数
    
    return 0;
}

} // namespace sylar
