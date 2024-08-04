#include "hook.h"
#include <dlfcn.h>

//#include "config.h"
// #include "log.h"
#include "fiber.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "macro.h"          //使用一些分支预测宏

// sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
namespace sylar {

// static sylar::ConfigVar<int>::ptr g_tcp_connect_timeout =
//     sylar::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");
const int g_tcp_connect_timeout = 5000;

//首先定义线程局部变量t_hook_enable，用于表示当前线程是否启用hook，
//使用线程局部变量表示hook模块是线程粒度的，各个线程可单独启用或关闭hook。
static thread_local bool t_hook_enable = false;

//然后是获取各个被hook的接口的原始地址， 这里要借助dlsym来获取。sylar使用了一套宏来简化编码
#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)




//hook_init() 放在一个静态对象的构造函数中调用，
//这表示在main函数运行之前就会获取各个符号的地址并保存在全局变量中。
void hook_init() {
    static bool is_inited = false;          //即是否已经调用了本init函数，这是一个局部变量，有全局生命期
    if(is_inited) {
        return;
    }
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX


//类似于sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep");
//即拿到原本动态库函数调用代码段地址后，再强转成函数指针类型
}

//默认的connect超时时间 其在init中赋值5000
static uint64_t s_connect_timeout = -1;

struct _HookIniter {
    //构造函数
    _HookIniter() {
        hook_init();
        //s_connect_timeout = g_tcp_connect_timeout->getValue();
        s_connect_timeout = g_tcp_connect_timeout;
        /*
        向 g_tcp_connect_timeout 对象注册了一个监听器（或称为回调函数）。这个监听器是一个 lambda 表达式，它会在 TCP 连接超时时间发生变化时被调用。监听器内部执行了两个操作：
        使用日志记录器（g_logger）记录超时时间的变化情况，包括旧值和新值。这里使用了 SYLAR_LOG_INFO 宏（或函数），它可能是一个自定义的日志记录宏，用于输出信息级别的日志。
        更新 s_connect_timeout 的值为新的超时时间值。
        */
        // g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
        //         // SYLAR_LOG_INFO(g_logger) << "tcp connect timeout changed from "
        //         //                          << old_value << " timeout " << new_value;
        //         s_connect_timeout = new_value;
        // });
    }
};

//全局的静态对象
static _HookIniter s_hook_initer;

//本线程是否hook
bool is_hook_enable() {
    return t_hook_enable;
}

void set_hook_enable(bool flag) {
    std::cout<<"本线程开启hook"<<std::endl;
    t_hook_enable = flag;
}

}

//定时器信息 标定该定时器是否被删除 并且存错误原因比如 ETIMEDOUT	110	/* Connection timed out */
struct timer_info {
    int cancelled = 0;
};


