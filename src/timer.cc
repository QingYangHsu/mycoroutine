#include "timer.h"
#include "util.h"
#include "macro.h"

namespace sylar {

bool Timer::Comparator::operator()(const Timer::ptr& lhs
                        ,const Timer::ptr& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    if(!rhs) {
        return false;
    }
    if(lhs->m_next < rhs->m_next) {
        return true;
    }
    if(rhs->m_next < lhs->m_next) {
        return false;
    }
    //如果 m_next 相等，即两个定时器下一次执行时间相同，则比较指针本身大小
    return lhs.get() < rhs.get();
}


Timer::Timer(uint64_t ms, std::function<void()> cb,
             bool recurring, TimerManager* manager)
    :m_recurring(recurring)
    ,m_ms(ms)
    ,m_cb(cb)
    ,m_manager(manager) {
    
    //GetElapsedMS拿到当前时间
    //m_next是定时器下一次执行时间
    m_next = sylar::GetElapsedMS() + m_ms;
}

Timer::Timer(uint64_t next)
    :m_next(next) {
}

bool Timer::cancel() {
    //调用WriteScopedLockImpl<RWMutex>类的copy ctor,并且上锁
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) {
        //将回调函数清零
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
    //析构函数自动解锁
}

bool Timer::refresh() {
    //上锁
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    
    //没找到该timer
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    
    //将时间延续一个周期 GetElapsedMS获取当前启动的毫秒，再往后延续一个周期
    m_next = sylar::GetElapsedMS() + m_ms;

    m_manager->m_timers.insert(shared_from_this());

    return true;

}

//from_now 是否从当前时间开始计算
//从当前时间开始（from_now 为 true）或从上一个周期结束时间开始（from_now 为 false）的情况
bool Timer::reset(uint64_t ms, bool from_now) {
    if(ms == m_ms && !from_now) {
        //什么也不用做
        return true;
    }
    
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }

    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    
    uint64_t start = 0;
    //如果从当前时间开始计算
    if(from_now) {
        start = sylar::GetElapsedMS();
    } 
    else {      //如果不从当前时间开始计算，仅仅改变周期，那就计算出最近到期的时间点，注意：m_next已经在listExpiredCb被更新
        start = m_next - m_ms;
    }

    //更新定时器周期
    m_ms = ms;

    //重新计算m_next
    m_next = start + m_ms;
    
    m_manager->addTimer(shared_from_this(), lock);
    return true;

}

TimerManager::TimerManager() {
    m_previouseTime = sylar::GetElapsedMS();
}

TimerManager::~TimerManager() {
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
    
    //it是一个迭代器类型 其会根据自定义的排序规则选择合适位置进行插入
    auto it = m_timers.insert(val).first;
    
    //如果插入的是定时器容器头部 并且原来没有触发过定时器tickle
    bool at_front = (it == m_timers.begin()) && !m_tickled;
    if(at_front) {
        std::cout<<"插入头部"<<std::endl;
        m_tickled = true;
    }
    lock.unlock();

    //类比于schedule中如果我们新添加一个任务到任务队列，如果原来任务队列为空，我们添加完成之后会执行一次tickle
    //这里是相同的作用
    if(at_front) {
        onTimerInsertedAtFront();
    }
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
                                  ,bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

//条件判定函数
//本函数就是考虑条件情况下，对用户cb的封装
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    /*weak_ptr的lock函数是C++标准库中weak_ptr类的一个成员函数，它的主要作用是尝试获取一个指向weak_ptr所管理的对象的shared_ptr。*/
    std::shared_ptr<void> tmp = weak_cond.lock();
    
    //如果条件满足
    if(tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
                                    ,std::weak_ptr<void> weak_cond
                                    ,bool recurring) {
                                        //将上面的条件判定函数和条件绑定，以及和原始cb绑定，搞成一个新的cb
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::get_the_most_recent_Timer_time() {
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    /*0ull是一个字面量（literal），表示一个无符号长整型（unsigned long long）的零值。由于ull后缀指定了这是一个无符号长整型，所以0ull就是unsigned long long类型的0*/
    if(m_timers.empty()) {
        //~0ull就是一个极大值 表示这个时间永远不会到
        return ~0ull;
    }

    const Timer::ptr& next = *m_timers.begin();
    uint64_t now_ms = sylar::GetElapsedMS();
    //当前毫秒数已经比最近的定时器的超时时间要大，返回0，表示迫不及待
    if(now_ms >= next->m_next) {
        return 0;
    } 
    else {
        //计算出还要等的毫秒数
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    //拿到当前时间
    uint64_t now_ms = sylar::GetElapsedMS();
    
    //超时定时器数组
    std::vector<Timer::ptr> expired;
    {
        //局域读锁
        RWMutexType::ReadLock lock(m_mutex);
        if(m_timers.empty()) {
            return;
        }
    }

    RWMutexType::WriteLock lock(m_mutex);
    if(m_timers.empty()) {
        return;
    }

    //是否发生时间跳变标志
    bool rollover = false;

    //检测是否发生时间跳变
    if(SYLAR_UNLIKELY(detectClockRollover(now_ms))) {
        // 使用clock_gettime(CLOCK_MONOTONIC_RAW)，应该不可能出现时间回退的问题
        rollover = true;
    }

    //定时器列表首个定时器的下一次到期时间还没到，那其他定时器更不可能到
    if(!rollover && ( (*m_timers.begin() )->m_next > now_ms)) {
        return;
    }

    //造一个临时定时器出来 用于筛选出超时的定时器 因为我们自定义了定时器比较规则
    Timer::ptr now_timer(new Timer(now_ms));
    
    //lower_bound 是 C++ 标准模板库（STL）中的一个算法，它用于在有序区间中查找第一个不小于（即大于或等于）给定值的元素
    //这里如果跳变了 就认为所有定时器都到期了 还是非常狠的
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    
    //跳过所有相等定时器
    while(it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }
    //现在it已经移动到超时定时器的下一位

    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    for(auto& timer : expired) {
        cbs.push_back(timer->m_cb);
        
        //该定时器要循环使用 那么更新器m_next，再重新插入
        if(timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } 
        else {
            timer->m_cb = nullptr;
        }
    }
}

/*在某些情况下，如系统重启或夏令时变更，系统时间可能会突然跳变，导致时间戳出现非单调递增的情况。这对于依赖于时间递增性的定时器管理器来说是一个问题，因为这可能导致定时器逻辑混乱。*/
bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    /*
    函数的工作原理如下：
    首先，它检查now_ms是否小于m_previouseTime，这表示时间可能向后移动了。
    然后，它进一步检查now_ms是否小于m_previouseTime减去一小时的时间（即60 * 60 * 1000毫秒）。
    这是因为系统时间的小幅跳变（例如几毫秒或几秒内）可能是正常的，也就是设置一个误差范围
    但超过一个小时的跳变很可能是因为系统时间发生了重置或回滚。
    如果上述条件都满足，函数将rollover标志设置为true，表示检测到了时间回滚。
    最后，无论是否检测到时间回滚，函数都会更新m_previouseTime为当前的时间戳now_ms。
    */
    if(now_ms < m_previouseTime &&
            now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    //更新为最新时间
    m_previouseTime = now_ms;
    return rollover;
}

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}
