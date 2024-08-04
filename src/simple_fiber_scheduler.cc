//一个简单的协程调度器实现

 
#include "fiber.h"
#include<iostream> 
/**
 * @brief 简单协程调度类，支持添加调度任务以及运行调度任务
 */
class easyScheduler {
public:
    /**
     * @brief 添加协程调度任务
     */
    void schedule(sylar::Fiber::ptr task) {
        m_tasks.push_back(task);
    }
 
    /**
     * @brief 执行调度任务
     */
    void run() {
        std::cout << "run begin " << std::endl;
        sylar::Fiber::ptr task;
        auto it = m_tasks.begin();
 
        while(it != m_tasks.end()) {
            task = *it;
            m_tasks.erase(it);
            std::cout << "before resume " << std::endl;
            task->resume();     
            std::cout << "after resume " << std::endl;
            it = m_tasks.begin();
            
        }
    }
private:
    /// 任务队列
    std::list<sylar::Fiber::ptr> m_tasks;
};
 
void test_fiber(int i) {
    std::cout << "hello world " << i << std::endl;
}
 
int main() {
    std::cout << "main begin " << std::endl;
    /// 初始化当前线程的主协程
    sylar::Fiber::GetThis();
 
    /// 创建调度器
    easyScheduler sc;
    std::cout << "tag 1 " << std::endl;
    /// 添加调度任务
    for(auto i = 0; i < 10; i++) {
        //通过bind将test_fiber改造成一个void()的函数对象
        //不参与协程器调度，fiber返回cpu到线程主协程
        sylar::Fiber::ptr fiber(new sylar::Fiber(
            std::bind(test_fiber, i),0,false
        ));
        sc.schedule(fiber);
    }
    std::cout << "tag 2 " << std::endl;
    /// 由线程主协程执行调度任务
    sc.run();
 
    return 0;
}