//下面read write send一堆函数的共用底层函数 
//OriginFun为原始调用的函数指针 hook_fun_name为系统调用名称
//event表示iomanager支持的监视的事件名称 无非就是读事件或者写事件
//timeout_so用来标定是 接收时间(SO_RCVTIMEO) 还是发送时间(SO_SENDTIMEO)
//args是原始参数 这里使用了万能引用
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
        uint32_t event, int timeout_so, Args&&... args) {
    if(!sylar::t_hook_enable) {
        return fun(fd, std::forward<Args>(args)...);
    }

    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx) {
        return fun(fd, std::forward<Args>(args)...);        //因为形参用万能引用接，这里使用完美转发
    }

    if(ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket() || ctx->getUserNonblock()) {
        std::cout<<"该fd不是socket，或者用户已经设置了非阻塞"<<std::endl;
        return fun(fd, std::forward<Args>(args)...);
    }

    //拿到fdctx中设置的读写超时时间
    uint64_t timeout = ctx->getTimeout(timeout_so);
    
    //new一个定时器信息类 用来标定定时器是否删除(初始默认未删除)以及返回错误原因
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    //Interrupted system call
    while(n == -1 && errno == EINTR) {      //如果失败，并且错误类型为中断错误，无限重试 直到成功 或者 错误为EAGAIN
        n = fun(fd, std::forward<Args>(args)...);
    }

    if(n == -1 && errno == EAGAIN) {        //try again 资源暂时不可用 这通常发生在非阻塞操作中，当系统资源（如文件描述符、缓冲区、消息队列等）暂时无法满足请求时，
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        sylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        if(timeout != (uint64_t)-1) {
            timer = iom->addConditionTimer(timeout, [winfo, fd, iom, event]() {
                std::cout<<"已经超时，系统调用还没调用成功"<<std::endl;
                auto t = winfo.lock();
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                //fd中不再注册这个事件，从epollfd删除之前针对该fd会触发一次event，
                //让该fiber在yield之后继续往下执行，让本函数走完，表明到了connect约定的超时时间，并没有成功连接到server
                iom->cancelEvent(fd, (sylar::IOManager::Event)(event));
            }, winfo);
        }

        //针对该fd添加监视事件
        int rt = iom->addEvent(fd, (sylar::IOManager::Event)(event));   
        if(SYLAR_UNLIKELY(rt)) {        //失败
            // SYLAR_LOG_ERROR(g_logger) << hook_fun_name << " addEvent("
            //     << fd << ", " << event << ")";
            if(timer) {
                timer->cancel();    //删除定时器
            }
            return -1;
        } 
        else {
            //这里是异步的关键 即比如send调用，如果写缓冲区并未准备好，会阻塞，这里先让其yield，
            //当fdctx设置的sendtimeout定时器到期后，或者监视事件发生，再重新resume
            sylar::Fiber::GetThis()->yield();
            
            if(timer) {     //删除定时器 无论其是否触发
                timer->cancel();
            }
            if(tinfo->cancelled) {      //超时定时器中设置的错误原因
                errno = tinfo->cancelled;
                return -1;
            }
            std::cout<<"本轮没成功，再来一轮"<<std::endl;
            goto retry;         //无限重试 什么时候跳出这个循环？即fun成功，即原始系统调用成功
        }
    }
    //应该只能为0
    std::cout<<"我猜这里返回值只能为0,"<<n<<std::endl;
    return n;
}


