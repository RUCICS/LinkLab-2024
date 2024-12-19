// As if this is the implementation of libc.

#include "minilibc.h"
#include <stdarg.h>
#include <sys/syscall.h>

long syscall(int num, ...)
{
    va_list ap;
    va_start(ap, num);
    register long a0 asm("rax") = num;
    register long a1 asm("rdi") = va_arg(ap, long);
    register long a2 asm("rsi") = va_arg(ap, long);
    register long a3 asm("rdx") = va_arg(ap, long);
    register long a4 asm("r10") = va_arg(ap, long);
    va_end(ap);
    asm volatile("syscall"
        : "+r"(a0)
        : "r"(a1), "r"(a2), "r"(a3), "r"(a4)
        : "memory", "rcx", "r8", "r9", "r11");
    return a0;
}

size_t strlen(const char* s)
{
    size_t len = 0;
    for (; *s; s++)
        len++;
    return len;
}

char* strcpy(char* d, const char* s)
{
    char* r = d;
    while (*s) {
        *d++ = *s++;
    }
    *d = '\0';
    return r;
}

char* strchr(const char* s, int c)
{
    for (; *s; s++) {
        if (*s == c)
            return (char*)s;
    }
    return NULL;
}

void print(const char* s, ...)
{
    va_list ap;
    va_start(ap, s);
    while (s) {
        syscall(SYS_write, 1, s, strlen(s));
        s = va_arg(ap, const char*);
    }
    va_end(ap);
}

int sprintf(char* buf, const char* fmt, ...)
{
    char* p = buf;
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case 'd': {
                int val = va_arg(ap, int);
                if (val < 0) {
                    *p++ = '-';
                    val = -val;
                }

                // 转换整数到字符串
                char tmp[32];
                char* t = tmp;
                do {
                    *t++ = '0' + (val % 10);
                    val /= 10;
                } while (val);

                // 反转数字
                while (t > tmp) {
                    *p++ = *--t;
                }
                break;
            }
            default:
                *p++ = *fmt;
                break;
            }
        } else {
            *p++ = *fmt;
        }
        fmt++;
    }

    *p = '\0';
    va_end(ap);
    return p - buf;
}

int main();

// libc provides the "_start".
void _start() { syscall(SYS_exit, main()); }
