#include "fd_manager.h"
#include "hook.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

namespace sylar {

FdCtx::FdCtx(int fd)
    :m_isInit(false)
    ,m_isSocket(false)
    ,m_sysNonblock(false)
    ,m_userNonblock(false)
    ,m_isClosed(false)
    ,m_fd(fd)
    ,m_recvTimeout(-1)
    ,m_sendTimeout(-1) {
    init();
}

FdCtx::~FdCtx() {
}

bool FdCtx::init() {
    //已经初始化过了
    if(m_isInit) {
        return true;
    }

    m_recvTimeout = -1;
    m_sendTimeout = -1;

    struct stat fd_stat;

    //获取一个打开文件的状态信息
    if(-1 == fstat(m_fd, &fd_stat)) {
        m_isInit = false;
        m_isSocket = false;
    } 
    else {
        m_isInit = true;

        //判断该文件描述符是否为socket
        m_isSocket = S_ISSOCK(fd_stat.st_mode);
    }

    //如果是socket
    if(m_isSocket) {
        int flags = fcntl_f(m_fd, F_GETFL, 0);
        if(!(flags & O_NONBLOCK)) {
            //进来的前提是该socketfd没设置非阻塞
            //设置hook非阻塞 
            std::cout<<"该socket原来没设置过非阻塞.现在设置"<<std::endl;
            fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);     //这里用的是原始调用,而不是hook调用
        }

        //无论怎样 到这里已经针对该socketfd设置了非阻塞，hook非阻塞为true
        m_sysNonblock = true;
    } 
    else {          //不是socket
        m_sysNonblock = false;
    }

    m_userNonblock = false;         //这个值等待fctrl去改变
    m_isClosed = false;
    return m_isInit;
}

void FdCtx::setTimeout(int type, uint64_t v) {
    //读超时
    if(type == SO_RCVTIMEO) {
        m_recvTimeout = v;
    } 
    else {      //写超时
        m_sendTimeout = v;
    }
}

uint64_t FdCtx::getTimeout(int type) {
    if(type == SO_RCVTIMEO) {
        return m_recvTimeout;
    } 
    else {
        return m_sendTimeout;
    }
}

FdManager::FdManager() {
    m_datas.resize(64);
}

//如果指定的fd不存在于内部存储（m_datas）中，并且auto_create参数为true，则会创建一个新的FdCtx对象并将其添加到内部存储中
FdCtx::ptr FdManager::get(int fd, bool auto_create) {
    if(fd == -1) {
        return nullptr;
    }

    RWMutexType::ReadLock lock(m_mutex);        //用读写锁初始化一把局域读锁，在ctor中自动上锁

    if((int)m_datas.size() <= fd) {
        if(auto_create == false) {
            return nullptr;
        }
    } 
    else {
        if(m_datas[fd] || !auto_create) {
            return m_datas[fd];
        }
    }
    lock.unlock();
    
    //能走到这说明auto_create一定为true
    RWMutexType::WriteLock lock2(m_mutex);
    FdCtx::ptr ctx(new FdCtx(fd));
    if(fd >= (int)m_datas.size()) {
        //1.5倍扩容
        m_datas.resize(fd * 1.5);
    }
    m_datas[fd] = ctx;
    return ctx;
}

void FdManager::del(int fd) {
    RWMutexType::WriteLock lock(m_mutex);
    if((int)m_datas.size() <= fd) {
        return;
    }
    //引用计数--，当引用计数为0自动调fdctx的析构函数,下一次重新用该fd，直接覆盖该位置的fd 当
    m_datas[fd].reset();
}

}
