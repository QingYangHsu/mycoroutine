#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
 
// ssize_t write(int fd, const void *buf, size_t count) {
//     syscall(SYS_write, STDOUT_FILENO, "12345\n", strlen("12345\n"));
// }
 
int main() {
    write(STDOUT_FILENO, "hello world\n", strlen("hello world\n")); // 这里调用的是上面的write实现
    return 0;
}