extern "C" {
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

//下面是各个接口的hook实现‘=
//具体表现就是注册一个指定时间的定时器，定时器回调函数为将yield的fiber重新schedule到iom，然后当前这个正在运行的fiber yield，
//为的就是不浪费这段睡眠时间，让调度器能将在这段睡眠时间阻塞的cpu调度到一个有用的协程上去,让用户看上去是异步的效果
unsigned int sleep(unsigned int seconds) {
    std::cout<<"hook:sleep func() begin"<<std::endl;

    //如果本线程不hook，那就直接调原始调用
    if(!sylar::t_hook_enable) {
        std::cout<<"hook:sleep func() end1"<<std::endl;
        return sleep_f(seconds);
    }

    //拿到当前线程正在执行的协程
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    
    //拿到当前的iomanager
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    
    //先添加定时器
    //将 sylar::IOManager::schedule 成员函数的指针类型转换为 sylar::Scheduler 的成员函数指针类型
    //sylar::IOManager::schedule的返回值为void 参数为FiberOrCb与int，现在将这个函数指针拿到，并将其强转为(void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))这个类型
    //现在指针强转完成，开始绑定参数，iom作为this参数，fiber为第二个参数，-1表示不指定线程
    iom->addTimer(seconds * 1000, std::bind( (void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    
    //再yield 这里是异步的关键 也是同步，阻塞的系统调用体现出异步的关键
    std::cout<<"hook:sleep fiber yield"<<std::endl;

    sylar::Fiber::GetThis()->yield();
    
    std::cout<<"hook:sleep func() end2"<<std::endl;
    return 0;
}

int usleep(useconds_t usec) {
    if(!sylar::t_hook_enable) {
        return usleep_f(usec);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();

    iom->addTimer(usec / 1000, std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if(!sylar::t_hook_enable) {
        return nanosleep_f(req, rem);
    }

    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 /1000;
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(timeout_ms, std::bind((void(sylar::Scheduler::*)
            (sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

//并没有做什么 只是在原有socket基础上创建了一个fdctx，便于后续accept bind connect等一系列行为的管理
int socket(int domain, int type, int protocol) {        
    std::cout<<"socket func() tag1"<<std::endl;
    if(!sylar::t_hook_enable) {
        std::cout<<"socket func() tag2"<<std::endl;
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if(fd == -1) {
        return fd;
    }

    std::cout<<"socket func() tag3"<<std::endl;

    //需要在拿到fd后将其添加到FdManager中，并且允许其创建一个fdctx
    sylar::FdMgr::GetInstance()->get(fd, true);
    return fd;
}

int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if(!sylar::t_hook_enable) {
        std::cout<<"connect_with_time_out func() tag1"<<std::endl;
        return connect_f(fd, addr, addrlen);
    }

    //从fdmanager中拿到fd对应的fdctx
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    
    //如果该fdctx已经关闭
    if(!ctx || ctx->isClose()) {
        //bad file number
        errno = EBADF;
        std::cout<<"connect_with_time_out func() tag2"<<std::endl;
        return -1;
    }

    //判断传入的fd是否为套接字，如果不为套接字，则调用系统的connect函数并返回。
    if(!ctx->isSocket()) {
        std::cout<<"connect_with_time_out func() tag3"<<std::endl;
        return connect_f(fd, addr, addrlen);
    }

    //判断fd是否被用户设置为了非阻塞模式，如果是则调用系统的connect函数并返回。因为已经达到了异步的目的
    if(ctx->getUserNonblock()) {
        std::cout<<"connect_with_time_out func() tag4"<<std::endl;
        return connect_f(fd, addr, addrlen);
    }

    //在fdctx的init中已经设置了hook非阻塞，也就是系统非阻塞

    //调用系统的connect函数，由于套接字是非阻塞的，这里会直接返回EINPROGRESS错误
    //返回值要么是0 要么是-1 并且errno为EINPROGRESS
    int n = connect_f(fd, addr, addrlen);
    if(n == 0) {        //直接成功
        std::cout<<"connect_with_time_out func() tag5"<<std::endl;
        return 0;
    } 
    else if(n != -1 || errno != EINPROGRESS) {      //EINPROGRESS 通常与非阻塞套接字相关，表示连接操作尚未完成，但已经开始
        std::cout<<"impossible"<<std::endl;
        
        //理论上这个分支不会进去
        return n;
    }

    //如果超时参数有效，则添加一个条件定时器，在定时时间到后通过t->cancelled设置超时标志并触发一次WRITE事件。
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    sylar::Timer::ptr timer;

    //定时器信息类的shared_ptr
    std::shared_ptr<timer_info> tinfo(new timer_info);
    
    //定时器信息类的weak_ptr 也就是条件
    std::weak_ptr<timer_info> winfo(tinfo);

    //如果形参传进来的超时时间不是无限等待 就add一个对应时间的条件timer
    if(timeout_ms != (uint64_t)-1) {
        //向iom中add一个条件timer 条件就是winfo
        //什么是条件定时器？就是时间到后，先判断一个条件，考虑是否触发其callback
        timer = iom->addConditionTimer(timeout_ms, 
        [winfo, fd, iom]() {
                std::cout<<"connect_with_time_out func() tag6"<<std::endl;
                //拿到shared_ptr
                auto t = winfo.lock();

                //如果shared_ptr空 或者定时器已经删除
                if(!t || t->cancelled) {
                    return;
                }

                //Connection timed out
                t->cancelled = ETIMEDOUT;

                //fd中不再注册这个事件，从epollfd删除之前针对该fd会触发一次write，让该fiber在yield之后继续往下执行，让本函数走完，
                //表明到了connect约定的超时时间，并没有成功连接到server
                iom->cancelEvent(fd, sylar::IOManager::WRITE);
        }, winfo);
    }

    //针对该clientfd添加WRITE事件(表明用户连接server成功 下一步可以写数据到server了) 
    //并将当前协程绑定到write对应eventctx，具体行为是当connect成功时继续本fiber
    int rt = iom->addEvent(fd, sylar::IOManager::WRITE);

    //事件添加注视成功
    if(rt == 0) {
        std::cout<<"connect_with_time_out func() tag7"<<std::endl;
        //yield         这里是异步的关键
        sylar::Fiber::GetThis()->yield();
        
        //又恢复执行    两种情况：1.表明client成功连接到server，fiber恢复执行 或者发生错误，clientfd也会可写  2.超时，最后cancleevent又触发了一次事件，fiber恢复执行
        if(timer) {//情况1情况2
            std::cout<<"connect_with_time_out func() tag8"<<std::endl;
            //删除定时器 因为已经不需要超时时间了
            timer->cancel();
        }
        if(tinfo->cancelled) {      //情况2：errno设置为超市定时器cb中设置的原因，并返回-1表示失败
            std::cout<<"connect_with_time_out func() tag9"<<std::endl;
            errno = tinfo->cancelled;
            return -1;
        }
    } 
    else {  //对clientfd添加写事件监视失败
        if(timer) {
            timer->cancel();    //删除定时器
        }
        std::cout<<"connect_with_time_out func() tag10"<<std::endl;
        //SYLAR_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    //该变量用来临时查一下错误标志
    int error = 0;
    
    //指定error变量的大小，因为getsockopt需要知道缓冲区的大小。这里使用sizeof(int)是因为SO_ERROR选项的返回值通常是一个整数。
    socklen_t len = sizeof(int);

    //尝试从套接字fd获取错误状态。SO_ERROR是具体要查询的选项，它返回最近一次错误的代码（如果没有错误，则为0）。
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
        std::cout<<"connect_with_time_out func() tag11"<<std::endl;
        return -1;
    }

    if(!error) {
        std::cout<<"connect_with_time_out func() tag12"<<std::endl;
        return 0;
    } 
    else {
        std::cout<<"connect_with_time_out func() tag13"<<std::endl;
        //errno是一个全局错误标志
        errno = error;
        return -1;
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    //s_connect_timeout默认-1 即阻塞
    return connect_with_timeout(sockfd, addr, addrlen, sylar::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int clientfd = do_io(s, accept_f, "accept", sylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);

    if(clientfd >= 0) {
        //把客户的fdctx搞出来
        sylar::FdMgr::GetInstance()->get(clientfd, true);
    }
    return clientfd;
}

ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_f, "read", sylar::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", sylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {//send = write
    std::cout<<"send func()"<<std::endl;
    return do_io(s, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_f, "sendmsg", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}

//关闭某fd
int close(int fd) {
    if(!sylar::t_hook_enable) {
        return close_f(fd);
    }

    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(ctx) {
        auto iom = sylar::IOManager::GetThis();
        if(iom) {
            //取消fd注册的所有事件 无非就是读或者写事件 并且删除之前会全部触发一遍
            iom->cancelAll(fd);
        }
        //从fdmanager删除该fd对应的fdctx 注意：不要将这里的fdctx与上面的fdcontext混淆
        sylar::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);
}

//eg:rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK)
int fcntl(int fd, int cmd, ... /* arg */ ) {
    std::cout<<"fcntl func() begin "<<std::endl;
    va_list va;
    va_start(va, cmd);
    switch(cmd) {
        case F_SETFL:
            {
                std::cout<<"fcntl f_setfl "<<std::endl;
                int arg = va_arg(va, int);
                va_end(va);

                //如果是第一次get 会创建一个对应fdctx
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()) {
                    std::cout<<"不是socket"<<std::endl;
                    return fcntl_f(fd, cmd, arg);
                }

                // 更新文件描述符上下文中的用户级非阻塞标志  arg是传入的参数 表明用户希望设置的状态
                // 这里使用arg与O_NONBLOCK进行位与操作，以检查用户是否请求了非阻塞模式  
                if(arg & O_NONBLOCK) {
                    std::cout<<"用户希望设置socket非阻塞"<<std::endl;
                }
                ctx->setUserNonblock(arg & O_NONBLOCK);     

                // 接下来，根据上下文中的系统级非阻塞标志  
                // 来调整最终要传递给fcntl_f的参数。  
                // 如果系统级非阻塞标志被设置，则将O_NONBLOCK标志添加到arg中。  
                // 否则，从arg中清除O_NONBLOCK标志。  
                if(ctx->getSysNonblock()) {                 
                    arg |= O_NONBLOCK;
                } else {
                    arg &= ~O_NONBLOCK;
                }

                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETFL:
            {
                std::cout<<"fcntl f_getfl "<<std::endl;
                va_end(va);
                int arg = fcntl_f(fd, cmd);
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()) {
                    return arg;
                }

                if(ctx->getUserNonblock()) {
                    return arg | O_NONBLOCK;
                } 
                else {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
            break;
        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }
}

int ioctl(int d, unsigned long int request, ...) {
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if(FIONBIO == request) {
        bool user_nonblock = !!*(int*)arg;
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(d);
        if(!ctx || ctx->isClose() || !ctx->isSocket()) {
            return ioctl_f(d, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(d, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {       //cnm 脱裤子放屁
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    if(!sylar::t_hook_enable) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    if(level == SOL_SOCKET) {       //如果level设置的是socket相关
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {      //如果要设置的是发送时间或者接收时间
            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx) {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}


/*
setsockopt()函数是一个用于设置套接字（socket）选项的系统调用，它允许程序员配置套接字的各种参数和属性，以满足特定的网络编程需求。该函数在网络编程中广泛使用，可以在套接字层面上调整其行为。
基本用法
setsockopt()函数的一般用法如下：
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
sockfd：要操作的套接字文件描述符。
level：选项所在的协议层级别，对于套接字选项来说，通常设置为SOL_SOCKET。但也有一些选项可能位于其他协议层，如IPPROTO_TCP或IPPROTO_IP。
optname：要设置的选项名称。
optval：指向包含新选项值的缓冲区的指针。
optlen：optval缓冲区的大小，以字节为单位。
常见选项
setsockopt()函数支持多种选项，以下是一些常见的选项及其作用：
SO_REUSEADDR：允许套接字绑定到一个已在使用中的地址上（通常用于TCP套接字）。这对于服务器程序来说非常有用，因为它们通常需要绑定到一个特定的端口上，而这个端口可能在之前的运行中还未被完全释放。
SO_REUSEPORT：与SO_REUSEADDR类似，但允许不同的套接字绑定到相同的地址和端口上，并且每个套接字可以接收到达该地址和端口的数据包的一部分。这通常用于提高服务器程序的性能和可扩展性。
SO_SNDBUF和SO_RCVBUF：分别设置套接字的发送和接收缓冲区大小。这些选项对于控制网络数据传输的流量和延迟非常有用。
TCP_NODELAY：禁用TCP的Nagle算法。Nagle算法通过合并小的数据包来减少网络拥塞，但在某些实时性要求较高的应用中，这可能会导致不必要的延迟。通过设置TCP_NODELAY选项，可以关闭Nagle算法，以减少数据传输的延迟。
SO_BROADCAST：允许套接字发送广播消息。默认情况下，广播消息是被禁止的，因为它们可能会干扰网络上的其他设备。通过设置SO_BROADCAST选项，可以允许套接字发送广播消息。
*/