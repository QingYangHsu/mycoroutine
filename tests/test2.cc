#include "../libco/co_routine.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <stack>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <iostream>


#ifdef __FreeBSD__
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#endif


using namespace std;

//任务类
struct task_t
{
    stCoRoutine_t *co;          //任务协程
    int fd;                     //指定进程号
};

static stack<task_t *> g_readwrite;
static int serverfd = -1;

static int SetNonBlock(int iSock)
{
    int iFlags;
    iFlags = fcntl(iSock, F_GETFL, 0);
    iFlags |= O_NONBLOCK;
    iFlags |= O_NDELAY;
    int ret = fcntl(iSock, F_SETFL, iFlags);
    return ret;
}

static void *readwrite_routine(void *arg)
{
    std::cout<<"readwrite_routine func() beign"<<std::endl;
    
    //该协程启用hook
    co_enable_hook_sys();
    task_t *co = (task_t *)arg;
    char buf[1024 * 16];

    for (;;)
    {
        if (-1 == co->fd)
        {
            // push进去
            g_readwrite.push(co);
            
            // 切出
            co_yield_ct();
            continue;
        }
        int fd = co->fd;
        co->fd = -1;
        for (;;)
        {
            // 将该fd的可读事件，注册到epoll中
            // co_accept⾥⾯libco并没有将其设置为 O_NONBLOCK
            // 是⽤户主动设置的 O_NONBLOCK
            // 所以read函数不⾛hook逻辑，需要⾃⾏进⾏poll切出
            struct pollfd pf = {0};
            pf.fd = fd;
            pf.events = (POLLIN | POLLERR | POLLHUP);

            co_poll(co_get_epoll_ct(), &pf, 1, 1000);
            // 当超时或者可读事件到达时，进⾏read。所以read不⼀定成功，有可能是超时造成的
            int ret = read(fd, buf, sizeof(buf));
            // 读多少就写多少
            if (ret > 0)
            {
                ret = write(fd, buf, ret);
            }
            if (ret <= 0)
            {
                // accept_routine->SetNonBlock(fd) cause EAGAIN, we should continue
                if (errno == EAGAIN)
                    continue;
                close(fd);
                break;
            }
        }
    }
    std::cout<<"readwrite_routine func() end"<<std::endl;
    return 0;
}

int co_accept(int fd, struct sockaddr *addr, socklen_t *len);

static void *accept_routine(void *)
{
    co_enable_hook_sys();
    printf("accept_routine\n");
    fflush(stdout);
    for (;;)
        {
            // printf("pid %ld g_readwrite.size %ld\n",getpid(),g_readwrite.size());
            if (g_readwrite.empty())
            {
                printf("empty\n"); // sleep
                struct pollfd pf = {0};
                pf.fd = -1;
                // sleep 1秒，等待有空余的协程
                poll(&pf, 1, 1000);
                continue;
            }
            struct sockaddr_in addr; // maybe sockaddr_un;
            memset(&addr, 0, sizeof(addr));
            socklen_t len = sizeof(addr);
            // accept
            int fd = co_accept(serverfd, (struct sockaddr *)&addr, &len);
            if (fd < 0)
            {
                // 意思是，如果accept失败了，没办法，暂时切出去
                struct pollfd pf = {0};
                pf.fd = serverfd;
                pf.events = (POLLIN | POLLERR | POLLHUP);
                co_poll(co_get_epoll_ct(), &pf, 1, 1000);
                continue;
            }
            if (g_readwrite.empty())
            {
                close(fd);
                continue;
            }
            // 这⾥需要⼿动将其变成⾮阻塞的
            SetNonBlock(fd);
            task_t *co = g_readwrite.top();
            co->fd = fd;
            g_readwrite.pop();
            co_resume(co->co);
        }
    return 0;
}


