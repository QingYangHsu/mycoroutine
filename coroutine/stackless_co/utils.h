//
// Created by Jasonkay on 2022/6/12.
//

#ifndef COROUTINE_UTILS_H
#define COROUTINE_UTILS_H

namespace stackless_co {

    class Schedule;
    //协程可以调用的函数签名，该函数由client指定
    typedef void (*coroutine_func)(Schedule *, void *ud);

} // namespace stackless_co

#endif //COROUTINE_UTILS_H
