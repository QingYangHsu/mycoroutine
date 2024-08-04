#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include <unistd.h>

#define PORT 9190

// 回声处理函数
void echo_read_cb(evutil_socket_t clientfd, short events, void *arg)
{
    char buf[1024];
    int len;
    len = recv(clientfd, buf, sizeof(buf) - 1, 0);
    if (len <= 0)
    {
        // 发⽣错误或连接关闭，关闭连接并释放事件资源
        close(clientfd);

        //释放参数的资源
        event_free((struct event *)arg);
        return;
    }
    buf[len] = '\0';
    printf("接收到消息：%s\n", buf);
    
    // echo
    send(clientfd, buf, len, 0);
}

// 接受连接回调函数
void accept_conn_cb(evutil_socket_t listener, short event, void *arg)
{
    //拿到reactor实例
    struct event_base *base = (struct event_base *)arg;
    
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr *)&ss, &slen);
    if (fd < 0)
    {
        perror("accept");
    }
    else if (fd > FD_SETSIZE)
    {
        close(fd);
    }
    else
    {
        // 创建⼀个新的事件处理器
        struct event *ev = event_new(NULL, -1, 0, NULL, NULL);

        // 对clientfd添加读事件
        event_assign(ev, base, fd, EV_READ | EV_PERSIST, echo_read_cb, (void *)ev);

        // 将事件处理器添加到注册事件队列之中，并将事件处理器对应的事件添加到事件多路分发器之中
        event_add(ev, NULL);
    }
}

int main()
{
    struct event_base *base;
    struct event *listener;
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(PORT);

    // 创建一个event base对象，一个event base对象就是一个reactor实例
    base = event_base_new();

    // 创建⼀个事件处理器，并设置其从属的reactor实例，第二个参数为-1表示创建的是定时事件处理器
    listener = event_new(base, -1, EV_READ | EV_PERSIST, accept_conn_cb, (void *)base);
    
    // 将事件处理器添加到注册事件队列之中，并将事件处理器对应的事件添加到事件多路分发器之中
    if (event_add(listener, NULL) == -1)
    {
        perror("event_add");
        return -1;
    }

    // 执行事件循环
    event_base_dispatch(base);
    return 0;
}

//使用libevent网络库 该库的介绍可参考linux高性能服务器编程
// g++ test3.cc -o test3 -std=c++11 -levent
// qps:1066.1
// 但是会有tls握手失败的问题
// ab -n 100 -c 10 https://127.0.0.1:9190/