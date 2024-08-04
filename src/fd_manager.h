#ifndef __FD_MANAGER_H__
#define __FD_MANAGER_H__

#include <memory>
#include <vector>
#include "thread.h"
#include "singleton.h"

namespace sylar {

/**
 * @brief 文件句柄上下文类
 * @details 管理文件句柄类型(是否socket)
 *          是否阻塞,是否关闭,读/写超时时间
 */
class FdCtx : public std::enable_shared_from_this<FdCtx> {
public:
    typedef std::shared_ptr<FdCtx> ptr;
    /**
     * @brief 通过文件句柄构造FdCtx
     */
    FdCtx(int fd);

    /**
     * @brief 析构函数
     */
    ~FdCtx();

    /**
     * @brief 是否初始化完成
     */
    bool isInit() const { return m_isInit;}

    /**
     * @brief 是否socket
     */
    bool isSocket() const { return m_isSocket;}

    /**
     * @brief 是否已关闭
     */
    bool isClose() const { return m_isClosed;}

    /**
     * @brief 设置用户主动设置非阻塞 该函数会在fctrl 与ictrl的自定义版本中调用
     * @param[in] v 是否阻塞
     */
    void setUserNonblock(bool v) { m_userNonblock = v;}

    /**
     * @brief 获取是否用户主动设置的非阻塞
     */
    bool getUserNonblock() const { return m_userNonblock;}

    /**
     * @brief 设置系统非阻塞,也就是hook非阻塞
     * @param[in] v 是否阻塞
     */
    void setSysNonblock(bool v) { m_sysNonblock = v;}

    /**
     * @brief 获取系统非阻塞
     */
    bool getSysNonblock() const { return m_sysNonblock;}

    /**
     * @brief 设置超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @param[in] v 时间毫秒
     * 该函数在setsockopt中会调用，用于设置socket的发送与接收时间
     */
    void setTimeout(int type, uint64_t v);

    /**
     * @brief 获取超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @return 超时时间毫秒
     */
    uint64_t getTimeout(int type);

private:
    /**
     * @brief 初始化
     */
    bool init();

private:
    //每个位字段都被指定为占用1位，
    
    /// 是否初始化 即是否已经调用过init函数
    bool m_isInit: 1;

    /// 是否socket
    bool m_isSocket: 1;
    
    /// 是否hook非阻塞
    bool m_sysNonblock: 1;
    
    /// 是否用户主动设置非阻塞
    bool m_userNonblock: 1;
    
    /// 是否关闭
    bool m_isClosed: 1;
    
    /// 文件句柄
    int m_fd;
    
    //下面两个超时时间是系统调用比如send recv，针对阻塞socket的最长等待时间,这里体现了hook的特点
    /// 读超时时间毫秒
    uint64_t m_recvTimeout;
    
    /// 写超时时间毫秒
    uint64_t m_sendTimeout;
};

/**
 * @brief 文件句柄管理类
 */
class FdManager {
public:
    typedef RWMutex RWMutexType;
    /**
     * @brief 无参构造函数
     */
    FdManager();

    /**
     * @brief 获取/创建文件句柄类FdCtx
     * @param[in] fd 文件句柄
     * @param[in] auto_create 是否自动创建，也就是当对应fdctx不存在时是否需要fdmanager自己创建一个
     * @return 返回对应文件句柄类FdCtx::ptr
     */
    FdCtx::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除文件句柄类
     * @param[in] fd 文件句柄
     */
    void del(int fd);

private:
    /// 读写锁类型 其实底层就是pthread_rwlock_t类型
    RWMutexType m_mutex;

    /// 文件句柄集合
    std::vector<FdCtx::ptr> m_datas;
};

/// 文件句柄单例
typedef Singleton<FdManager> FdMgr;

}

#endif
