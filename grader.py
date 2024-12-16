import json
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import tomli
from rich.console import Console
from rich.panel import Panel
from rich.progress import Progress, SpinnerColumn, TextColumn
from rich.table import Table

console = Console()


@dataclass
class TestResult:
    success: bool
    message: str
    time: float
    score: float

    # 添加to_dict方法便于JSON序列化
    def to_dict(self):
        return {
            "success": self.success,
            "message": self.message,
            "time": self.time,
            "score": self.score,
        }


@dataclass
class TestCase:
    path: Path
    meta: Dict[str, Any]
    run_steps: List[Dict[str, Any]]


class Grader:
    def __init__(self, json_output=False):
        self.project_root = Path.cwd()
        self.config = self._load_config()
        # 使用配置文件中的路径
        self.tests_dir = self.project_root / self.config["paths"]["tests_dir"]
        self.cases_dir = self.project_root / self.config["paths"]["cases_dir"]
        self.common_dir = self.project_root / self.config["paths"]["common_dir"]
        self.results: Dict[str, TestResult] = {}
        self.json_output = json_output
        self.console = Console(quiet=json_output)

    def _load_config(self) -> Dict[str, Any]:
        """加载全局配置文件"""
        config_path = self.project_root / "grader_config.toml"
        if not config_path.exists():
            if not self.json_output:
                self.console.print(
                    "[yellow]Warning:[/yellow] grader_config.toml not found, using defaults"
                )
            return {
                "paths": {
                    "tests_dir": "tests",
                    "cases_dir": "tests/cases",
                    "common_dir": "tests/common",
                }
            }

        with open(config_path, "rb") as f:
            return tomli.load(f)

    def run_setup_steps(self) -> bool:
        """运行配置文件中定义的所有准备步骤"""
        if "setup" not in self.config:
            return True

        for step in self.config["setup"]["steps"]:
            if not self._run_setup_step(step):
                return False
        return True

    def _run_setup_step(self, step: Dict[str, Any]) -> bool:
        """运行单个准备步骤"""
        if not self.json_output and "message" in step:
            self.console.print(f"[bold]{step['message']}[/bold]")

        try:
            if step["type"] == "compile":
                return self._run_compile_step(step)
            elif step["type"] == "command":
                return self._run_command_step(step)
            else:
                if not self.json_output:
                    self.console.print(
                        f"[red]Error:[/red] Unknown setup step type: {step['type']}"
                    )
                return False
        except Exception as e:
            if not self.json_output:
                self.console.print(f"[red]Error:[/red] Setup step failed: {str(e)}")
            else:
                print(f"Error: Setup step failed: {str(e)}", file=sys.stderr)
            return False

    def _run_compile_step(self, step: Dict[str, Any]) -> bool:
        """运行编译类型的准备步骤"""
        source = self.project_root / step["source"]
        output = self.project_root / step["output"]

        if not source.exists():
            if not self.json_output:
                self.console.print(
                    f"[red]Error:[/red] Source file {step['source']} not found"
                )
            return False

        try:
            cmd = [step["compiler"], str(source), "-o", str(output)]
            process = subprocess.run(
                cmd, cwd=self.project_root, capture_output=True, text=True
            )

            if process.returncode != 0:
                if not self.json_output:
                    self.console.print("[red]Error:[/red] Compilation failed:")
                    self.console.print(process.stderr)
                return False

            if not self.json_output and "success_message" in step:
                self.console.print(f"[green]✓[/green] {step['success_message']}")
            return True

        except Exception as e:
            if not self.json_output:
                self.console.print(f"[red]Error:[/red] Compilation failed: {str(e)}")
            return False

    def _run_command_step(self, step: Dict[str, Any]) -> bool:
        """运行命令类型的准备步骤"""
        try:
            cmd = [step["command"]]
            if "args" in step:
                if isinstance(step["args"], list):
                    cmd.extend(step["args"])
                else:
                    cmd.append(step["args"])

            process = subprocess.run(
                cmd,
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=step.get("timeout", 5.0),
            )

            if process.returncode != 0:
                if not self.json_output:
                    self.console.print("[red]Error:[/red] Command failed:")
                    self.console.print(process.stderr)
                return False

            if not self.json_output and "success_message" in step:
                self.console.print(f"[green]✓[/green] {step['success_message']}")
            return True

        except Exception as e:
            if not self.json_output:
                self.console.print(f"[red]Error:[/red] Command failed: {str(e)}")
            return False

    def load_test_cases(self, specific_test: Optional[str] = None) -> List[TestCase]:
        """加载测试用例，如果指定了特定测试，则只加载该测试"""
        if specific_test:
            test_path = self.cases_dir / specific_test
            if not test_path.exists() or not (test_path / "config.toml").exists():
                if not self.json_output:
                    self.console.print(
                        f"[red]Error:[/red] Test case '{specific_test}' not found"
                    )
                else:
                    print(
                        f"Error: Test case '{specific_test}' not found", file=sys.stderr
                    )
                sys.exit(1)
            return [self._load_single_test(test_path)]

        # 检查测试用例目录是否存在
        if not self.cases_dir.exists():
            if not self.json_output:
                self.console.print("[red]Error:[/red] tests/cases directory not found")
            else:
                print("Error: tests/cases directory not found", file=sys.stderr)
            sys.exit(1)

        # 加载所有测试用例
        test_cases = []
        for test_dir in sorted(self.cases_dir.iterdir()):
            if test_dir.is_dir() and (test_dir / "config.toml").exists():
                test_cases.append(self._load_single_test(test_dir))

        # 如果没有找到任何测试用例
        if not test_cases:
            if not self.json_output:
                self.console.print(
                    "[red]Error:[/red] No test cases found in tests/cases/"
                )
            else:
                print("Error: No test cases found in tests/cases/", file=sys.stderr)
            sys.exit(1)

        return test_cases

    def _load_single_test(self, test_path: Path) -> TestCase:
        """加载单个测试用例的配置"""
        try:
            with open(test_path / "config.toml", "rb") as f:
                config = tomli.load(f)

            # 验证必要的配置项
            if "meta" not in config:
                raise ValueError("Missing 'meta' section in config")
            if "name" not in config["meta"]:
                raise ValueError("Missing 'name' in meta section")
            if "score" not in config["meta"]:
                raise ValueError("Missing 'score' in meta section")
            if "run" not in config:
                raise ValueError("Missing 'run' section in config")

            return TestCase(
                path=test_path, meta=config["meta"], run_steps=config["run"]
            )
        except Exception as e:
            if not self.json_output:
                self.console.print(
                    f"[red]Error:[/red] Failed to load test '{test_path.name}': {str(e)}"
                )
            else:
                print(
                    f"Error: Failed to load test '{test_path.name}': {str(e)}",
                    file=sys.stderr,
                )
            sys.exit(1)

    def _check_output(
        self,
        step: Dict[str, Any],
        output: str,
        error: str,
        return_code: int,
        test_dir: Path,
    ) -> Tuple[bool, str]:
        """检查命令的输出是否符合预期"""
        check = step.get("check", {})
        if not check:
            return True, "No check specified"

        # 检查返回值
        if "return_code" in check and return_code != check["return_code"]:
            return (
                False,
                f"Expected return code {check['return_code']}, got {return_code}",
            )

        # 检查文件是否存在
        if "files" in check:
            for file_path in check["files"]:
                resolved_path = Path(self._resolve_path(file_path, test_dir))
                if not resolved_path.exists():
                    return False, f"Required file '{file_path}' not found"

        # Special Judge
        if "special_judge" in check:
            judge_script = test_dir / check["special_judge"]
            if not judge_script.exists():
                return False, f"Special judge script {check['special_judge']} not found"

            input_data = {
                "stdout": output,
                "stderr": error,
                "return_code": return_code,
                "test_dir": str(test_dir),
            }

            try:
                process = subprocess.run(
                    [sys.executable, str(judge_script)],
                    input=json.dumps(input_data).encode(),
                    capture_output=True,
                    text=True,
                )
                result = json.loads(process.stdout)
                return result["success"], result.get("message", "No message provided")
            except Exception as e:
                return False, f"Special judge failed: {str(e)}"

        # 检查标准输出
        if "stdout" in check:
            expect_file = test_dir / check["stdout"]
            if not expect_file.exists():
                return False, f"Expected output file {check['stdout']} not found"
            with open(expect_file) as f:
                expected = f.read()
            if check.get("ignore_whitespace", False):
                output = " ".join(output.split())
                expected = " ".join(expected.split())
            if output.rstrip() != expected.rstrip():
                return False, "Output does not match expected content"

        # 检查标准错误
        if "stderr" in check:
            expect_file = test_dir / check["stderr"]
            if not expect_file.exists():
                return False, f"Expected error file {check['stderr']} not found"
            with open(expect_file) as f:
                expected = f.read()
            if check.get("ignore_whitespace", False):
                error = " ".join(error.split())
                expected = " ".join(expected.split())
            if error.rstrip() != expected.rstrip():
                return False, "Error output does not match expected content"

        # 正则表达式匹配
        if "stdout_pattern" in check:
            if not re.search(check["stdout_pattern"], output, re.MULTILINE):
                return False, f"Output does not match pattern {check['stdout_pattern']}"

        if "stderr_pattern" in check:
            if not re.search(check["stderr_pattern"], error, re.MULTILINE):
                return (
                    False,
                    f"Error output does not match pattern {check['stderr_pattern']}",
                )

        return True, "All checks passed"

    def _resolve_path(self, path: str, test_dir: Path) -> str:
        """解析路径中的变量"""
        build_dir = test_dir / "build"
        build_dir.mkdir(exist_ok=True)

        replacements = {
            "${test_dir}": str(test_dir),
            "${common_dir}": str(self.common_dir),
            "${root_dir}": str(self.project_root),
            "${build_dir}": str(build_dir),
        }

        for var, value in replacements.items():
            path = path.replace(var, value)
        return path

    def run_test(self, test: TestCase) -> TestResult:
        """运行单个测试用例"""
        start_time = time.time()

        try:
            # 清理和创建构建目录
            build_dir = test.path / "build"
            if build_dir.exists():
                for file in build_dir.iterdir():
                    if file.is_file():
                        file.unlink()
            build_dir.mkdir(exist_ok=True)

            if not self.json_output:
                # 只在非JSON模式下显示进度
                with Progress(
                    SpinnerColumn(),
                    TextColumn("[progress.description]{task.description}"),
                    console=self.console,
                ) as progress:
                    total_steps = len(test.run_steps)
                    task = progress.add_task(
                        f"Running {test.meta['name']} [0/{total_steps}]...",
                        total=total_steps,
                    )
                    result = self._execute_test_steps(test, progress, task)
                    progress.update(task, completed=total_steps)
                    return result
            else:
                # JSON模式下直接执行测试
                return self._execute_test_steps(test)

        except subprocess.TimeoutExpired:
            return TestResult(
                success=False,
                message="Timeout",
                time=time.time() - start_time,
                score=0,
            )
        except Exception as e:
            return TestResult(
                success=False,
                message=f"Error: {str(e)}",
                time=time.time() - start_time,
                score=0,
            )

    def _execute_test_steps(
        self, test: TestCase, progress=None, task=None
    ) -> TestResult:
        """执行测试步骤的具体逻辑"""
        start_time = time.time()
        total_steps = len(test.run_steps)

        for i, step in enumerate(test.run_steps, 1):
            if (progress is not None) and (task is not None):
                step_name = step.get("name", step["command"])
                progress.update(
                    task,
                    description=f"Running {test.meta['name']} [{i}/{total_steps}]: {step_name}",
                    completed=i,
                )

            # 处理命令和参数中的路径变量
            cmd = [self._resolve_path(step["command"], test.path)]
            args = []
            for arg in step.get("args", []):
                args.append(self._resolve_path(str(arg), test.path))
            stdin_data = None

            # 处理标准输入
            if "stdin" in step:
                stdin_file = test.path / step["stdin"]
                if not stdin_file.exists():
                    return TestResult(
                        success=False,
                        message=f"Input file {step['stdin']} not found",
                        time=time.time() - start_time,
                        score=0,
                    )
                with open(stdin_file) as f:
                    stdin_data = f.read()

            # 运行命令
            process = subprocess.run(
                cmd + args,
                cwd=test.path,
                input=stdin_data,
                capture_output=True,
                text=True,
                timeout=step.get("timeout", 5.0),
            )

            # 检查输出
            if "check" in step:
                success, message = self._check_output(
                    step,
                    process.stdout,
                    process.stderr,
                    process.returncode,
                    test.path,
                )
                if not success:
                    return TestResult(
                        success=False,
                        message=f"Step {i}/{total_steps} '{step.get('name', cmd[0])}' failed: {message}",
                        time=time.time() - start_time,
                        score=0,
                    )

        return TestResult(
            success=True,
            message="All steps completed successfully",
            time=time.time() - start_time,
            score=test.meta["score"],
        )

    def run_all_tests(self, specific_test: Optional[str] = None):
        """运行所有测试用例或指定的测试用例"""
        if not self.run_setup_steps():
            sys.exit(1)

        test_cases = self.load_test_cases(specific_test)
        if not self.json_output:
            self.console.print(
                f"\n[bold]Running {len(test_cases)} test cases...[/bold]\n"
            )

        total_score = 0
        max_score = 0
        test_results = []

        for test in test_cases:
            result = self.run_test(test)
            self.results[test.path.name] = result

            test_results.append(
                {
                    "name": test.meta["name"],
                    "success": result.success,
                    "time": round(result.time, 2),
                    "score": result.score,
                    "max_score": test.meta["score"],
                    "message": result.message,
                }
            )

            total_score += result.score
            max_score += test.meta["score"]

        if self.json_output:
            # 输出JSON格式结果
            json_result = {
                "total_score": round(total_score, 1),
                "max_score": round(max_score, 1),
                "percentage": round(total_score / max_score * 100, 1),
                "tests": test_results,
            }
            print(json.dumps(json_result, ensure_ascii=False))
        else:
            # 原有的表格输出逻辑保持不变
            table = Table(show_header=True, header_style="bold")
            table.add_column("Test Case", style="cyan")
            table.add_column("Result", justify="center")
            table.add_column("Time", justify="right")
            table.add_column("Score", justify="right")
            table.add_column("Message")

            # 使用已经运行的测试结果，而不是重新运行测试
            for test, result in zip(test_cases, test_results):
                table.add_row(
                    test.meta["name"],
                    "[green]PASS[/green]" if result["success"] else "[red]FAIL[/red]",
                    f"{result['time']:.2f}s",
                    f"{result['score']:.1f}/{result['max_score']}",
                    result["message"],
                )

            self.console.print(table)

            summary = Panel(
                f"[bold]Total Score: {total_score:.1f}/{max_score:.1f} "
                f"({total_score/max_score*100:.1f}%)[/bold]",
                border_style="green" if total_score == max_score else "yellow",
            )
            self.console.print()
            self.console.print(summary)
            self.console.print()

    def list_test_cases(self, specific_test: Optional[str] = None):
        """列出所有测试用例的信息而不执行它们"""
        test_cases = self.load_test_cases(specific_test)
        
        if self.json_output:
            # JSON格式输出
            cases_info = [
                {
                    "name": test.meta["name"],
                    "description": test.meta.get("description", "No description"),
                    "score": test.meta["score"],
                    "path": str(test.path.name)
                }
                for test in test_cases
            ]
            print(json.dumps({"test_cases": cases_info}, ensure_ascii=False))
        else:
            # 表格形式输出
            table = Table(show_header=True, header_style="bold")
            table.add_column("Test Case", style="cyan")
            table.add_column("Description")
            table.add_column("Score", justify="right")
            table.add_column("Path")

            for test in test_cases:
                table.add_row(
                    test.meta["name"],
                    test.meta.get("description", "No description"),
                    f"{test.meta['score']:.1f}",
                    str(test.path.name)
                )

            self.console.print("\n[bold]Available Test Cases:[/bold]\n")
            self.console.print(table)
            self.console.print()


def check_dependencies():
    """Check if required dependencies are installed"""
    try:
        import rich
        import tomli
    except ImportError as e:
        print(
            f"Error: Missing required dependencies. Please use run_grader.py to run this script.\nDetails: {str(e)}",
            file=sys.stderr,
        )
        sys.exit(1)


def main():
    """主函数"""
    import argparse

    # 添加依赖检查
    check_dependencies()

    parser = argparse.ArgumentParser(description="Grade student submissions")
    parser.add_argument(
        "--json", action="store_true", help="Output results in JSON format"
    )
    parser.add_argument(
        "--list", action="store_true", help="List all test cases without running them"
    )
    parser.add_argument("test", nargs="?", help="Specific test to run")
    args = parser.parse_args()

    grader = Grader(json_output=args.json)
    
    if args.list:
        grader.list_test_cases(args.test)
    else:
        grader.run_all_tests(args.test)


if __name__ == "__main__":
    main()
