#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <vector>
#include <set>
#include "mutex.h"

namespace sylar {

class TimerManager;
/**
 * @brief 定时器
 */
class Timer : public std::enable_shared_from_this<Timer> {
friend class TimerManager;
public:
    /// 定时器的智能指针类型
    typedef std::shared_ptr<Timer> ptr;

    /**
     * @brief 取消定时器
     */
    bool cancel();

    /**
     * @brief 刷新设置定时器的执行时间
     */
    bool refresh();

    /**
     * @brief 重置定时器时间
     * @param[in] ms 定时器执行间隔时间(毫秒) 也就是周期
     * @param[in] from_now 是否从当前时间开始计算
     */
    bool reset(uint64_t ms, bool from_now);
private:
    /**
     * @brief 构造函数
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 回调函数
     * @param[in] recurring 是否循环
     * @param[in] manager 定时器管理器
     */
    Timer(uint64_t ms, std::function<void()> cb,
          bool recurring, TimerManager* manager);
    /**
     * @brief 构造函数
     * @param[in] next 执行的时间戳(毫秒)，也就是定时器下一次应该执行的时间戳
     */
    Timer(uint64_t next);
private:

    /// 是否循环定时器
    bool m_recurring = false;

    /// 执行周期
    uint64_t m_ms = 0;

    /// 精确的执行时间
    /*
    m_next代表定时器下一次应该执行的时间戳（以毫秒为单位）。
    这是一个绝对时间值，表示从某个固定时间点（如程序启动时间、UNIX纪元时间等）到定时器下一次执行之间的毫秒数。
    */
    uint64_t m_next = 0;

    /// 回调函数
    std::function<void()> m_cb;

    /// 定时器管理器
    TimerManager* m_manager = nullptr;
private:
    /**
     * @brief 定时器比较仿函数
     */
    struct Comparator {
        /**
         * @brief 比较定时器的智能指针的大小(按执行时间排序) 因为下面的堆排序需要定义两个定时器的比较规则
         * @param[in] lhs 定时器智能指针
         * @param[in] rhs 定时器智能指针
         */
        //这里传引用 避免一次ptr的copy ctor
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
};

/**
 * @brief 定时器管理器 该类是一个纯虚类，该类不能实例化 用于iomanager的基类
 */
class TimerManager {
friend class Timer;
public:
    /// 读写锁类型
    typedef RWMutex RWMutexType;

    /**
     * @brief 构造函数
     */
    TimerManager();

    /**
     * @brief 析构函数
     */
    virtual ~TimerManager();

    /**
     * @brief 添加定时器
     * @param[in] ms 定时器执行间隔时间 周期
     * @param[in] cb 定时器回调函数
     * @param[in] recurring 是否循环定时器
     */
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb
                        ,bool recurring = false);

    /**
     * @brief 条件判定函数这个在后面的hook模块会用到)
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 定时器回调函数
     * @param[in] weak_cond 条件(即定时器到时后还要判断一下条件)
     * @param[in] recurring 是否循环
     */
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb
                        ,std::weak_ptr<void> weak_cond
                        ,bool recurring = false);

    /**
     * @brief 当前时间到最近一个定时器执行的时间间隔(毫秒)
     * 如果定时器列表为空 返回一个极大值 如果定时器列表最近的定时器时间还没到达 根据当前时间计算出一个等待时间 
     * 如果定时器列表最近的定时器时间已经到达 返回0，表示一刻也不用等
     */
    uint64_t get_the_most_recent_Timer_time();

    /**
     * @brief 获取需要执行的定时器的回调函数列表
     * @param[out] cbs 回调函数数组 这是一个传出参数
     */
    void listExpiredCb(std::vector<std::function<void()> >& cbs);

    /**
     * @brief 是否有定时器,即定时器数组是否为空
     */
    bool hasTimer();

protected:

    /**
     * @brief 当有新的定时器插入到定时器的首部,执行该函数 该函数设置为虚函数，其实现在子类中实现 也就是iomanager中实现 类似scheduler中的tickle函数
     */
    virtual void onTimerInsertedAtFront() = 0;

    /**
     * @brief 将定时器添加到管理器中
     */
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);
private:
    /**
     * @brief 检测服务器时间是否被调后了
     */
    bool detectClockRollover(uint64_t now_ms);
private:
    /// Mutex 底层就是pthread_rwlock_t的锁
    RWMutexType m_mutex;

    /// 定时器集合 使用有序集合set
    std::set<Timer::ptr, Timer::Comparator> m_timers;

    /// 是否触发onTimerInsertedAtFront
    bool m_tickled = false;

    /// 上次执行时间？或者理解为定时器管理类的刷新时间
    uint64_t m_previouseTime = 0;
};

}

#endif
