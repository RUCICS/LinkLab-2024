# LinkLab 2024：构建你自己的链接器

```
 ___       ___  ________   ___  __    ___       ________  ________     
|\  \     |\  \|\   ___  \|\  \|\  \ |\  \     |\   __  \|\   __  \    
\ \  \    \ \  \ \  \\ \  \ \  \/  /|\ \  \    \ \  \|\  \ \  \|\ /_   
 \ \  \    \ \  \ \  \\ \  \ \   ___  \ \  \    \ \   __  \ \   __  \  
  \ \  \____\ \  \ \  \\ \  \ \  \\ \  \ \  \____\ \  \ \  \ \  \|\  \ 
   \ \_______\ \__\ \__\\ \__\ \__\\ \__\ \_______\ \__\ \__\ \_______\
    \|_______|\|__|\|__| \|__|\|__| \|__|\|_______|\|__|\|__|\|_______|
```

> 每个程序员都用过链接器，但很少有人真正理解它。
> 
> 在这个实验中，你将亲手实现一个链接器，揭开程序是如何被"拼接"在一起的秘密。我们设计了一个友好的目标文件格式（FLE），让你可以专注于理解链接的核心概念。

> [!WARNING]
> 这是 LinkLab 的第一个版本，可能存在一些问题。如果你：
> - 发现了 bug，请[提交 issue](https://github.com/RUCICS/LinkLab-2024/issues)（记得遵循 issue 模板）
> - 有任何疑问，请在[讨论区](https://github.com/RUCICS/LinkLab-2024/discussions)提出
> - 想要改进实验，欢迎提交 PR
>
> 预计耗时：
> - 基础任务：10-15 小时
> - 进阶内容：5-10 小时
>
> 我们会认真对待每一个反馈！

[![GitHub Issues](https://img.shields.io/github/issues/RUCICS/LinkLab-2024?style=for-the-badge&logo=github)](https://github.com/RUCICS/LinkLab-2024/issues)

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/RUCICS/LinkLab-2024.git
cd LinkLab-2024

# 构建项目
make

# 运行测试（此时应该会失败，这是正常的）
make test_1  # 运行任务一的测试
make test    # 运行所有测试
```

## 环境要求

- 操作系统：Linux（推荐 Ubuntu 22.04 或更高版本）
- 编译器：g++ 12.0.0 或更高版本（需要 C++20）
- Python 3.6+
- Git

请使用 Git 管理你的代码，养成经常提交的好习惯。

## 项目结构

```
LinkLab-2024/
├── include/                    # 头文件
│   └── fle.hpp                # FLE 格式定义（请仔细阅读）
├── src/
│   ├── base/                  # 基础框架（助教提供）
│   │   ├── cc.cpp            # 编译器前端，生成 FLE 文件
│   │   └── exec.cpp          # 程序加载器，运行生成的程序
│   └── student/              # 你需要完成的代码
│       ├── nm.cpp            # 任务一：符号表查看器
│       └── ld.cpp            # 任务二~六：链接器主程序
└── tests/                    # 测试用例
    └── cases/               # 按任务分类的测试
        ├── 1-nm-test/      # 任务一：符号表显示
        ├── 2-ld-test/      # 任务二：基础链接
        └── ...             # 更多测试用例
```

每个任务都配有完整的测试用例，包括：
- 源代码：用于生成测试输入
- 期望输出：用于验证你的实现
- 配置文件：定义测试参数

你可以：
1. 阅读测试代码了解具体要求
2. 运行测试检查实现是否正确
3. 修改测试探索更多可能性

## 任务零：理解目标文件格式

在开始写链接器之前，我们需要先理解它处理的文件格式。传统的 ELF 格式虽然强大，但细节太多。为了让你专注于链接的核心概念，我们设计了 FLE (Friendly Linking Executable) 格式。

让我们通过一个简单的例子来了解它：

```c
int message[2] = {1, 2};  // 全局数组

static int helper(int x) { // 静态函数
    return x + message[0];
}

int main() {             // 程序入口
    return helper(42);
}
```

编译器会把它转换成这样的 FLE 文件：

```json
{
    "type": ".obj",           // 这是一个目标文件
    ".text": [               // 代码段
        "🔢: 55 48 89 e5",    // 机器码
        "🏷️: helper 0",       // 局部符号
        "❓: .rel(message)",   // 需要重定位
        "🔢: 48 8b 00",       // 加载 message[0]
        "🔢: 01 f8",          // 加到参数 x
        "🔢: c3",             // 返回
        "📤: main 16",        // 全局符号
        "❓: .rel(helper)",    // 需要重定位
        "🔢: c3"              // 返回
    ],
    ".data": [               // 数据段
        "📤: message 0",      // 全局变量
        "🔢: 01 00 00 00",    // 1
        "🔢: 02 00 00 00"     // 2
    ]
}
```

FLE 使用表情符号来标记不同类型的信息：
- 🔢 原始的机器码或数据
- 🏷️ 文件内部的局部符号
- 📤 可以被其他文件引用的全局符号
- 📎 可以被覆盖的弱符号
- ❓ 需要重定位的地方

这些信息在内存中用 C++ 结构体表示（定义在 `include/fle.hpp` 中）：

```cpp
struct FLEObject {
    std::string type;                             // 文件类型
    std::map<std::string, FLESection> sections;   // 各个段
    std::vector<Symbol> symbols;                  // 符号表
    std::vector<ProgramHeader> phdrs;            // 程序头
    size_t entry = 0;                           // 入口点
};
```

注意，FLE 文件的格式和内存中的表示有两个重要区别：
1. 文件中的信息是内联的（比如，符号定义的地方直接用 📤 标记，而不是分离的符号表）
2. 没有占位字节（需要重定位的地方直接用 ❓ 标记，而不是用 0 占位）

在接下来的任务中，你将逐步实现处理这种格式的工具链，从最基本的符号表查看器开始，最终实现一个完整的链接器。

准备好了吗？让我们开始第一个任务！

## 任务一：窥探程序的符号表

你有没有遇到过这样的错误？
```
undefined reference to `printf'
multiple definition of `main'
```

这些都与符号（symbol）有关。符号就像程序中的"名字"，代表了函数、变量等。让我们通过一个例子来理解：

```c
static int counter = 0;        // 静态变量：文件内可见
int shared = 42;              // 全局变量：其他文件可见
extern void print(int x);     // 外部函数：需要其他文件提供

void count() {                // 全局函数
    counter++;                // 访问静态变量
    print(counter);           // 调用外部函数
}
```

这段代码中包含了几种不同的符号：
- `counter`：静态符号，只在文件内可见
- `shared`：全局符号，可以被其他文件引用
- `print`：未定义符号，需要在链接时找到
- `count`：全局函数符号

你的第一个任务是写一个工具（nm）来查看这些符号。对于上面的代码，它应该输出：

```
0000000000000000 b counter    # 静态变量在 bss 段
0000000000000004 D shared    # 全局变量在 data 段
0000000000000000 T count     # 全局函数在 text 段
                 U print     # 未定义的外部函数
```

每一行包含：
- 地址：符号在其所在段中的偏移量
- 类型：表示符号的类型和位置
  - 大写（T、D、B）：全局符号，分别表示在代码段、数据段、BSS段
  - 小写（t、d、b）：局部符号，分别表示在代码段、数据段、BSS段
  - U：未定义符号
- 名称：符号的名字

要实现这个工具，你需要：
1. 遍历符号表
2. 确定每个符号的类型
3. 按格式打印信息

提示：
- 使用 `std::setw` 和 `std::setfill` 格式化输出
- 根据 section 字段判断符号位置
- 未定义符号的 section 为空

### 验证
运行测试：
```bash
make test_1
```

如果看到：
```
Running test case: 1-nm-test
✓ Symbol table matches expected output
All tests passed!
```
说明任务一完成！

## 任务二：实现基础链接器

让我们从最简单的情况开始。假设有这样一个程序：
```c
// message.c
int magic = 42;    // 一个全局变量

// main.c
extern int magic;  // 声明：magic 在别处定义
int main() {
    return magic;  // 需要找到 magic 的实际位置
}
```

链接器的工作就是把这些文件"拼接"在一起。具体来说，它需要：
1. 收集所有的代码和数据
2. 解决符号之间的引用（"这个变量在那个文件里"）
3. 调整代码中的地址（因为所有东西的位置都变了）

为了让你更容易理解这个过程，我们先采用最简单的方案：把所有内容都放在一个叫 `.load` 的段里。编译器已经帮我们把源代码变成了这样的目标文件：

```json
// message.fle
{
    "type": ".obj",
    ".data": [
        "📤: magic 0",      // 全局变量 magic
        "🔢: 2a 00 00 00"   // 值：42
    ]
}

// main.fle
{
    "type": ".obj",
    ".text": [
        "📤: main 0",       // main 函数
        "🔢: 55 48 89 e5",  // 函数序言
        "❓: .abs(magic)",   // 需要 magic 的地址
        "🔢: c3"            // 返回
    ]
}
```

你的任务是把这些目标文件链接成一个可执行文件：
```json
{
    "type": "exe",              // 这是一个可执行文件
    "sections": {
        ".load": {              // 所有内容都在这个段里
            "data": [...],      // 合并后的数据
            "relocs": []        // 重定位已完成，这里是空的
        }
    },
    "phdrs": [{                 // 程序头
        "name": ".load",
        "vaddr": 0x400000,     // 固定的加载地址
        "size": <总大小>,
        "flags": 7             // 可读、可写、可执行
    }]
}
```

在这个阶段，我们只需要处理最简单的重定位类型：`R_X86_64_32`（32位绝对地址）。它告诉链接器："在这里填入符号的绝对地址"。

提示：
1. 先处理最简单的情况：只有一个输入文件
2. 用 readfle 工具检查你的输出是否正确
3. 打印调试信息，帮助你理解重定位过程
4. 记得更新符号的位置信息

### 验证
运行测试：
```bash
make test_2
```

如果看到：
```
Running test case: 2-ld-test
✓ Executable file format is correct
✓ Relocation results match expected
All tests passed!
```
说明任务二完成！

## 任务三：实现相对重定位

在任务二中，我们只处理了最简单的重定位类型（R_X86_64_32），用于访问全局变量。但在实际程序中，最常见的其实是函数调用。让我们看一个例子：

```c
// lib.c
int add(int x) {    // 一个简单的函数
    return x + 1;
}

// main.c
extern int add(int);  // 声明：add 函数在别处定义
int main() {
    return add(41);   // 调用 add 函数
}
```

编译器会把它们变成：
```json
// lib.fle
{
    "type": ".obj",
    ".text": [
        "📤: add 0",        // add 函数
        "🔢: 55 48 89 e5",  // 函数序言
        "🔢: 8d 47 01",     // lea eax, [rdi+1]
        "🔢: c3"            // 返回
    ]
}

// main.fle
{
    "type": ".obj",
    ".text": [
        "📤: main 0",       // main 函数
        "🔢: 55 48 89 e5",  // 函数序言
        "🔢: bf 29 00",     // mov edi, 41
        "❓: .rel(add)",    // 调用 add 函数
        "🔢: c3"            // 返回
    ]
}
```

注意那个 `.rel(add)` —— 它会变成一条 `call` 指令，但具体怎么跳转呢？

x86-64 的 `call` 指令使用相对寻址（R_X86_64_PC32 类型的重定位）。也就是说，它存储的不是目标函数的绝对地址，而是"要跳多远"。这样的好处是：
1. 代码可以加载到内存的任何位置（位置无关）
2. 跳转指令更短（只需要 32 位偏移量）

计算公式是：
```
偏移量 = 目标地址 - (当前地址 + 指令长度)
```

比如，如果：
- `call` 指令在 0x1000
- `add` 函数在 0x1100
- `call` 指令长度是 5 字节

那么：
```
偏移量 = 0x1100 - (0x1000 + 5) = 0xFB
```

所以 `call` 指令会变成：
```
E8 FB 00 00 00   ; call add
```

你的任务是实现这种重定位。需要注意：
1. 识别 R_X86_64_PC32 类型的重定位
2. 计算正确的偏移量（记得考虑指令长度）
3. 将偏移量写入到指令中

提示：
1. `call` 指令的长度是 5 字节
2. 重定位项的 addend 通常是 -4
3. 用调试打印验证你的计算

### 验证
运行测试：
```bash
make test_3
```

如果看到：
```
Running test case: 3-relative-reloc
✓ Relative relocations are correct
All tests passed!
```
说明任务三完成！

## 任务四：处理符号冲突

在前面的任务中，我们假设每个符号只在一个地方定义。但实际编程中经常会遇到同名符号，比如：

```c
// config.c
int debug_level = 0;  // 默认配置

// main.c
int debug_level = 1;  // 自定义配置
```

这时链接器该怎么办？用哪个 `debug_level`？

为了解决这个问题，C 语言允许程序员指定符号的"强度"：
- 强符号（GLOBAL）：普通的全局符号
- 弱符号（WEAK）：可以被覆盖的备选项

最常见的用法是用弱符号来提供默认实现：
```c
// logger.c
__attribute__((weak))        // 标记为弱符号
void init_logger() {         // 默认的初始化函数
    // 使用默认配置
}

// main.c
void init_logger() {         // 强符号会覆盖默认实现
    // 使用自定义配置
}
```

链接规则很简单：
1. 强符号必须唯一
   ```c
   // error.c
   int x = 1;        // 强符号
   int x = 2;        // 错误：重复定义！
   ```

2. 强符号优先于弱符号
   ```c
   // a.c
   __attribute__((weak)) int v = 1;  // 弱符号
   // b.c
   int v = 2;                        // 强符号，这个会被使用
   ```

3. 多个弱符号时取第一个
   ```c
   // first.c
   __attribute__((weak)) int mode = 1;  // 这个会被使用
   // second.c
   __attribute__((weak)) int mode = 2;  // 这个会被忽略
   ```

你的任务是实现这些规则。具体来说：
1. 收集所有符号
2. 检查是否有重复的强符号（报错）
3. 在强弱符号冲突时选择强符号
4. 在多个弱符号时选择第一个

提示：
1. 使用 `std::map` 按名字分组符号
2. 仔细检查每个符号的 `SymbolType`
3. 保持良好的错误信息

### 验证
运行测试：
```bash
make test_4
```

如果看到：
```
Running test case: 4-symbol-binding
✓ Symbol resolution follows binding rules
All tests passed!
```
说明任务四完成！

## 任务五：处理 64 位地址

到目前为止，我们处理的都是 32 位的地址（R_X86_64_32 和 R_X86_64_PC32）。但在 64 位系统中，有时我们需要完整的 64 位地址。比如：

```c
// 一个全局数组
int numbers[] = {1, 2, 3, 4};

// 一个指向这个数组的指针
int *ptr = &numbers[0];  // 需要完整的 64 位地址！
```

为什么这里需要 64 位地址？因为：
1. 指针本身是 64 位的（8字节）
2. 程序可能被加载到高地址区域
3. 32 位地址最多只能访问 4GB 空间

这种情况下，编译器会生成一个新的重定位类型（R_X86_64_64）：
```json
{
    "type": ".obj",
    ".data": [
        "📤: numbers 0",
        "🔢: 01 00 00 00",    // 1
        "🔢: 02 00 00 00",    // 2
        "🔢: 03 00 00 00",    // 3
        "🔢: 04 00 00 00",    // 4
        "📤: ptr 16",
        "❓: .abs64(numbers)"  // 需要 numbers 的完整地址
    ]
}
```

注意那个 `.abs64` —— 这是一个新的重定位类型（R_X86_64_64），它告诉链接器："在这里填入符号的完整 64 位地址"。

你的任务是支持这种重定位。需要注意：
1. 写入完整的 64 位地址（8字节）
2. 考虑字节序（x86 是小端）
3. 地址要加上基地址（0x400000）

提示：
1. 使用 uint64_t 存储地址
2. 小心整数溢出
3. 用 readfle 检查输出

### 验证
运行测试：
```bash
make test_5
```

如果看到：
```
Running test case: 5-abs64-reloc
✓ 64-bit relocations are correct
All tests passed!
```
说明任务五完成！

## 任务六：分离代码和数据

到目前为止，我们把所有内容都放在一个段里。这看起来很方便，但实际上非常危险。看看这个例子：

```c
void hack() {
    // 1. 修改代码
    void* code = (void*)hack;
    *(char*)code = 0x90;     // 如果代码段可写，程序可以被篡改！

    // 2. 执行数据
    char shellcode[] = {...}; // 一些恶意代码
    ((void(*)())shellcode)(); // 如果数据段可执行，这就是漏洞！
}
```

为了防止这些攻击，现代系统采用分段机制：
1. 代码段（.text）：只读且可执行
2. 只读数据段（.rodata）：只读
3. 数据段（.data）：可读写
4. BSS段（.bss）：可读写，但不占文件空间

编译器已经帮我们分好了段：
```json
{
    "type": ".obj",
    ".text": [
        "📤: main 0",
        "🔢: 55 48 89 e5"     // 代码
    ],
    ".rodata": [
        "📤: message 0",
        "🔢: 48 65 6c 6c 6f"  // "Hello"
    ],
    ".data": [
        "📤: counter 0",
        "🔢: 00 00 00 00"     // 已初始化数据
    ],
    ".bss": [
        "📤: buffer 0",       // 未初始化数据
        "size: 1024"          // 只记录大小
    ]
}
```

你的任务是：
1. 保持段的独立性（不要合并）
2. 设置正确的权限：
   - `.text`: r-x（读/执行）
   - `.rodata`: r--（只读）
   - `.data`: rw-（读/写）
   - `.bss`: rw-（读/写）
3. 优化内存布局：
   - 4KB 对齐（页面大小）
   - 相似权限的段放在一起
   - BSS 段不需要文件内容

最终的可执行文件应该是这样的：
```json
{
    "type": "exe",
    "sections": {
        ".text": { ... },      // 代码
        ".rodata": { ... },    // 常量
        ".data": { ... },      // 已初始化数据
        ".bss": { ... }        // 未初始化数据（只记录大小）
    },
    "phdrs": [
        {
            "name": ".text",
            "vaddr": 0x400000,
            "size": <代码大小>,
            "flags": 5        // r-x
        },
        {
            "name": ".rodata",
            "vaddr": <对齐后的地址>,
            "size": <常量大小>,
            "flags": 4        // r--
        },
        // ...其他段...
    ]
}
```

提示：
1. 段的地址要适当对齐（影响性能）
2. 注意更新所有重定位（地址都变了）
3. BSS 段只需要分配空间，不需要数据
4. 考虑把相似权限的段合并到一个程序头中

修改 `src/student/ld.cpp`，实现内存布局的优化。

### 验证
运行测试：
```bash
make test_6
```

如果看到：
```
Running test case: 6-section-perm
✓ Section permissions are correct
✓ Memory layout is properly aligned
All tests passed!
```
说明任务六完成！

## 任务七：验证与总结

恭喜你完成了所有基础任务！现在让我们验证整个链接器的功能。

### 完整性检查
```bash
make test
```

你应该看到：
```
Running all test cases...
✓ 1-nm-test: 符号表显示正确
✓ 2-single-file: 基础链接功能正常
✓ 3-relative-reloc: 相对重定位正确
✓ 4-symbol-binding: 符号解析规则正确
✓ 5-abs64-reloc: 64位地址处理正确
✓ 6-section-perm: 内存布局合理
All tests passed! Congratulations!
```

### 实验报告要求

1. 实现思路（占 60%）
   - 关键数据结构的设计选择
   - 重要算法的核心思想
   - 关键功能的实现方法

2. 问题分析（占 20%）
   - 遇到的技术难点
   - 采用的解决方案
   - 可能的优化方向

3. 总结反思（占 20%）
   - 对链接过程的理解
   - 实验的收获和建议
   - 进一步的探索方向

### 提交方式

我们使用 GitHub Classroom 进行提交：
1. 确保所有代码已提交到你的仓库
2. GitHub Actions 会自动运行测试
3. 我们将以 Actions 的输出作为评分依据

## 评分标准

基础部分（80分）：
- 任务一 (15分): 符号表显示
- 任务二 (15分): 基础链接
- 任务三 (15分): 相对重定位
- 任务四 (15分): 符号解析
- 任务五 (10分): 64位支持
- 任务六 (10分): 内存布局

附加部分（20分）：
- 代码质量（10分）：风格规范、注释清晰
- 实验报告（10分）：思路清晰、分析深入

## 调试建议

1. 仔细阅读 `include/fle.hpp` 中的数据结构定义
2. 使用 `readfle` 工具查看 FLE 文件的内容
3. 测试用例提供了很好的参考实例
4. 链接器的调试技巧：
   - 使用 `objdump` 查看生成的文件
   - 打印中间过程的重要信息
   - 分步骤实现，先确保基本功能正确

## 进阶内容

完成了基本任务后，你可以尝试：
1. 支持更多的重定位类型
2. 实现共享库加载
3. 添加符号版本控制
4. 优化链接性能
5. 支持增量链接

## 参考资料

1. [System V ABI](https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf)
2. [Linkers & Loaders](https://linker.iecc.com/)
3. [How To Write Shared Libraries](https://www.akkadia.org/drepper/dsohowto.pdf)
