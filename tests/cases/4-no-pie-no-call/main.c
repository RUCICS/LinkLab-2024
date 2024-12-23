// Global variables for absolute addressing test
int result = 0;

const char str[] = "Hello, World!";

// Define our own entry point
void _start()
{
    int ans = 0;

    for (int i = 0; i < 10; i++) {
        ans += str[i];
    }

    result = ans;

    // Exit directly using syscall
    asm volatile(
        "mov %0, %%edi\n" // First argument (exit code) in edi
        "mov $60, %%eax\n" // syscall number for exit (60)
        "syscall" // Make the syscall
        : // no outputs
        : "r"(result) // input: our result variable
        : "eax", "edi" // clobbered registers
    );
}