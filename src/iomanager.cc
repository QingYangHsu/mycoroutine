#include <unistd.h>    // for pipe()
#include <sys/epoll.h> // for epoll_xxx()
#include <fcntl.h>     // for fcntl()
#include "iomanager.h"
#include <string.h>
#include <cassert>
// #include "log.h"
#include "macro.h"  //用于分支预测

namespace sylar {

// static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

enum EpollCtlOp {       //空类 没什么用
};
/*
这个函数重载了 std::ostream 的 << 操作符，以便能够将 EpollCtlOp 枚举类型的值以字符串形式输出。
EpollCtlOp 枚举类型可能包含如 EPOLL_CTL_ADD、EPOLL_CTL_MOD 和 EPOLL_CTL_DEL 这样的值，
这些值分别代表 epoll 控制操作的不同类型。
*/
static std::ostream &operator<<(std::ostream &os, const EpollCtlOp &op) {
    switch ((int)op) {
#define XX(ctl) \
    case ctl:   \
        return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
#undef XX
    default:
        return os << (int)op;
    }
}
/*
这个函数同样重载了 std::ostream 的 << 操作符，但这次是为了输出 EPOLL_EVENTS 类型的值。
EPOLL_EVENTS 可能是一个结构体或类似枚举的位域，用于表示 epoll 事件的不同类型，如可读、可写、错误等。
*/
static std::ostream &operator<<(std::ostream &os, EPOLL_EVENTS events) {
    if (!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E)          \
    if (events & E) {  \
        if (!first) {  \
            os << "|"; \
        }              \
        os << #E;      \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event) {
    
    switch (event) {
    case IOManager::READ:
        return read_ctx;
    case IOManager::WRITE:
        return write_ctx;
    default:
        //SYLAR_ASSERT2(false, "getContext");
        assert(false);
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetEventContext(EventContext &ctx) {   //这个ctx不是read_ctx就是write_ctx
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

//触发三元组中的事件，event标定发生的事件类型，即事件已经发生，我们去执行其task
void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    
    // 待触发的事件必须已被注册过，这是一个位运算
    //SYLAR_ASSERT(events & event);
    assert(events & event);
    
    /**
     * 从events中清除该事件，表示不再关注该事件了
     * 也就是说，注册的IO事件是一次性的，如果想持续关注某个socket fd的读写事件，那么每次触发事件之后都要重新添加
     */
    events = (Event)(events & ~event);

    // ctx是该读写事件对应的上下文，读事件返回读事件上下文，写事件返回写事件上下文
    EventContext &ctx = getEventContext(event);
    //如果当时addevent指定了event发生时的cb
    if (ctx.cb) {
        //将任务push进ctx的调度器队列
        ctx.scheduler->schedule(ctx.cb);
    } 
    else {  //如果没指定cb，就将当时的协程重新resume
        ctx.scheduler->schedule(ctx.fiber);
    }

    //已经将任务push到调度器队列中，这里重置三元组中的对应事件上下文
    resetEventContext(ctx);
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
    : Scheduler(threads, use_caller, name) {
    /*
    epoll_create 函数用于创建一个新的 epoll 实例，这个实例会被用来后续通过 epoll_ctl 添加、修改或删除感兴趣的文件描述符（通常是套接字），
    并通过 epoll_wait 等待这些文件描述符上事件的发生
    */
    std::cout<<"iomanger ctor() begins"<<std::endl;
    m_epfd = epoll_create(5000);

    //SYLAR_ASSERT(m_epfd > 0);
    assert(m_epfd > 0);

    int rt = pipe(m_tickleFds);

    //SYLAR_ASSERT(!rt);
    assert(!rt);

    // 关注pipe读句柄的可读事件，用于tickle协程
    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    
    //边缘触发  为读端注册读事件
    // 注册pipe读句柄的可读事件，用于tickle调度协程，通过epoll_event.data.fd保存描述符
    event.events  = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];

    // 非阻塞方式，配合边缘触发         将管道读端设置为非阻塞
    //这里会调用hook系统调用
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);

    //SYLAR_ASSERT(!rt);
    assert(!rt);

    // 将管道的读描述符加入epoll多路复用，如果管道可读，idle中的epoll_wait会返回
    // 这里并没有使用addevent的方式添加事件，因为使用addevent的方式m_pendingevent就要++，最后导致stop函数不能正常退出
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);

    assert(!rt);
    //SYLAR_ASSERT(!rt);

    //三元组数组大小开到32
    contextResize(32);
    
    //启动scheduler调度器 将工作线程new出来
    start();
}

IOManager::~IOManager() {
    //停止scheduler调度器 等待所有工作线程执行结束
    stop();

    //调度器成功停止 调度协程以及所有子协程全部停止

    //调度器已经停止 所有工作线程已经退出 所有任务已经完成 关闭epollfd
    close(m_epfd);

    //关闭管道读端 写端
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    //将三元组数组清空 因为数组元素类型是原始指针
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
    std::cout<<"~iomanager() func end"<<std::endl;
}

void IOManager::contextResize(size_t size) {
    //使用resize改变大小，旧元素不变
    m_fdContexts.resize(size);

    //为新的元素初始化一下
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (!m_fdContexts[i]) {
            m_fdContexts[i]     = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

//为m_epfd这个epollfd添加监视事件 并且注册事件cb
//event表示要监视读事件还是写事件
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    // 找到fd对应的FdContext，如果不存在，那就分配一个
    //fdcontext是三元组结构体 也可以理解为客户结构体
    FdContext *fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);

    if ((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } 
    else {
        lock.unlock();
        //读锁释放 换成写锁
        RWMutexType::WriteLock lock2(m_mutex);
        //1.5倍扩容
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 同一个fd不允许重复添加相同的事件 也就是原来针对这个fd已经注册了读事件 现在又注册一遍
    //使用对应fdcontext的小锁
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    //该事件已经注册过
    if (SYLAR_UNLIKELY(fd_ctx->events & event)) {
        // SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
        //                           << " event=" << (EPOLL_EVENTS)event
        //                           << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        //人为assert失败
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有指针存储FdContext的位置
    /*
    如果 fd_ctx->events 的值为真（即非零），表示该文件描述符的事件已经存在于 epoll 实例中，此时应该使用 EPOLL_CTL_MOD 操作符来修改这些事件。这通常发生在需要更改文件描述符上监听的事件类型时。
    如果 fd_ctx->events 的值为假（即零），表示该文件描述符的事件尚未被注册到 epoll 实例中，此时应该使用 EPOLL_CTL_ADD 操作符来添加这些事件。
    */
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    
    //fd_ctx->events表示旧的该fd的监视事件 event表示新注册的读或者写事件
    epevent.events   = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);               //最关键的步骤
    if (rt) {
        // SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
        //                           << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
        //                           << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
        //                           << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    // 待执行IO事件数加1
    ++m_pendingEventCount;

    //更新fdctx的注册事件
    fd_ctx->events                     = (Event)(fd_ctx->events | event);
    

    //若event是读事件，就从三元组中返回读事件上下文 若event是写事件，就从三元组中返回写事件上下文
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);

    // 从三元组数组中找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    //因为是新注册(第一次)读事件或者写事件 该事件上下文中的字段都应该为空
    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体
    event_ctx.scheduler = Scheduler::GetThis();
    //如果addevent指定了event的处理函数
    if (cb) {
        event_ctx.cb.swap(cb);
    } 
    else {  //如果addevent没指定event的处理函数，那就将当前线程正在执行的协程绑定到event_ctx,相应的，当事件发生后，resume该fiber
        //拿到当前线程正在执行的协程 即工作协程 将其赋给eventctx对应协程
        event_ctx.fiber = Fiber::GetThis();
        SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::RUNNING, "state=" << event_ctx.fiber->getState());
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {

    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    //换小锁
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    //该事件没注册过
    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    // 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);       //最关键的步骤

    if (rt) {
        // SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
        //                           << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
        //                           << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 待执行事件数减1
    --m_pendingEventCount;

    // 重置该fd对应的event事件上下文 因为该event已经从注册事件中删除
    fd_ctx->events                     = new_events;        //更新三元组中该fd的注册事件
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);//拿到刚才删除事件的eventcontext
    fd_ctx->resetEventContext(event_ctx);//重载该eventcontext
    return true;
}

//注意：其与delevent的区别在于其会触发一次事件
bool IOManager::cancelEvent(int fd, Event event) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();
    //换小锁
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    // 删除事件
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    
    if (rt) {
        // SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
        //                           << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
        //                           << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 删除之前触发一次事件 具体怎么触发呢？就是将该event对应的eventctx里面的cb push进eventctx里面的调度器
    fd_ctx->triggerEvent(event);
    
    // 待执行事件数减1
    --m_pendingEventCount;
    return true;
}

bool IOManager::cancelAll(int fd) {

    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    //该fd注册的事件为空 直接返回
    if (!fd_ctx->events) {
        return false;
    }

    // 删除全部事件
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        // SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
        //                           << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
        //                           << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }
    std::cout<<"删除该fd之前,将其注册事件全部触发一遍"<<std::endl;
    // 触发全部已注册的事件 反正一共就俩事件
    if (fd_ctx->events & READ) {    //如果注册过读事件
        //触发，将读事件对应cb push进调度器队列
        fd_ctx->triggerEvent(READ);
        //待执行io事件数--
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager *IOManager::GetThis() {
    //基类指针转换为派生类指针
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

/**
 * 通知调度协程、也就是Scheduler::run()从idle中退出
 * Scheduler::run()每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle
 * 如果没有调度线程处理于idle状态，那也就没必要发通知了
 */
void IOManager::tickle() {
    // SYLAR_LOG_DEBUG(g_logger) << "tickle";
    std::cout<<"tickle:我要做通知了"<<std::endl;

    //当前没有空闲线程 只有在run中，进入idle fiber才会改变idlethreadcount，表示有线程空闲
    if(!hasIdleThreads()) {
        std::cout<<"当前没有空闲协程"<<std::endl;
        return;
    }
    std::cout<<"write"<<std::endl;
    //向写端写一个"T" 这就是做通知的具体行为
    int rt = write(m_tickleFds[1], "T", 1);
    SYLAR_ASSERT(rt == 1);
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

//判断该调度器是否可以停止
bool IOManager::stopping(uint64_t &timeout) {

    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    // 增加定时器功能后，还应该保证没有剩余的定时器待触发
    timeout = get_the_most_recent_Timer_time();

    //timeout为~0ull表示当前定时器列表为空 ~0ull是一个极大值
    //m_pendingEventCount表示还未发生的监视事件 也就是所有注册的监视事件全部已经发生了
    //Scheduler::stopping()判断任务队列是否为空 以及 是否工作线程数位0
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

/**
 * 我们在scheduler类中，如果调度器发现没有任务会调度idle协程
 * 调度器无调度任务时会阻塞idle协程上(阻塞具体行为就是一直epoll_wait)，
 * 而对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务push进任务队列之中，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；
 * 二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行IO事件对应的回调函数
 */
void IOManager::idle() {
    // SYLAR_LOG_DEBUG(g_logger) << "idle";

    // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理
    const uint64_t MAX_EVNETS = 256;
    epoll_event *events       = new epoll_event[MAX_EVNETS]();
    
    //shared_events 是一个shared ptr，其掌管整个events数组，并且定义了删除方法
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {
        delete[] ptr;
    });

    std::cout<<"iomanager:idle func:tag1"<<std::endl;
    while (true) {

        // 获取下一个定时器的超时时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;

        //next_timeout是一个传出参数
        if( SYLAR_UNLIKELY(stopping(next_timeout))) {
            //能进来说明iomanager已经可以停止了
            // SYLAR_LOG_DEBUG(g_logger) << "name=" << getName() << "idle stopping exit";
            std::cout<<"break 1"<<std::endl;
            break;
        }
        std::cout<<"tag2"<<std::endl;
        // 阻塞在epoll_wait上，等待注册事件发生或定时器超时
        int rt = 0;
        do{
            // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时，避免定时器超时时间太大时，epoll_wait一直阻塞
            //MAX_TIMEOUT表示预计最多等待的的时间，过了这个时间，要么会有预期事件发生，要么epollwait超时
            static const int MAX_TIMEOUT = 5000;

            //若next_timeout不是非法值
            if(next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } 
            else {
                next_timeout = MAX_TIMEOUT;
            }

            //返回值大于0 表示有多少个监视事件发生 并将这些事件存到events数组
            std::cout<<"tag3"<<std::endl;
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);

            std::cout<<"rt = "<<rt<<std::endl;
            
            //系统调用被中断 比如ctrl c 那就继续等下一轮epoll_wait
            if(rt < 0 && errno == EINTR) {
                continue;
            } 
            else {          //成功的等到了注册事件(返回发生事件数量)或者超时(返回0) 跳出无限等待的epoll wait
                std::cout<<"break 2"<<std::endl;
                break;
            }
        } while(true);
        //此时是epoll_wait超时，但是不知道是否有定时器到期，所以需要我们主动检查

        // 收集所有已超时的定时器的回调函数，一个个执行回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        
        std::cout<<"检测出来到期定时器共有: "<<cbs.size()<<std::endl;
        if(!cbs.empty()) {
            for(const auto &cb : cbs) {
                //一个一个将定时器的执行函数push进调度器任务队列
                schedule(cb);
            }
            cbs.clear();
        }
        
        // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        // rt是发生的事件数量 这里也是epoll相比poll与select的高效之处
        for (int i = 0; i < rt; ++i) {
            std::cout<<"tag4"<<std::endl;
            epoll_event &event = events[i];
            std::cout<<"发生事件的fd是:"<<event.data.fd<<std::endl;
            
            //这个事件是tickle()触发的 因为tickle会写一个T到管道写端 表明现在有空闲任务需要调度器开始调度工作线程取执行该任务
            if (event.data.fd == m_tickleFds[0]) {
                std::cout<<"读管道事件"<<std::endl;
                // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可
                uint8_t dummy[256];
                //因为管道读端是边缘触发 所以要用while读完
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
                    ;
                continue;
            }

            // 通过epoll_event的私有指针获取FdContext，也就是指向三元组的指针，该三元组包含了客户相关信息
            FdContext *fd_ctx = (FdContext *)event.data.ptr;

            std::cout<<"fd_ctx关联的句柄是:"<<fd_ctx->fd<<std::endl;
            
            //这是一把客户级别的锁
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            /**
                EPOLLERR
                EPOLLERR是一个epoll事件掩码，它用于指示一个文件描述符上的I/O操作发生了错误。
                当epoll_wait返回时，如果事件掩码中包含EPOLLERR，那么这通常意味着与该文件描述符相关的某种错误已经发生。
                错误的类型可能包括但不限于：
                1. 文件描述符对应的设备或资源不再可用。
                2. I/O操作中出现了某种硬件或软件错误。
                3. 文件描述符上的读或写操作违反了某些限制，比如尝试写入一个只读文件。
                4. 发生了信号中断，导致I/O操作被中止。
                在epoll事件处理函数中，如果你发现事件掩码中有EPOLLERR，应该检查该文件描述符的状态，可能需要通过read(), write(), 或recv(), send()等系统调用来确定具体错误，或者使用getsockopt()来获取套接字的错误状态。

                EPOLLHUP
                EPOLLHUP也是一个epoll事件掩码，它表示一个文件描述符已经被挂起（hang up）。
                在套接字的上下文中，这通常意味着远程主机已经关闭了连接，或者本地主机主动关闭了套接字的写端。EPOLLHUP可能在以下情况发生：
                远程主机关闭了连接，发送了TCP FIN包。
                本地主机关闭了套接字的写端，即调用了shutdown(sockfd, SHUT_WR)。
                网络连接中断，导致套接字变得无效。
                当epoll_wait返回一个事件掩码中包含EPOLLHUP时，这表明文件描述符可能不再适合进一步的I/O操作，特别是写操作。在处理EPOLLHUP事件时，你可能需要检查套接字的状态，决定是否关闭该套接字，或者进行必要的清理工作。
             *  这两个事件都是内核触发的
             */ 
            // 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                if(event.events & EPOLLERR) {           //我们的test_io中connect失败，会触发这个错误
                    std::cout<<"tag 7"<<std::endl;
                }
                else if(event.events & EPOLLHUP) {
                    std::cout<<"tag 8"<<std::endl;
                }
                //在三元组结构体中重新同时注册读和写事件
                /*
                确保错误和挂起事件的全面处理：
                当事件包含 EPOLLERR 或 EPOLLHUP 标志时，这通常表示文件描述符遇到了某种错误（如读端关闭的 pipe 或套接字对端关闭）。
                在这种情况下，仅处理错误或挂起事件可能不足以触发所有必要的后续操作，例如清理或重置连接。
                因此，这里通过将 EPOLLIN 和 EPOLLOUT 添加到 event.events 中，
                确保了即便在错误状态下，也会尝试触发读和写事件的处理逻辑，这可以避免某些情况下事件永久未被处理的问题。
                */
                //fd_ctx->events是旧的注册事件
                //按位与操作的结果是一个新的值，这个值只包含那些在fd_ctx->events中也被激活的事件标志。换句话说，如果fd_ctx->events中没有包含EPOLLIN或EPOLLOUT，那么相应的位将被清零。
                //这意味着event.events将包含所有原本就存在于event.events中的事件标志，以及那些在fd_ctx->events中也激活的EPOLLIN和EPOLLOUT事件标志。
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }

            //实际事件
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            //当前FdContext对象中注册的事件(fd_ctx->events)与实际发生的事件(real_events)之间是否有交集
            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 从三元组中剔除已经发生的事件，将剩下的事件重新加入epoll_wait
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;

            //仍然是边缘触发
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            //如果操作失败
            if (rt2) {
                // SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                //                           << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                //                           << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            // 处理实际发生的事件，也就是让调度器调度指定的函数或协程 注意下面两个事件不是if else关系
            if (real_events & READ) {
                std::cout<<"tag5"<<std::endl;
                fd_ctx->triggerEvent(READ);

                //等待执行的事件数量--
                --m_pendingEventCount;
            }
            if (real_events & WRITE) {
                std::cout<<"tag6"<<std::endl;
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        /**
         * 一旦处理完所有的事件，idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有新任务要调度
         * 上面triggerEvent实际也只是把对应的fiber重新加入调度，要执行的话还要等idle协程退出
         */ 
        //这个cur就是当前的idle协程
        Fiber::ptr cur = Fiber::GetThis();
        //拿到原始指针
        auto raw_ptr   = cur.get();
        
        //引用计数--
        cur.reset();
        std::cout<<"idle 协程 yield"<<std::endl;
        raw_ptr->yield();
    } // end while(true)
    std::cout<<"idle func exit"<<std::endl;
}

void IOManager::onTimerInsertedAtFront() {
    std::cout<<"qxu"<<std::endl;
    tickle();
}

} // end namespace sylar