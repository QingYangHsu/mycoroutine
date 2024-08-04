#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
 
ssize_t write(int fd, const void *buf, size_t count) {
    //函数内容是用syscall的方式直接调用编号为SYS_write的系统调用，实现的效果也是往标准输出写内容，只不过这里我们将输出内容替换成了其他值
    syscall(SYS_write, STDOUT_FILENO, "12345\n", strlen("12345\n"));
}