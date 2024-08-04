//
// Created by Jasonkay on 2022/6/6.
//

#include "utils.h"
#include "coroutine.h"
#include "schedule.h"

namespace stackless_co {
    //这里传入的第一个参数并没有什么用 
    Coroutine *Coroutine::new_co(Schedule *s, coroutine_func func, void *ud) {
        auto *co_vec = new Coroutine();
        co_vec->func = func;
        co_vec->ud = ud;
        co_vec->cap = 0;
        co_vec->size = 0;
        
        //初始状态就默认就绪态
        co_vec->status = Schedule::COROUTINE_READY;
        co_vec->stack = nullptr;
        return co_vec;
    }

    void Coroutine::delete_co() {
        free(this->stack);
        free(this);
    }

} // namespace stackless_co