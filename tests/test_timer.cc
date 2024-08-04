/**
 * @file test_timer.cc
 * @brief IO协程测试器定时器测试
 * @version 0.1
 * @date 2021-06-19
 */

#include "../src/timer.h"
#include "../src/iomanager.h"
#include <iostream>

// static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static int timeout = 1000;
static sylar::Timer::ptr s_timer;

//1000ms定时器的callback
void timer_callback() {
    // SYLAR_LOG_INFO(g_logger) << "timer callback, timeout = " << timeout;
    std::cout<<"timer callback, timeout = " << timeout<<std::endl;
    //每次时间到 周期+1000ms
    timeout += 1000;
    if(timeout < 5000) {
        //从当前时间开始（from_now 为 true）
        s_timer->reset(timeout, true);
    } 
    else {
        //删除定时器
        s_timer->cancel();
    }
}

void test_timer() {

    //io协程调度器 因为timemanager是一个纯虚类 不允许实例化
    sylar::IOManager iom;

    //IOManager从TimerManager 和 scheduler继承过来 
    //循环定时器
    s_timer = iom.addTimer(1000, timer_callback, true);
    
    // 单次定时器
    iom.addTimer(500, 
        []{
            // SYLAR_LOG_INFO(g_logger) << "500ms timeout";
            std::cout<<"500ms timeout"<<std::endl;
        }
    );
    
    iom.addTimer(5000, []{
        // SYLAR_LOG_INFO(g_logger) << "5000ms timeout";
        std::cout<<"5000ms timeout"<<std::endl;
    });
}

int main(int argc, char *argv[]) {
    // sylar::EnvMgr::GetInstance()->init(argc, argv);
    // sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    test_timer();

    // SYLAR_LOG_INFO(g_logger) << "end";
    std::cout<<"end"<<std::endl;

    return 0;
}

//g++ test_timer.cc ../src/fiber.cc ../src/scheduler.cc ../src/util.cpp ../src/thread.cc ../src/mutex.cc ../src/iomanager.cc ../src/timer.cc -o test -std=c++11 -lpthread