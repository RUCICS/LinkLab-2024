// Global variables for absolute addressing test
int value1 = 30;
int value2 = 20;
int value3 = 50;
int result = 42;

// Define our own entry point
void _start()
{
    // Only use global variables to test absolute addressing
    result = value1 + value2 + value3;

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