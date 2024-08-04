
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "../src/iomanager.h"
#include <string.h>

//测试的系统调用 sleep() socket() connect()(内部调的是connect_with_timeout) send() receve()

/**
 * @brief 测试sleep被hook之后的浆果
 */
void test_sleep() {
    std::cout<<"test_sleep begin"<<std::endl;
    sylar::IOManager iom;
    
    /**
     * 这里的两个协程sleep是同时开始的，一共只会睡眠3秒钟，第一个协程开始sleep后，会yield到后台，
     * 第二个协程会得到执行，最终两个协程都会yield到后台，并等待睡眠时间结束，相当于两个sleep是同一起点开始的
     */
    iom.schedule([] {
        sleep(2);               //我们看sleep内部还会再做一次schedule
        std::cout<<"sleep 2"<<std::endl;
        //SYLAR_LOG_INFO(g_logger) << "sleep 2";
    });

    // iom.schedule([] {
    //     sleep(3);
    //     std::cout<<"sleep 3"<<std::endl;
    // });
    std::cout<<"test_sleep end"<<std::endl;
}

/**
 * 测试socket api hook
 */
void test_sock() {
    std::cout<<"test_sock begin"<<std::endl;
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9190);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

    std::cout<<"begin connect"<<std::endl;
    
    int rt = connect(sock, (const sockaddr*)&addr, sizeof(addr));
    std::cout<<"connect rt="<<rt<<" errno= " << errno<<std::endl;
    //如果失败
    if(rt) {
        return;
    }

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    rt = send(sock, data, sizeof(data), 0);

    std::cout<<"send rt="<<rt<<" errno= " << errno<<std::endl;
    //发送失败
    if(rt <= 0) {
        return;
    }

    std::string buff;
    buff.resize(4096);

    rt = recv(sock, &buff[0], buff.size(), 0);
    std::cout<<"recv rt="<<rt<<" errno= " << errno<<std::endl;
    //接收失败
    if(rt <= 0) {
        return;
    }
    buff.resize(rt);
    std::cout<<buff<<std::endl;
}

int main(int argc, char *argv[]) {
    // sylar::EnvMgr::GetInstance()->init(argc, argv);
    // sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    test_sleep();
    std::cout<<"test sleep end2"<<std::endl;

    // 只有以协程调度的方式运行hook才能生效
    // 每个线程的hook开关在scheduler的run里面
    sylar::IOManager iom;
    iom.schedule(test_sock);

    //SYLAR_LOG_INFO(g_logger) << "main end";
    std::cout<<"main end"<<std::endl;
    return 0;
}


//g++ test_hook.cc ../src/iomanager.cc ../src/scheduler.cc ../src/fiber.cc ../src/mutex.cc ../src/thread.cc  ../src/timer.cc ../src/util.cpp ../src/hook.cc ../src/fd_manager.cc -o test -std=c++11 -lpthread -ldl