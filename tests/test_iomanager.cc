
//通过IO协程调度器实现一个简单的TCP客户端，这个客户端会不停地判断是否可读，并把读到的消息打印出来
//当服务器关闭连接时客户端也退出

// #include "sylar/sylar.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <cassert>
#include <fcntl.h>
#include "../src/iomanager.h"
// #include "macro.h"

// sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sockfd;
void watch_io_read();

// 写事件回调，只执行一次，用于判断非阻塞套接字connect成功
void do_io_write() {
    //SYLAR_LOG_INFO(g_logger) << "write callback";
    int so_err;
    socklen_t len = size_t(so_err);

    //检查套接字上是否有待处理的错误
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_err, &len);
    if(so_err) {
        std::cout<<"so_err = "<<so_err<<std::endl;
        //SYLAR_LOG_INFO(g_logger) << "connect fail";
        return;
    } 
    //SYLAR_LOG_INFO(g_logger) << "connect success";
}

// 读事件回调，每次读取之后如果套接字未关闭，需要重新添加 
void do_io_read() {
    //SYLAR_LOG_INFO(g_logger) << "read callback";
    char buf[1024] = {0};
    int readlen = 0;
    readlen = read(sockfd, buf, sizeof(buf));
    if(readlen > 0) {
        //填上结束符
        buf[readlen] = '\0';
        //SYLAR_LOG_INFO(g_logger) << "read " << readlen << " bytes, read: " << buf;
    } 
    else if(readlen == 0) {     //对端已经关闭
       // SYLAR_LOG_INFO(g_logger) << "peer closed";
        close(sockfd);
        return;
    } 
    else {              //触发这个分支
        //SYLAR_LOG_ERROR(g_logger) << "err, errno=" << errno << ", errstr=" << strerror(errno);
        close(sockfd);
        return;
    }

    // read之后重新添加读事件回调，这里不能直接调用addEvent，因为在当前位置fd的三元组中读事件上下文还是有效的，直接调用addEvent相当于重复添加相同事件 
    // 可以去看addevent逻辑，那里不允许对同一个fd注册相同事件
    sylar::IOManager::GetThis()->schedule(watch_io_read);
}

void watch_io_read() {
    std::cout<<"watch io read"<<std::endl;
    //SYLAR_LOG_INFO(g_logger) << "watch_io_read";
    sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::READ, do_io_read);
}

void test_io() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // SYLAR_ASSERT(sockfd > 0);
    assert(sockfd > 0);
    //将套接字设置为非阻塞模式
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9190);
    inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr.s_addr);
    
    //client将clientfd连接到服务端
    //由于套接字是非阻塞的，如果连接不能立即完成，connect函数不会阻塞，而是立即返回一个错误。
    //并且会向sockfd写一个epollerr，这在后面会读取到
    int rt = connect(sockfd, (const sockaddr*)&servaddr, sizeof(servaddr));
    
    //一般情况下会失败 因为没有serverfd在listen
    if(rt != 0) {
        /*
        当一个异步或非阻塞的套接字调用尚未完成时，系统调用可能会返回这个错误。
        例如，在TCP连接建立过程中（三次握手），如果在连接尚未完全建立时尝试发送数据，或者在bind、listen、connect、accept等函数调用中，
        如果这些操作不能立即完成，那么就可能返回EINPROGRESS
        */
        if(errno == EINPROGRESS) {
           // SYLAR_LOG_INFO(g_logger) << "EINPROGRESS";
            // 注册写事件回调，只用于判断connect是否成功
            // 非阻塞的TCP套接字connect一般无法立即建立连接，要通过套接字可写来判断connect是否已经成功
            
            //向epollfd中添加一个sockfd的写监视事件
            sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::WRITE, do_io_write);

            // 注册读事件回调，注意事件是一次性的
            sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::READ, do_io_read);
        } 
        else {
            //发生了其他错误，如地址不可达或端口不可用，此时会打印错误信息。
           // SYLAR_LOG_ERROR(g_logger) << "connect error, errno:" << errno << ", errstr:" << strerror(errno);
        }
    } 
    else {
        //这通常意味着连接已经立即完成，但这在这个场景下不太可能，因此也会打印错误信息，因为非阻塞套接字的connect不应该立即返回成功。
      //  SYLAR_LOG_ERROR(g_logger) << "else, errno:" << errno << ", errstr:" << strerror(errno);
    }
    std::cout<<"test io end"<<std::endl; //因为test_io的自然结束，os会向sockfd发送一个errnum
}

void test_iomanager() {
    //new 一个io manager出来 线程数为1 并且调度caller线程
    sylar::IOManager iom;
    
    // sylar::IOManager iom(10); // 演示多线程下IO协程在不同线程之间切换
    iom.schedule(test_io);
    std::cout<<"tag3"<<std::endl;
    //当函数即将结束，执行iomanager的析构函数
    //主协程退出，调度协程获得cpu?
}

int main(int argc, char *argv[]) {
    //sylar::EnvMgr::GetInstance()->init(argc, argv);
    //sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());
    
    test_iomanager();
    std::cout<<"tag end"<<std::endl;

    return 0;
}

//g++ test_iomanager.cc ../src/fiber.cc ../src/scheduler.cc ../src/util.cpp ../src/thread.cc ../src/mutex.cc ../src/iomanager.cc ../src/timer.cc -o test -std=c++11 -lpthread