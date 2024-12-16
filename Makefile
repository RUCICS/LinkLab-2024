CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -I./include
REQUIRED_CXX_STANDARD = 20

# 源文件
BASE_SRCS = src/base/main.cpp src/base/cc.cpp src/base/exec.cpp
STUDENT_SRCS = src/student/ld.cpp src/student/nm.cpp src/student/objdump.cpp src/student/readfle.cpp

# 所有源文件
SRCS = $(BASE_SRCS) $(STUDENT_SRCS)

# 目标文件
OBJS = $(SRCS:.cpp=.o)

# 基础可执行文件
BASE_EXEC = fle_base

# 工具名称
TOOLS = cc ld nm objdump readfle exec

# 检查编译器版本和标准支持
check_compiler:
	@echo "Checking compiler configuration..."
	@echo "Current compiler: $$($(CXX) --version | head -n 1)"
	@if ! $(CXX) -std=c++$(REQUIRED_CXX_STANDARD) -dM -E - < /dev/null > /dev/null 2>&1; then \
		echo "Error: $(CXX) does not support C++$(REQUIRED_CXX_STANDARD)"; \
		exit 1; \
	fi
	@echo "Compiler supports C++$(REQUIRED_CXX_STANDARD) ✓"
	@echo "Compiler check completed"
	@echo "------------------------"

# 默认目标
all: check_compiler $(TOOLS)

# 先编译基础可执行文件
$(BASE_EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# 为每个工具创建符号链接
$(TOOLS): $(BASE_EXEC)
	@if [ ! -L $@ ] || [ ! -e $@ ]; then \
		ln -sf $(BASE_EXEC) $@; \
	fi

# 编译源文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# 清理编译产物
clean:
	rm -f $(OBJS) $(BASE_EXEC) $(TOOLS)
	rm -rf tests/cases/*/build

# 运行测试
test: all
	python3 run_grader.py

# 运行特定测试
test_%: all
	@echo "Running test $*..."
	python3 run_grader.py $*

.PHONY: all clean test check_compiler