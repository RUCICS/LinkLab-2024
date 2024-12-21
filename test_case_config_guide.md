# 测试点配置指南

本文档详细说明了测试点配置文件的格式和用法。每个测试点都需要在其目录下包含一个 `config.toml` 文件，用于定义测试的元数据和运行步骤。

## 配置文件结构

配置文件使用 TOML 格式，主要包含两个主要部分：`meta` 和 `run`。

### meta 部分

`meta` 部分包含测试点的基本信息：

```toml
[meta]
name = "测试点名称"           # 必需：测试点的显示名称
score = 10.0                # 必需：测试点的满分分值
description = "测试描述"     # 可选：测试点的详细描述
```

### run 部分

`run` 部分定义了测试的执行步骤，是一个数组，每个元素代表一个执行步骤：

```toml
[[run]]
name = "步骤名称"           # 可选：步骤的描述性名称
command = "要执行的命令"     # 必需：要执行的命令
args = ["参数1", "参数2"]   # 可选：命令的参数列表
timeout = 5.0              # 可选：步骤超时时间（秒），默认为 5.0
stdin = "input.txt"        # 可选：标准输入文件的路径
score = 5.0               # 可选：该步骤的分值，用于分步骤评分
must_pass = true          # 可选：该步骤是否必须通过才能继续执行，默认为 true

[run.check]               # 可选：输出检查配置
```

### 分数计算方式

测试点的分数计算有两种模式：

1. **整体评分模式**（默认）：
   - 不在任何步骤中指定 `score`
   - 所有步骤全部通过时，获得 `meta.score` 指定的满分
   - 任何步骤失败时，得 0 分

2. **分步骤评分模式**：
   - 在任意步骤中指定了 `score`
   - 每个步骤独立计分，通过该步骤即可获得该步骤的分数
   - 最终分数为所有通过步骤的分数之和
   - 步骤分数总和不需要等于 `meta.score`

### 路径变量

在配置文件中，可以使用以下路径变量，它们会在运行时被自动替换：

- `${test_dir}`: 当前测试点目录的路径
- `${common_dir}`: 公共测试文件目录的路径
- `${root_dir}`: 项目根目录的路径
- `${build_dir}`: 测试点的构建目录路径

### 检查配置

每个运行步骤可以包含 `check` 部分，用于验证命令的执行结果：

```toml
[run.check]
return_code = 0                    # 可选：期望的返回值
files = ["expected_file.txt"]      # 可选：检查这些文件是否存在
stdout = "expected_output.txt"     # 可选：标准输出与此文件比较
stderr = "expected_error.txt"      # 可选：标准错误与此文件比较
stdout_pattern = "正则表达式"      # 可选：标准输出需匹配的正则表达式
stderr_pattern = "正则表达式"      # 可选：标准错误需匹配的正则表达式
ignore_whitespace = false         # 可选：比较时是否忽略空白字符差异
special_judge = "judge.py"        # 可选：特判程序的路径
```

### Special Judge

如果使用特判程序（`special_judge`），该程序需要：

1. 接受 JSON 格式的输入，包含以下字段：
   - `stdout`: 命令的标准输出
   - `stderr`: 命令的标准错误
   - `return_code`: 命令的返回值
   - `test_dir`: 测试点目录的路径
   - `max_score`: 当前步骤的满分值（即步骤配置中的 `score` 值）

2. 输出 JSON 格式的结果，包含以下字段：
   - `success`: 布尔值，表示测试是否通过
   - `message`: 字符串，测试结果的说明信息
   - `score`: 数字，可选，特判给出的得分。如果不提供，通过时得满分，不通过得0分。
     注意：这个分数只能针对当前步骤，且不能超过步骤的满分值。

特判程序可以通过返回 `score` 字段来实现更灵活的评分。例如，可以根据输出的正确性给出部分分数。

### 特判打分示例

```python
# judge.py
import json
import sys

def judge():
    # 从标准输入读取测试数据
    input_data = json.load(sys.stdin)
    stdout = input_data["stdout"]
    max_score = input_data["max_score"]  # 获取当前步骤的满分值
    
    # 假设我们要根据输出中正确答案的数量给分
    correct_count = 0
    total_count = 10
    answers = stdout.strip().split("\n")
    expected = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10"]
    
    for ans, exp in zip(answers, expected):
        if ans == exp:
            correct_count += 1
    
    # 计算得分（按正确率乘以满分）
    score = (correct_count / total_count) * max_score
    
    # 返回结果
    result = {
        "success": correct_count > 0,  # 只要有一个正确就算通过
        "message": f"Correct: {correct_count}/{total_count}",
        "score": score  # 返回实际得分（不会超过步骤满分）
    }
    print(json.dumps(result))

if __name__ == "__main__":
    judge()
```

对应的测试配置示例：

