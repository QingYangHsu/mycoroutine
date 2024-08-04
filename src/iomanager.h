#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "scheduler.h"
#include "timer.h"

namespace sylar {
//sylar的IO协程调度器对应IOManager
class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;

    //RWMutex就是在mutex类中对pthread_rwlock_t的一个封装
    typedef RWMutex RWMutexType;

    /**
     * @brief IO事件，继承自epoll对事件的定义
     * @details 这里只关心socket fd的读和写事件，其他epoll事件会归类到这两类事件中
     */
    enum Event {
        /// 无事件
        NONE = 0x0,
        /// 读事件(EPOLLIN)
        READ = 0x1,
        /// 写事件(EPOLLOUT)
        WRITE = 0x4,
    };

private:
    /**
     * @brief socket fd上下文类(三元组)     相当于客户信息，server端每接收一个新client连接都会创建一个三元组
     * @details 每个socket fd都对应一个FdContext，其中包括fd的值(fd)，fd上的事件(events)，以及fd的读写事件上下文(read_ctx write_ctx) 因为我们可以对一个fd同时注册读事件和写事件
     */
    struct FdContext {
        typedef Mutex MutexType;
        /**
         * @brief 事件上下文类
         * @details fd的每个事件都有一个事件上下文，保存这个事件的回调函数以及执行回调函数的调度器
         *          sylar对fd事件做了简化，只预留了读事件和写事件，所有的事件都被归类到这两类事件中
         */
        //类比于scheduler中定义的scheduleTask
        struct EventContext {
            /// 执行事件回调的调度器
            Scheduler *scheduler = nullptr;

            /// 事件回调协程
            Fiber::ptr fiber;

            /// 事件回调函数
            std::function<void()> cb;
        };

        /**
         * @brief 获取事件上下文类(即下面的read 与 wirte两个上下文)，因为每一个fdconnext三元组都有分开的读事件上下文和写事件上下文
         * @param[in] event 事件类型
         * @return 返回对应事件的上下文
         */
        EventContext &getEventContext(Event event);

        /**
         * @brief 重置事件上下文(下面的read 与 write两个事件都可以当做参数传入)
         * @param[in, out] ctx 待重置的事件上下文对象
         */
        void resetEventContext(EventContext &ctx);

        /**
         * @brief 触发事件
         * @details 根据事件类型调用对应上下文结构中的调度器去调度回调协程或回调函数
         * @param[in] event 事件类型
         */
        void triggerEvent(Event event);

        /// 读事件上下文(key)
        EventContext read_ctx;

        /// 写事件上下文(key)
        EventContext write_ctx;

        /// 事件关联的句柄
        int fd = 0;

        /// 该fd添加了哪些事件的回调函数，或者说该fd关心哪些事件(默认是无事件，是一个位的&操作)
        Event events = NONE;

        /// 事件的Mutex，也就是该三元组本身的锁
        MutexType mutex;
    };          //三元组定义结束

public:
    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否调度caller线程
     * @param[in] name 调度器的名称
     */
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");

    /**
     * @brief 析构函数
     */
    ~IOManager();

    /**
     * @brief 向epollfd添加一个监视事件
     * @details fd描述符发生了event事件时执行cb函数
     * @param[in] fd socket句柄
     * @param[in] event 事件类型 无事件，读事件，写事件
     * @param[in] cb 事件回调函数，如果为空，则默认把当前协程作为回调执行体
     * @return 添加成功返回0,失败返回-1
     */
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

    /**
     * @brief 从epollfd中删除一个监视事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 不会触发事件
     * @return 是否删除成功
     */
    bool delEvent(int fd, Event event);

    /**
     * @brief 取消事件
     * //注意：其与delevent的区别在于其会触发一次事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 如果该事件被注册过回调，那就触发一次回调事件
     * @return 是否删除成功
     */
    bool cancelEvent(int fd, Event event);

    /**
     * @brief 取消fd注册的所有事件 也就是取消fd注册的所有读事件或者写事件 只要注册过统统删除
     * @details 所有被注册的回调事件在cancel之前都会被执行一次
     * @param[in] fd socket句柄
     * @return 是否删除成功
     */
    bool cancelAll(int fd);

    /**
     * @brief 返回当前的IOManager
     */
    static IOManager *GetThis();

protected:
    /**
     * @brief 通知调度器有任务要调度
     * @details 具体如何通知？写pipe让idle协程从epoll_wait退出，待idle协程yield之后，工作线程就空出来了，Scheduler::run就可以调度其他任务到工作线程上
     */
    //override关键字将基类中的同名函数覆盖掉 因为我们在scheduler中的tickle啥也没干
    void tickle() override;

    /**
     * @brief 判断是否可以停止
     * @details 判断条件是Scheduler::stopping()外加IOManager的m_pendingEventCount为0，表示没有IO事件可调度了
     */
    bool stopping() override;           //基类中的stopping是无参的

    /**
     * @brief idle协程
     * @details 对于IO协程调度来说，常态是阻塞在等待IO事件上(epoll wait)，idle退出的时机是epoll_wait返回，因为tickle或者是注册的IO事件发生或者超时
     */
    void idle() override;

    /**
     * @brief 判断是否可以停止，同时获取最近一个定时器的超时时间
     * @param[out] timeout 最近一个定时器的超时时间，用于idle协程的epoll_wait
     * @return 返回是否可以停止
     */
    bool stopping(uint64_t& timeout);

    /**
     * @brief 当有定时器插入到头部时，要重新更新epoll_wait的超时时间，这里是唤醒idle协程以便于使用新的超时时间
     */
    void onTimerInsertedAtFront() override;

    /**
     * @brief 重置socket句柄上下文的容器，也就是m_fdContexts该vector的大小
     * @param[in] size 容量大小
     */
    void contextResize(size_t size);

private:

    /// epoll 文件句柄 也就是epollfd
    int m_epfd = 0;

    /// pipe 文件句柄，fd[0]读端，fd[1]写端
    int m_tickleFds[2];

    /// 当前等待执行的IO事件数量(或者说已经注册还未发生的事件数量) 每次addevent 这个值都会++ 每次delevent 或者cancleevent这个值会-- 
    //？这里有问题 等会全局看一下 暂时看上去没有问题 管道读端注册的读事件不会使用addevent接口
    std::atomic<size_t> m_pendingEventCount = {0};

    /// IOManager的Mutex
    RWMutexType m_mutex;

    /// socket事件上下文(三元组)的容器 注意元素类型是任务类的原始指针而不是智能指针，所以要手动释放
    std::vector<FdContext *> m_fdContexts;
};

} // end namespace sylar

#endif