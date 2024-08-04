// #include "sylar.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <stack>
#include <string.h>
#include "../src/iomanager.h"

static int serverfd = -1;

//前向声明
void test_accept();

void error(char* msg) {
    perror(msg);
    printf("erreur...\n");
    exit(1);
}

void watch_io_read() {
    //相当于击鼓传花
    sylar::IOManager::GetThis()->addEvent(serverfd, sylar::IOManager::READ, test_accept);
}


//当serverfd发生可读事件，说明有客户连接进来,注册的回调函数
void test_accept() {
    struct sockaddr_in addr; 
    memset( &addr,0,sizeof(addr) );
    socklen_t len = sizeof(addr);
    int clientfd = accept(serverfd, (struct sockaddr*)&addr, &len);

    if (clientfd < 0) {
        std::cout << "clientfd = "<< clientfd << "accept false" << std::endl;
    } 
    else {
        std::cout<<"来了个客户连接"<<"clientfd = "<< clientfd <<std::endl;
        
        //对clientfd设置非阻塞
        fcntl(clientfd, F_SETFL, O_NONBLOCK);
        
        //对clientfd添加可读事件，来读取client向server发来的信息
        //这里传值
        sylar::IOManager::GetThis()->addEvent(clientfd, sylar::IOManager::READ, 
        [clientfd](){
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            while (true) {
                int ret = recv(clientfd, buffer, sizeof(buffer), 0);
                if (ret > 0) {
                    //echo
                    ret = send(clientfd, buffer, ret, 0);
                }
                if (ret <= 0) {
                    //EAGAIN是一个在Linux系统编程中常见的错误码，其含义为“资源暂时不可用”。
                    //这个错误码通常出现在非阻塞模式下，因为我们已经对clientfd设置了非阻塞
                    //当进程尝试进行一个非阻塞操作时，由于资源不可用（如没有足够的数据可读或缓冲区已满），
                    //系统调用会以非阻塞方式失败并返回EAGAIN错误。
                    if (errno == EAGAIN) continue;
                    close(clientfd);
                    break;
                }
            }
        }
        );

    }
    //向io manager调度一个任务，这个任务的主要目的是向iomanager再一次添加对serverfd的读事件的监视
    //为什么呢？我们去看iomanager的idle那里，当待注册事件发生之后，会将发生的事件删除，并将事件回调函数push进调度器
    //所以这里我们要再次注册serverfd的监视事件
    sylar::IOManager::GetThis()->schedule(watch_io_read); 
}


void test_iomanager() {
    int portno = 9190;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // setup socket
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        error("Error creating socket..\n");
    }
    int yes = 1;
    std::cout<<"serverfd是"<<serverfd<<std::endl;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset((char *)&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);
    server_addr.sin_addr.s_addr = INADDR_ANY;


    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        error("Error binding socket..\n");
    }

    if (listen(serverfd, 1024) < 0) {
        error("Error listening..\n");
    }

    printf("epoll echo server listening for connections on port: %d\n", portno);
    
    //设置serverfd非阻塞
    fcntl(serverfd, F_SETFL, O_NONBLOCK);
    
    sylar::IOManager iom;
    
    //向iomanager的epollfd注册一个针对serverfd的监视读事件，同时m_pendingeventcount会+1
    iom.addEvent(serverfd, sylar::IOManager::READ, test_accept);
}

int main(int argc, char *argv[]) {
    test_iomanager();
    return 0;
}

//使用mysylar库 并且开启hook
//g++ test1.cc ../src/iomanager.cc ../src/scheduler.cc ../src/fiber.cc ../src/mutex.cc ../src/thread.cc  ../src/timer.cc ../src/util.cpp ../src/hook.cc  ../src/fd_manager.cc -o test -std=c++11 -lpthread -ldl
//qps:1266.14
//ab -n 10 -c 2 https://127.0.0.1:9190/

