//
// Created by Jasonkay on 2022/6/5.
//

#include "context.h"
#include<iostream>

extern "C" {
    //将当前cpu的上下文保存到第一个上下文，将第二个上下文赋值给当前cpu的上下文
    //相当于ucontext_t 族函数swapcontext()
extern void switch_context(stack_co::Context *, stack_co::Context *) asm("switch_context");
}

namespace stack_co {

    void Context::switch_from(Context *previous) {
        //cpu上下文环境从previous切换成this
        switch_context(previous, this);
    }

    //相当于ucontext_t 族函数makecontext()
    void Context::prepare_context(Context::Callback ret, Context::Word rdi) {
        
        //拿到该协程对应的上下文环境的栈指针
        Word sp = get_stack_pointer();

        //将这三个地址赋值到寄存器 栈顶地址 返回函数地址 函数参数地址
        fill_registers(sp, ret, rdi);
    }

    //测试当前context是否获得cpu 即是否成功上位
    bool Context::test() {
        char current;
        ptrdiff_t diff = std::distance(std::begin(_stack), &current);
        return diff >= 0 && diff < STACK_SIZE;
    }

    Context::Word Context::get_stack_pointer() {
        //从这里可以看出栈增长方向是从大地址到小地址
        auto sp = std::end(_stack) - sizeof(Word);
        
        //将 sp 的地址对齐到 16 字节
        sp = decltype(sp)(reinterpret_cast<size_t>(sp) & (~0xF));
        return sp;
    }

    void Context::fill_registers(Word sp, Callback ret, Word rdi, ...) {
        ::memset(_registers, 0, sizeof _registers);
        //这个sp是栈尾部地址向下取整的地址
        auto temp_pRet = (Word *) sp;
        *temp_pRet = (Word) ret;

        _registers[RSP] = sp;           //将栈地址赋值到寄存器
        _registers[RET] = *temp_pRet;   //将函数返回地址赋值到寄存器
        _registers[RDI] = rdi;          //将函数第一个参数的地址赋值到寄存器

        //由此以来，当该context上cpu之后，会跳转到该函数处执行
    }

} // namespace stack_co
