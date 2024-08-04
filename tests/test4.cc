#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <iostream>

#define MAX_EVENTS 10

#define PORT 9190

int main()
{
    int serverfd, clientfd, epoll_fd, event_count;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct epoll_event events[MAX_EVENTS], event;

    // 创建监听套接字
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        return -1;
    }

    // 设置服务器地址和端⼝
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定监听套接字到服务器地址和端⼝
    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        return -1;
    }

    // 监听连接
    if (listen(serverfd, 5) == -1)
    {
        perror("listen");
        return -1;
    }

    // 创建 epoll 实例
    if ((epoll_fd = epoll_create1(0)) == -1)
    {
        perror("epoll_create1");
        return -1;
    }

    // 添加监听serverf的读事件到 epoll 实例中
    event.events = EPOLLIN;
    event.data.fd = serverfd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serverfd, &event) == -1)
    {
        perror("epoll_ctl");
        return -1;
    }

    while (1)
    {
        // 等待事件发⽣ 无限阻塞等待
        event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (event_count == -1)
        {
            perror("epoll_wait");
            return -1;
        }

        // 处理事件
        for (int i = 0; i < event_count; i++)
        {   
            //如果是serverfd的连接事件
            if (events[i].data.fd == serverfd)
            {
                // 有新连接到达
                clientfd = accept(serverfd, (struct sockaddr *)&client_addr, &addr_len);
                if (clientfd == -1)
                {
                    perror("accept");
                    continue;
                }
                // 将clientfd的读事件添加到 epoll 实例中
                event.events = EPOLLIN;
                event.data.fd = clientfd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &event) == -1)
                {
                    perror("epoll_ctl");
                    return -1;
                }
            }
            else    //是clientfd的可读事件
            {
                // 有数据可读
                char buf[1024];
                int len = read(events[i].data.fd, buf, sizeof(buf) - 1);
                if (len <= 0)
                {
                    // 发⽣错误或连接关闭，关闭连接
                    close(events[i].data.fd);
                }
                else
                {
                    //补上字符串结束符
                    buf[len] = '\0';
                    printf("接收到消息：%s\n", buf);
                    // echo
                    write(events[i].data.fd, buf, len);
                }
            }
        }
    }
    //是走不到这里来的
    std::cout<<"inifite loop end"<<std::endl;
    // 关闭监听套接字和 epoll 实例
    close(serverfd);
    close(epoll_fd);
    return 0;
}

//使用原生调用
// g++ test4.cc -o test4 -std=c++11
// qps:873.66
// ab -n 100 -c 10 https://127.0.0.1:9190/