/**
 * @file mutex.cc
 * @brief 信号量实现
 * @version 0.1
 * @date 2021-06-09
 */

#include "mutex.h"

namespace sylar {

Semaphore::Semaphore(uint32_t count) {
    if(sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() {
    sem_destroy(&m_semaphore);
}

void Semaphore::wait() {
    /*
    wait操作（在POSIX中通常称为sem_wait）用于阻塞当前线程（或进程），直到信号量的值大于零。当线程调用wait操作时，信号量的值会减一（如果之前大于零）。
    */
    if(sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify() {
    /*
    post操作（在POSIX中称为sem_post）用于增加信号量的值。当线程调用post操作时，信号量的值会加一。如果有线程因为调用wait而被阻塞在该信号量上，且信号量的值因此变为正数，则其中一个（或所有，具体取决于信号量的类型）被阻塞的线程将被唤醒以继续执行。
    */
    if(sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}

} // namespace sylar