```toml
[meta]
name = "特判打分示例"
score = 10.0  # 测试点总分
description = "使用特判程序实现部分给分"

[[run]]
name = "编译程序"
command = "gcc"
args = ["-o", "${build_dir}/program", "main.c"]
score = 3.0  # 编译步骤满分
timeout = 10.0

[run.check]
return_code = 0
files = ["${build_dir}/program"]

[[run]]
name = "运行程序"
command = "${build_dir}/program"
score = 7.0  # 运行步骤满分

[run.check]
special_judge = "judge.py"  # 特判程序将决定这个步骤的实际得分（0-7分）
```

在这个示例中：
1. 编译步骤使用普通检查，通过得3分，失败得0分
2. 运行步骤使用特判程序：
   - 满分是7分
   - 特判程序根据输出的正确率给出0-7分之间的分数
   - 最终分数 = 编译得分 + 特判得分

### 步骤执行控制

每个步骤可以通过 `must_pass` 选项控制其失败时的行为：

1. `must_pass = true`（默认值）：
   - 如果步骤失败（包括超时），立即终止测试
   - 在分步骤评分模式下，保留之前步骤的得分
   - 在整体评分模式下，整个测试得0分

2. `must_pass = false`：
   - 如果步骤失败，继续执行后续步骤
   - 在分步骤评分模式下，该步骤得0分，但不影响其他步骤的得分
   - 在整体评分模式下，整个测试仍然得0分

这个选项在以下场景特别有用：
- 当某些步骤是可选的，失败不应影响后续测试
- 当你想测试多个独立的功能点，即使其中一个失败也要继续测试其他点
- 当你想收集所有测试点的结果，而不是在第一个失败时就停止

## 完整示例

### 整体评分模式示例

```toml
[meta]
name = "整体评分示例"
score = 10.0
description = "所有步骤通过得满分，任一步骤失败得0分"

[[run]]
name = "编译程序"
command = "gcc"
args = ["-o", "${build_dir}/program", "main.c"]
timeout = 10.0

[run.check]
return_code = 0
files = ["${build_dir}/program"]

[[run]]
name = "运行程序"
command = "${build_dir}/program"
stdin = "input.txt"

[run.check]
stdout = "expected_output.txt"
return_code = 0
```

### 分步骤评分模式示例

```toml
[meta]
name = "分步骤评分示例"
score = 10.0  # 在分步骤评分模式下，这个值仅用于显示
description = "每个步骤独立计分"

[[run]]
name = "编译程序"
command = "gcc"
args = ["-o", "${build_dir}/program", "main.c"]
score = 3.0  # 编译通过得3分
timeout = 10.0

[run.check]
return_code = 0
files = ["${build_dir}/program"]

[[run]]
name = "运行程序"
command = "${build_dir}/program"
stdin = "input.txt"
score = 7.0  # 运行通过得7分

[run.check]
stdout = "expected_output.txt"
return_code = 0
```

### 步骤执行控制示例

```toml
[meta]
name = "步骤执行控制示例"
score = 10.0
description = "展示步骤执行控制的用法"

[[run]]
name = "必须通过的编译步骤"
command = "gcc"
args = ["-o", "${build_dir}/program", "main.c"]
score = 2.0
must_pass = true  # 默认值，编译失败就停止测试
timeout = 10.0

[run.check]
return_code = 0

[[run]]
name = "可选的性能测试"
command = "${build_dir}/program"
args = ["--performance-test"]
score = 3.0
must_pass = false  # 即使性能测试失败也继续后续测试
timeout = 1.0  # 短超时用于性能测试

[run.check]
return_code = 0

[[run]]
name = "必须的功能测试"
command = "${build_dir}/program"
args = ["--functional-test"]
score = 5.0
must_pass = true  # 功能测试必须通过

[run.check]
stdout = "expected_output.txt"
```

在这个示例中：
1. 编译步骤必须通过，否则后续测试无法进行
2. 性能测试可以失败，不影响功能测试的执行
3. 功能测试必须通过才算完整完成测试

## 注意事项

1. 每个测试点必须有自己的目录，且目录中必须包含 `config.toml` 文件
2. `meta` 部分的 `name` 和 `score` 字段是必需的
3. `run` 部分至少需要包含一个执行步骤
4. 每个执行步骤必须指定 `command`
5. 如果使用 `special_judge`，确保特判程序能正确处理输入输出
6. 所有路径都相对于测试点目录
7. 构建目录 (`${build_dir}`) 会在每次测试开始前自动清理
8. 在同一个测试点中，不要混用整体评分和分步骤评分两种模式
9. 特判程序的 `score` 返回值不应超过步骤配置中的 `score` 值
10. 当使用 `must_pass = false` 时，要注意确保后续步骤不依赖于该步骤的成功执行