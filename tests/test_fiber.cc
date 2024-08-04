/**
 * @file test_fiber.cc
 * @brief 协程测试
 * @version 0.1
 * @date 2021-06-15
 */
#include "../src/fiber.h"
#include "../src/thread.h"
#include "../src/util.h"
#include <string>
#include <vector>

// sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run_in_fiber2()
{
    std::cout << "run_in_fiber2 begin" << std::endl;
    std::cout << "run_in_fiber2 end" << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "run_in_fiber2 begin";
    // SYLAR_LOG_INFO(g_logger) << "run_in_fiber2 end";
}

void run_in_fiber()
{
    // SYLAR_LOG_INFO(g_logger) << "run_in_fiber begin";
    std::cout << "run_in_fiber begin" << std::endl;
    std::cout << "before run_in_fiber yield" << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "before run_in_fiber yield";
    sylar::Fiber::GetThis()->yield();       //暂停执行，cpu执行权从fiber手里回到线程主协程手里
    // SYLAR_LOG_INFO(g_logger) << "after run_in_fiber yield";
    std::cout << "after run_in_fiber yield" << std::endl;
    std::cout << "run_in_fiber end" << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "run_in_fiber end";
    //  fiber结束之后会先回到mainfunc中，mainfunc结束之后会做一次yield，cpu自动返回到线程主协程运行
}

void test_fiber()
{
    // SYLAR_LOG_INFO(g_logger) << "test_fiber begin";
    std::cout <<"test_fiber begin" << std::endl;

    // 初始化线程主协程,为该线程new一个线程主协程出来
    sylar::Fiber::GetThis();

    // 这里线程搞出来的协程都不参与调度器调度
    sylar::Fiber::ptr fiber(new sylar::Fiber(run_in_fiber, 0, false));

    // SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 1
    std::cout << "use_count" << fiber.use_count() << std::endl;
    std::cout << "before test_fiber resume" << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "before test_fiber resume";
    fiber->resume();    //此时cpu从线程主协程交到fiber手里，去执行client函数

    // SYLAR_LOG_INFO(g_logger) << "after test_fiber resume";
    std::cout << "after test_fiber resume" << std::endl;
    /**
     * 关于fiber智能指针的引用计数为3的说明：
     * 一份在当前函数的fiber指针，一份在MainFunc的cur指针,因为mainfunc一开始就做了getthis动作
     * 还有一份在在run_in_fiber的GetThis()结果的临时变量里，这个临时变量指针还没有释放
     */
    // SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 3
    std::cout << "use_count:" << fiber.use_count() << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "fiber status: " << fiber->getState(); // READY
    std::cout << "fiber status: " << fiber->getState() << std::endl;
    std::cout << "before test_fiber resume again" << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "before test_fiber resume again";
    fiber->resume();
    // SYLAR_LOG_INFO(g_logger) << "after test_fiber resume again";
    std::cout << "after test_fiber resume again" << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 1
    // SYLAR_LOG_INFO(g_logger) << "fiber status: " << fiber->getState(); // TERM
    /*
    为什么为1？因为run_in_fiber的GetThis()结果的临时变量已经释放
    MainFunc的cur指针已经reset
    */
    std::cout << "use_count:" << fiber.use_count() << std::endl;
    std::cout << "fiber status: " << fiber->getState() << std::endl;        //term
    fiber->reset(run_in_fiber2); // 上一个协程结束之后，复用其栈空间再创建一个新协程,并重新制定client函数
    fiber->resume();

    // SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 1
    //原因与上相同
    std::cout << "use_count:" << fiber.use_count() << std::endl;
    // SYLAR_LOG_INFO(g_logger) << "test_fiber end";
    std::cout << "test_fiber end" << std::endl;
}

int main(int argc, char *argv[])
{
    // sylar::EnvMgr::GetInstance()->init(argc, argv);
    // sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    sylar::SetThreadName("main_thread");
    // SYLAR_LOG_INFO(g_logger) << "main begin";
    std::cout << "main bgin" << std::endl;

    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 2; i++)
    {
        thrs.push_back(sylar::Thread::ptr(
            new sylar::Thread(&test_fiber, "thread_" + std::to_string(i))));
    }

    for (auto i : thrs)
    {
        i->join();
        std::cout<<"thread join over"<<std::endl;
    }

    // SYLAR_LOG_INFO(g_logger) << "main end";
    std::cout << "main end" << std::endl;
    return 0;
}

// g++ test_fiber.cc ../src/fiber.cc ../src/scheduler.cc ../src/util.cpp ../src/thread.cc ../src/mutex.cc -o test -std=c++11 -lpthread