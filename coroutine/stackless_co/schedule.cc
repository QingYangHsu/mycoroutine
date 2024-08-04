//
// Created by JasonkayZK on 2022.06.06.
//

#include "utils.h"
#include "schedule.h"
#include "coroutine.h"
#include <iostream>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdint>

#if __APPLE__ && __MACH__

#include <sys/ucontext.h>

#else

#include <ucontext.h>

#endif


namespace stackless_co {

    Schedule *Schedule::schedule_new() {
        auto *s = new Schedule();
        s->nco = 0;
        //初始容量设置为1
        s->cap_of_vec = 1;
        s->running = -1;
        s->co_vec = (Coroutine **) malloc(sizeof(Coroutine *) * s->cap_of_vec);
        memset(s->co_vec, 0, sizeof(struct coroutine *) * s->cap_of_vec);
        return s;
    }

    //这是因为 makecontext 的函数指针的参数是 uint32_t 类型，在 64 位系统下，一个 uint32_t 没法承载一个指针, 所以基于兼容性的考虑，才采用了这种做法
    void Schedule::main_func(uint32_t low32, uint32_t hi32) {
        //两个32位地址拼成一个64位地址,这个ptr代表schedule*
        uintptr_t ptr = (uintptr_t) low32 | ((uintptr_t) hi32 << 32);
        auto *s = (Schedule *) ptr;

        int id = s->running;
        Coroutine *c = s->co_vec[id];

        //运行client指定函数
        c->get_func()(s, c->get_ud());

        //客户指定函数运行完成，该协程使命结束
        c->delete_co();
        s->co_vec[id] = nullptr;
        --s->nco;
        
        //schedule当前没有运行任何协程
        s->running = -1;
    }

    void Schedule::schedule_close() {
        int i;
        for (i = 0; i < this->cap_of_vec; i++) {
            Coroutine *inner_co = this->co_vec[i];
            if (inner_co) {
                //关闭协程
                inner_co->delete_co();
            }
        }
        //释放掉当前协程数组
        free(this->co_vec);
        this->co_vec = nullptr;

        //释放掉自己
        free(this);
    }

    int Schedule::coroutine_new(coroutine_func func, void *ud) {
        //new一个协程出来
        Coroutine *inner_co = Coroutine::new_co(this, func, ud);
        
        if (this->nco >= this->cap_of_vec) {        //当前协程数组满了
            //std::cout<<"tag1"<<std::endl;
            int id = this->cap_of_vec;
            this->co_vec = (Coroutine **) realloc(this->co_vec, this->cap_of_vec * 2 * sizeof(Coroutine *));
            
            //将新开辟的空间全部初始化为0
            memset(this->co_vec + this->cap_of_vec, 0, sizeof(struct coroutine *) * this->cap_of_vec);
            this->co_vec[this->cap_of_vec] = inner_co;
            
            //改变容量
            this->cap_of_vec *= 2;
            
            //改变size
            ++this->nco;
            return id;
        } 
        else {
            std::cout<<"tag2"<<std::endl;
            int i;
            for (i = 0; i < this->cap_of_vec; i++) {
                //从当前数组末尾开始遍历一轮
                int id = (i + this->nco) % this->cap_of_vec;
                if (this->co_vec[id] == nullptr) {
                    this->co_vec[id] = inner_co;
                    ++this->nco;
                    return id;
                }
            }
        }

        return 0;
    }

    void Schedule::coroutine_resume(int id) {
        //做两个断言
        assert(this->running == -1);
        assert(id >= 0 && id < this->cap_of_vec);
        
        Coroutine *c = this->co_vec[id];
        if (c == nullptr) return;

        int status = c->get_status();

        //这个ptr是指向当前schedule类的指针
        auto ptr = (uintptr_t) this;

        switch (status) {
            case COROUTINE_READY:
                //将当前cpu运行上下文信息保存到c的context中
                getcontext(c->get_ctx());

                //将协程c的运行栈设置为 this->stack，也就是schedule类的共享栈
                c->get_ctx()->uc_stack.ss_sp = this->stack;

                c->get_ctx()->uc_stack.ss_size = STACK_SIZE;
                
                //c协程执行完之后回到this->main主协程 uc_link是后序上下文
                c->get_ctx()->uc_link = &this->main;
                
                this->running = id;
                
                c->set_status(COROUTINE_RUNNING);

                //修改c的上下文信息，设置其执行函数为main_func，后面两个参数为mainfunc的参数
                makecontext(c->get_ctx(), (void (*)()) main_func, 2, (uint32_t) ptr, (uint32_t) (ptr >> 32));
                
                //将当前cpu信息保存到this->main,将c->get_ctx()的信息恢复成cpu上下文
                //等于getcontext(&this->main) setcontext(c->get_ctx())
                swapcontext(&this->main, c->get_ctx());
                break;
            case COROUTINE_SUSPEND:     
            //暂停态 this->stack + STACK_SIZE为栈底，c->get_size()为协程运行时栈保存起来的大小， 
            //this->stack + STACK_SIZE - c->get_size()就是c运行栈的栈顶位置
                memcpy(this->stack + STACK_SIZE - c->get_size(), c->get_stack(), c->get_size());
                this->running = id;
                c->set_status(COROUTINE_RUNNING);
                swapcontext(&this->main, c->get_ctx());
                break;
            default:
                assert(0);//故意触发断言失败
        }
    }

    int Schedule::coroutine_status(int id) {
        assert(id >= 0 && id < this->cap_of_vec);
        if (this->co_vec[id] == nullptr) {
            return COROUTINE_DEAD;
        }
        return this->co_vec[id]->get_status();
    }

    int Schedule::coroutine_running() const {
        return this->running;
    }

    void Schedule::coroutine_yield() {
        int id = this->running;
        assert(id >= 0);
        Coroutine *c = this->co_vec[id];
        assert((char *) &c > this->stack);
        //将当前协程的栈保存起来，本例中协程的实现是基于共享栈的，所以协程的栈内容需要单独保存起来；
        //这里也是共享栈的低效之处 会有大量的内存拷贝

        _save_stack(c, this->stack + STACK_SIZE);
        c->set_status(COROUTINE_SUSPEND);
        this->running = -1;
        //将当前上下文信息保存到c->get_ctx(),恢复this->main中保存的上下文
        swapcontext(c->get_ctx(), &this->main);
    }

    //top标定了c这个协程运行栈的栈底(因为栈是从高地址向地地址增长)，这个top地址是最大的地址，所以是栈底
    void Schedule::_save_stack(Coroutine *c, char *top) {
        //因为 dummy 变量是刚刚分配到栈上的，因此，此时就位于 栈的最顶部位置 也就是栈顶
        //这一点很重要
        char dummy = 0;
        //top是栈底，对应高地址，dummy是栈顶，对应低地址 中间部分就是c的运行时栈
        assert(top - &dummy <= STACK_SIZE);

        //该协程已经分配的旧栈的容量不能满足，需要重新分配
        if (c->get_cap() < top - &dummy) {
            //将协程旧的分配栈释放
            free(c->get_stack());
            //设置协程运行时栈容量 需要多少分配多少 不浪费空间
            c->set_cap(top - &dummy);
            //重新申请一片空间当做c的运行栈，原来的共享栈要让出去了
            c->set_stack(static_cast<char *>(malloc(c->get_cap())));
        }
        //设置协程运行时size
        c->set_size(top - &dummy);

        //从schedule共享栈处把运行栈内容拷贝过来
        memcpy(c->get_stack(), &dummy, c->get_size());
    }

} // namespace stackless_co