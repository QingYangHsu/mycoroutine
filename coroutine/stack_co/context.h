#ifndef COROUTINE_CONTEXT_H
#define COROUTINE_CONTEXT_H

#include <cstddef>
#include <cstring>
#include <iterator>

//协程上下文

namespace stack_co {

    class Coroutine;

    /**
     * The context of coroutine(in x86-64)
     *
     * low | _registers[0]: r15  |
     *     | _registers[1]: r14  |
     *     | _registers[2]: r13  |
     *     | _registers[3]: r12  |
     *     | _registers[4]: r9   |
     *     | _registers[5]: r8   |
     *     | _registers[6]: rbp  |
     *     | _registers[7]: rdi  |
     *     | _registers[8]: rsi  |
     *     | _registers[9]: ret  |
     *     | _registers[10]: rdx |
     *     | _registers[11]: rcx |
     *     | _registers[12]: rbx |
     * hig | _registers[13]: rsp |
     *
     */
    //final关键字不允许继承
    class Context final {
    public:
        using Callback = void (*)(Coroutine *);
        using Word = void *;
        //128KB
        constexpr static size_t STACK_SIZE = 1 << 17;
        
        constexpr static size_t RDI = 7;
        constexpr static size_t RSI = 8;
        constexpr static size_t RET = 9;
        constexpr static size_t RSP = 13;

    public:
        //准备协程的上下文，包括设置协程返回时的回调函数以及初始化 RDI。
        //这个函数可能在协程创建时被调用。表示预备协程环境，rdi表征ret这个函数参数的第一个参数
        void prepare_context(Callback ret, Word rdi);

        //从previous上下文切换到this的上下文
        void switch_from(Context *previous);

        //测试当前context是否获得cpu 即是否成功上位
        bool test();

    private:
        Word get_stack_pointer();

        //第一个参数为栈指针 后面为要向栈中填的寄存器的值
        void fill_registers(Word sp, Callback ret, Word rdi, ...);

    private:
        /**
         * We must ensure that registers are at the top of the memory layout.
         *
         * So the Context must have no virtual method, and len at least 14!
         */
        //用来保存当前cpu上下文的寄存器的数组
        Word _registers[14];
        
        //协程的栈空间 这个栈充当了cpu运行时，协程的运行栈空间
        //在协程执行期间，这个栈用于存储局部变量、函数调用等产生的数据。
        //这个大小是定死的

        //这里表明了是本demo是有栈协程 并且是独立栈
        //注意：这里栈增长方向是反的 即栈增长方向从高地址向低地址
        char _stack[STACK_SIZE];
    };

} // namespace stack_co

#endif //COROUTINE_CONTEXT_H
