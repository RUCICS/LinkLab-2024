// As if this is the C standard library headers.

#include <stddef.h>
#include <sys/syscall.h>

long syscall(int num, ...);
size_t strlen(const char* s);
char* strcpy(char* d, const char* s);
char* strchr(const char* s, int c);
void print(const char* s, ...);

// Global data
int n = 10;

int main()
{
    // Function calls
    char* p = "Hello, World!";
    for (int i = 0; i < strlen(p); i++) {
        print("Message: ", p, NULL);
    }

    return 42;
}
