//
// Created by JasonkayZK on 2022.06.06.
//

#include "coroutine.h"
#include "utils.h"

#include <iostream>
using namespace std;

void where() {
    std::cout << "running code in a "
              << (stack_co::test() ? "coroutine" : "thread")
              << std::endl;
}

void print1() {
    std::cout << 1 << std::endl;
    stack_co::this_coroutine::yield();
    std::cout << 2 << std::endl;
}

void print2(int i, stack_co::Coroutine *co1) {
    std::cout << i << std::endl;
    co1->resume();
    where();
    std::cout << "bye" << std::endl;
}

int main() {

    auto &env = stack_co::open();

    //co1是一个指向coroutine类型的shared ptr
    auto co1 = env.create_coroutine(print1);



    auto co2 = env.create_coroutine(print2, 3, co1.get());    //3和co1.get是print2的参数

    //现在enviroenmrnt中的_c_stack布局是 最底下是main默认coroutine ，co1与co2在接下来会随着resume逐个push
    
    //co1这个协程拿到cpu执行权
    co1->resume();
    std::cout<<"跳转"<<std::endl;

    co2->resume();

    where();

    return 0;
}

//我们发现用户态在使用协程，无非就是create，resume，yield三个动作，使用非常简单

//g++ *.cc *.S -o main
/*
1
3
2
running code in a coroutine
bye
running code in a thread
*/