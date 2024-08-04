#ifndef __SYLAR_THREAD_H__
#define __SYLAR_THREAD_H__

#include "mutex.h"

namespace sylar {
/*
1. 为什么不直接使用C++11提供的thread类。按sylar的描述，
因为thread其实也是基于pthread实现的。并且C++11里面没有提供读写互斥量，
RWMutex，Spinlock等，
在高并发场景，这些对象是经常需要用到的，所以选择自己封装pthread。
*/
/**
 * @brief 线程类
 */
class Thread : Noncopyable {        //继承自noncopyable类，默认私有继承
public:
    /// 线程智能指针类型
    typedef std::shared_ptr<Thread> ptr;

    /**
     * @brief 构造函数
     * @param[in] cb 线程执行函数
     * @param[in] name 线程名称
     */
    /*
    关于线程入口函数。sylar的线程只支持void(void)类型的入口函数，
    不支持给线程传参数，
    但实际使用时可以结合std::bind来绑定参数，
    这样就相当于支持任何类型和数量的参数。
    */
    Thread(std::function<void()> cb, const std::string &name);

    /**
     * @brief 析构函数
     */
    ~Thread();

    /**
     * @brief 线程ID
     */
    pid_t getId() const { return m_id; }

    /**
     * @brief 线程名称
     */
    const std::string &getName() const { return m_name; }

    /**
     * @brief 等待线程执行完成 阻塞
     */
    void join();

    /**
     * @brief 获取当前的线程指针
     */
    static Thread *GetThis();

    /**
     * @brief 获取当前的线程名称
     */
    static const std::string &GetName();

    /**
     * @brief 设置当前线程名称
     * @param[in] name 线程名称
     */
    static void SetName(const std::string &name);

private:
    /**
     * @brief 线程执行函数
     */
    static void *run(void *arg);

private:
    /// 线程id
    pid_t m_id = -1;
    /// 线程结构
    pthread_t m_thread = 0;
    /// 线程执行函数
    std::function<void()> m_cb;
    /// 线程名称
    std::string m_name;

    /// 信号量，使用默认初值0 
    //我在怀疑这里引入信号量的必要性？
    Semaphore m_semaphore;
};

} // namespace sylar

#endif
