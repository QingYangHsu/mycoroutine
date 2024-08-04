/**
 * @file mutex.h
 * @brief 信号量，互斥锁，读写锁，范围锁模板，自旋锁，原子锁
 * @version 0.1
 * @date 2021-06-09
 */
#ifndef __SYLAR_MUTEX_H__
#define __SYLAR_MUTEX_H__

#include <thread>
#include <functional>
#include <memory>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <atomic>
#include <list>

#include "noncopyable.h"

namespace sylar {

/**
 * @brief 信号量
 */
//在C++中，如果你没有显式地指定继承方式（如 public、protected 或 private），那么默认的继承方式是 private
class Semaphore : Noncopyable {
public:
    /**
     * @brief 构造函数
     * @param[in] count 信号量值的大小
     */
    Semaphore(uint32_t count = 0);

    /**
     * @brief 析构函数
     */
    ~Semaphore();

    /**
     * @brief 获取信号量
     */
    void wait();

    /**
     * @brief 释放信号量
     */
    void notify();
private:
    sem_t m_semaphore;
};

/**
 * @brief 局部锁的模板实现
 */
template<class T>
struct ScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex Mutex
     */
    ScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ScopedLockImpl() {
        unlock();
    }

    /**
     * @brief 加锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.lock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部读锁模板实现
 */
template<class T>
struct ReadScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    ReadScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.rdlock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ReadScopedLockImpl() {
        unlock();
    }

    /**
     * @brief 上读锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    /**
     * @brief 释放锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部写锁模板实现
 */
template<class T>
struct WriteScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    WriteScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.wrlock();
        m_locked = true;
    }

    /**
     * @brief 析构函数
     */
    ~WriteScopedLockImpl() {
        unlock();
    }

    /**
     * @brief 上写锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// Mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 互斥锁 线程级别的互斥锁 因为锁类型是pthread_mutex_t
 */
class Mutex : Noncopyable {
public: 
    /// 局部锁
    typedef ScopedLockImpl<Mutex> Lock;

    /**
     * @brief 构造函数
     */
    Mutex() {
        pthread_mutex_init(&m_mutex, nullptr);
    }

    /**
     * @brief 析构函数
     */
    ~Mutex() {
        pthread_mutex_destroy(&m_mutex);
    }

    /**
     * @brief 加锁
     */
    void lock() {
        pthread_mutex_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_mutex_unlock(&m_mutex);
    }
private:
    /// mutex 互斥锁
    pthread_mutex_t m_mutex;
};

/**
 * @brief 空锁(用于调试)
 */
class NullMutex : Noncopyable{
public:
    /// 局部锁
    typedef ScopedLockImpl<NullMutex> Lock;

    /**
     * @brief 构造函数
     */
    NullMutex() {}

    /**
     * @brief 析构函数
     */
    ~NullMutex() {}

    /**
     * @brief 加锁
     */
    void lock() {}

    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 读写互斥锁 读读不冲突 读写 写写 冲突
 */
class RWMutex : Noncopyable{
public:

    /// 局部读锁
    typedef ReadScopedLockImpl<RWMutex> ReadLock;

    /// 局部写锁
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    /**
     * @brief 构造函数
     */
    RWMutex() {
        pthread_rwlock_init(&m_lock, nullptr);
    }
    
    /**
     * @brief 析构函数
     */
    ~RWMutex() {
        pthread_rwlock_destroy(&m_lock);
    }

    /**
     * @brief 上读锁
     */
    void rdlock() {
        pthread_rwlock_rdlock(&m_lock);
    }

    /**
     * @brief 上写锁
     */
    void wrlock() {
        pthread_rwlock_wrlock(&m_lock);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_rwlock_unlock(&m_lock);
    }
private:
    /// 读写锁
    pthread_rwlock_t m_lock;
};

/**
 * @brief 空读写锁(用于调试)
 */
class NullRWMutex : Noncopyable {
public:
    /// 局部读锁
    typedef ReadScopedLockImpl<NullMutex> ReadLock;
    /// 局部写锁
    typedef WriteScopedLockImpl<NullMutex> WriteLock;

    /**
     * @brief 构造函数
     */
    NullRWMutex() {}
    /**
     * @brief 析构函数
     */
    ~NullRWMutex() {}

    /**
     * @brief 上读锁
     */
    void rdlock() {}

    /**
     * @brief 上写锁
     */
    void wrlock() {}
    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 自旋锁 线程级别的自旋锁
 *自旋锁（Spin Lock）是一种轻量级的锁机制，广泛应用于多线程或多处理器环境中，用于同步对共享资源的访问。以下是关于自旋锁的详细解释：
    定义
    自旋锁是一种同步原语，在线程试图获取锁时，如果锁已经被其他线程持有，它不会进入阻塞状态，而是不断地循环检查锁的状态，
    直到获得锁为止。这种忙等待的方式称为“自旋”。
    工作原理
    当一个线程尝试获取某个自旋锁时，如果该锁已被其他线程占用，则当前线程会进入一个循环，不断检查锁的状态。
    循环中，线程会周期性地检查锁是否已被释放（即检查锁的状态变量是否变为可用）。
    一旦锁被释放，即状态变量变为可用，当前线程会立即获取锁并退出循环，继续执行后续操作。
 */
class Spinlock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<Spinlock> Lock;

    /**
     * @brief 构造函数
     */
    Spinlock() {
        pthread_spin_init(&m_mutex, 0);
    }

    /**
     * @brief 析构函数
     */
    ~Spinlock() {
        pthread_spin_destroy(&m_mutex);
    }

    /**
     * @brief 上锁
     */
    void lock() {
        pthread_spin_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_spin_unlock(&m_mutex);
    }
private:
    /// 自旋锁
    pthread_spinlock_t m_mutex;
};

/**
 * @brief 原子锁
 * 这些操作是原子的，这意味着它们在执行过程中不会被线程调度器中断，从而保证了线程安全。
 */
class CASLock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<CASLock> Lock;

    /**
     * @brief 构造函数
     */
    CASLock() {
        m_mutex.clear();
    }

    /**
     * @brief 析构函数
     */
    ~CASLock() {
    }

    /**
     * @brief 上锁
     */
    void lock() {
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }
private:
    /// 原子状态
    volatile std::atomic_flag m_mutex;
};

} // namespace sylar

#endif // __SYLAR_MUTEX_H__