static void SetAddr(const char *pszIP, const unsigned short shPort, struct sockaddr_in &addr)
{
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(shPort);
    int nIP = 0;
    if (!pszIP || '\0' == *pszIP || 0 == strcmp(pszIP, "0") || 0 == strcmp(pszIP, "0.0.0.0") || 0 == strcmp(pszIP, "*"))
    {
        nIP = htonl(INADDR_ANY);
    }
    else
    {
        nIP = inet_addr(pszIP);
    }
    addr.sin_addr.s_addr = nIP;
}

/*
参数 bool bReuse 用于控制是否允许套接字地址的重用。在网络编程中，当一个TCP套接字被关闭后，
其地址和端口号通常会保持一段时间（TIME_WAIT状态），以防止新的套接字立即绑定到相同的地址和端口上，
从而避免由于延迟到达的数据包导致的问题。
当 bReuse 设置为 true 时，它告诉操作系统允许套接字地址的重用。
这通过调用 setsockopt 函数并设置 SO_REUSEADDR 选项来实现。一旦设置了 SO_REUSEADDR，
即使端口仍然处于TIME_WAIT状态，也可以在该端口上创建新的套接字。
这对于某些需要快速重启服务器或监听多个服务在同一端口上的情况非常有用。
*/
static int CreateTcpSocket(const unsigned short shPort /* = 0 */, const char *pszIP /* =
 "*" */, bool bReuse /* = false */)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd >= 0)
    {
        if (shPort != 0)
        {
            if (bReuse)
            {
                int nReuseAddr = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
            }
            struct sockaddr_in addr;
            SetAddr(pszIP, shPort, addr);
            int ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
            if (ret != 0)
            {
                close(fd);
                return -1;
            }
        }
    }
    return fd;
}


int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        printf("Usage:\n""example_echosvr [IP] [PORT] [TASK_COUNT] [PROCESS_COUNT]\n"
        "example_echosvr [IP] [PORT] [FIBER_TASK_COUNT] [PROCESS_COUNT] -d #daemonize mode\n");
        return -1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int cnt = atoi(argv[3]);     // task_count 协程数

    int process_count = atoi(argv[4]); // 进程数
    
    bool deamonize = argc >= 6 && strcmp(argv[5], "-d") == 0;   //是否启用后台进程

    serverfd = CreateTcpSocket(port, ip, true);

    listen(serverfd, 1024);     //等待1024个客户
    
    if (serverfd == -1)
    {
        printf("Port %d is in use\n", port);
        return -1;
    }
    
    printf("listen %d %s:%d\n", serverfd, ip, port);
    SetNonBlock(serverfd);
    
    for (int k = 0; k < process_count; k++)
    {
        pid_t pid = fork();
        if (pid > 0)
        {
            continue;
        }
        else if (pid < 0)
        {
            break;
        }
        for (int i = 0; i < cnt; i++)
        {
            //分配一个task_t对象的空间，并初始化为0
            task_t *task = (task_t *)calloc(1, sizeof(task_t));
            task->fd = -1;

            // 创建⼀个协程 指定协程cb 这个协程负责io
            co_create(&(task->co), NULL, readwrite_routine, task);
            
            // 启动协程
            co_resume(task->co);
        }

        // 启动listen协程
        stCoRoutine_t *accept_co = NULL;
        co_create(&accept_co, NULL, accept_routine, 0);
        // 启动协程
        co_resume(accept_co);
        // 启动事件循环
        co_eventloop(co_get_epoll_ct(), 0, 0);
        exit(0);
    }

    if (!deamonize)
        wait(NULL);
    return 0;
}

//使用微信libco库
// g++ test2.cc ../libco/co_routine.cpp  ../libco/co_epoll.cpp ../libco/*.S ../libco/coctx.cpp ../libco/co_hook_sys_call.cpp ../libco/co_comm.cpp -o test2 -std=c++11 -lpthread -ldl
// qps:2923.98
// ab -n 100 -c 10 https://127.0.0.1:9190/
//./test2 127.0.0.1 9190 1 1