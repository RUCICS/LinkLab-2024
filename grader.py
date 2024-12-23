import argparse
import json
import re
import subprocess
import sys
import time
import venv
from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Protocol, Tuple


def create_venv(venv_path):
    print("Creating virtual environment...", flush=True)
    venv.create(venv_path, with_pip=True)


def install_requirements(venv_path):
    pip_path = venv_path / ("Scripts" if sys.platform == "win32" else "bin") / "pip"
    requirements_path = Path(__file__).parent / "requirements.txt"
    print("Installing dependencies...", flush=True)
    subprocess.run([str(pip_path), "install", "-r", str(requirements_path)], check=True)


def ensure_venv():
    import importlib.util

    # 检查必需的包是否已安装
    if (
        importlib.util.find_spec("rich") is not None
        and importlib.util.find_spec("tomli") is not None
    ):
        return True

    # 如果缺少包,创建虚拟环境并安装
    venv_dir = Path(__file__).parent / ".venv"
    python_path = (
        venv_dir / ("Scripts" if sys.platform == "win32" else "bin") / "python"
    )

    if not venv_dir.exists():
        create_venv(venv_dir)
        install_requirements(venv_dir)

    subprocess.run([str(python_path), __file__] + sys.argv[1:])
    return False


if __name__ == "__main__":
    if not ensure_venv():
        sys.exit(0)


import tomli  # noqa: E402
from rich.console import Console  # noqa: E402
from rich.panel import Panel  # noqa: E402
from rich.progress import Progress, SpinnerColumn, TextColumn  # noqa: E402
from rich.table import Table  # noqa: E402


@dataclass
class TestResult:
    success: bool
    message: str
    time: float
    score: float
    max_score: float
    step_scores: List[Tuple[str, float, float]] = None
    error_details: Optional[Dict[str, Any]] = None

    @property
    def status(self) -> str:
        if not self.success:
            return "FAIL"
        if self.score == self.max_score:
            return "PASS"
        return "PARTIAL"

    def to_dict(self):
        return {
            "success": self.success,
            "status": self.status,
            "message": self.message,
            "time": self.time,
            "score": self.score,
            "max_score": self.max_score,
            "step_scores": self.step_scores,
            "error_details": self.error_details,
        }


@dataclass
class TestCase:
    path: Path
    meta: Dict[str, Any]
    run_steps: List[Dict[str, Any]]


class Config:
    def __init__(self, project_root: Path):
        self.project_root = project_root
        self._config = self._load_config()

    def _load_config(self) -> Dict[str, Any]:
        config_path = self.project_root / "grader_config.toml"
        if not config_path.exists():
            return {
                "paths": {
                    "tests_dir": "tests",
                    "cases_dir": "tests/cases",
                    "common_dir": "tests/common",
                }
            }
        with open(config_path, "rb") as f:
            return tomli.load(f)

    @property
    def paths(self) -> Dict[str, Path]:
        return {
            "tests_dir": self.project_root / self._config["paths"]["tests_dir"],
            "cases_dir": self.project_root / self._config["paths"]["cases_dir"],
            "common_dir": self.project_root / self._config["paths"]["common_dir"],
        }

    @property
    def setup_steps(self) -> List[Dict[str, Any]]:
        return self._config.get("setup", {}).get("steps", [])


class OutputChecker(Protocol):
    def check(
        self,
        step: Dict[str, Any],
        output: str,
        error: str,
        return_code: int,
        test_dir: Path,
    ) -> Tuple[bool, str, Optional[float]]:
        pass


class StandardOutputChecker:
    def check(
        self,
        step: Dict[str, Any],
        output: str,
        error: str,
        return_code: int,
        test_dir: Path,
    ) -> Tuple[bool, str, Optional[float]]:
        check = step.get("check", {})

        # 检查返回值
        if "return_code" in check and return_code != check["return_code"]:
            return (
                False,
                f"Expected return code {check['return_code']}, got {return_code}",
                None,
            )

        # 检查文件是否存在
        if "files" in check:
            for file_path in check["files"]:
                resolved_path = Path(self._resolve_path(file_path, test_dir))
                if not resolved_path.exists():
                    return False, f"Required file '{file_path}' not found", None

        # 检查标准输出
        if "stdout" in check:
            expect_file = test_dir / check["stdout"]
            if not expect_file.exists():
                return False, f"Expected output file {check['stdout']} not found", None
            with open(expect_file) as f:
                expected = f.read()
            if check.get("ignore_whitespace", False):
                output = " ".join(output.split())
                expected = " ".join(expected.split())
            if output.rstrip() != expected.rstrip():
                return False, "Output does not match expected content", None

        # 检查标准错误
        if "stderr" in check:
            expect_file = test_dir / check["stderr"]
            if not expect_file.exists():
                return False, f"Expected error file {check['stderr']} not found", None
            with open(expect_file) as f:
                expected = f.read()
            if check.get("ignore_whitespace", False):
                error = " ".join(error.split())
                expected = " ".join(expected.split())
            if error.rstrip() != expected.rstrip():
                return False, "Error output does not match expected content", None

        return True, "All checks passed", None

    def _resolve_path(self, path: str, test_dir: Path) -> str:
        build_dir = test_dir / "build"
        build_dir.mkdir(exist_ok=True)

        replacements = {
            "${test_dir}": str(test_dir),
            "${build_dir}": str(build_dir),
        }

        for var, value in replacements.items():
            path = path.replace(var, value)
        return path


class SpecialJudgeChecker:
    def check(
        self,
        step: Dict[str, Any],
        output: str,
        error: str,
        return_code: int,
        test_dir: Path,
    ) -> Tuple[bool, str, Optional[float]]:
        check = step.get("check", {})
        if "special_judge" not in check:
            return True, "No special judge specified", None

        judge_script = test_dir / check["special_judge"]
        if not judge_script.exists():
            return (
                False,
                f"Special judge script {check['special_judge']} not found",
                None,
            )

        input_data = {
            "stdout": output,
            "stderr": error,
            "return_code": return_code,
            "test_dir": str(test_dir),
            "max_score": step.get("score", 0),
        }

        try:
            process = subprocess.run(
                [sys.executable, str(judge_script)],
                input=json.dumps(input_data),
                capture_output=True,
                text=True,
            )
            result = json.loads(process.stdout)
            if "score" in result:
                result["score"] = min(result["score"], step.get("score", 0))
            return (
                result["success"],
                result.get("message", "No message provided"),
                result.get("score", None),
            )
        except Exception as e:
            return False, f"Special judge failed: {str(e)}", None


class PatternChecker:
    def check(
        self,
        step: Dict[str, Any],
        output: str,
        error: str,
        return_code: int,
        test_dir: Path,
    ) -> Tuple[bool, str, Optional[float]]:
        check = step.get("check", {})

        if "stdout_pattern" in check:
            if not re.search(check["stdout_pattern"], output, re.MULTILINE):
                return (
                    False,
                    f"Output does not match pattern {check['stdout_pattern']}",
                    None,
                )

        if "stderr_pattern" in check:
            if not re.search(check["stderr_pattern"], error, re.MULTILINE):
                return (
                    False,
                    f"Error output does not match pattern {check['stderr_pattern']}",
                    None,
                )

        return True, "All pattern checks passed", None


class CompositeChecker:
    def __init__(self):
        self.checkers = [
            StandardOutputChecker(),
            SpecialJudgeChecker(),
            PatternChecker(),
        ]

    def check(
        self,
        step: Dict[str, Any],
        output: str,
        error: str,
        return_code: int,
        test_dir: Path,
    ) -> Tuple[bool, str, Optional[float]]:
        for checker in self.checkers:
            success, message, score = checker.check(
                step, output, error, return_code, test_dir
            )
            if not success:
                return success, message, score
        return True, "All checks passed", None


class TestRunner:
    def __init__(self, config: Config, console: Optional[Console] = None):
        self.config = config
        self.console = console
        self.checker = CompositeChecker()

    def run_test(self, test: TestCase) -> TestResult:
        start_time = time.perf_counter()
        try:
            # 清理和创建构建目录
            build_dir = test.path / "build"
            if build_dir.exists():
                for file in build_dir.iterdir():
                    if file.is_file():
                        file.unlink()
            build_dir.mkdir(exist_ok=True)

            result = None
            if self.console and not isinstance(self.console, type):
                # 在 rich 环境下显示进度条
                status_icons = {
                    "PASS": "[green]✓[/green]",
                    "PARTIAL": "[yellow]~[/yellow]",
                    "FAIL": "[red]✗[/red]",
                }
                with Progress(
                    SpinnerColumn(finished_text=status_icons["FAIL"]),
                    TextColumn("[progress.description]{task.description}"),
                    console=self.console,
                ) as progress:
                    total_steps = len(test.run_steps)
                    task = progress.add_task(
                        f"Running {test.meta['name']} [0/{total_steps}]...",
                        total=total_steps,
                    )
                    result = self._execute_test_steps(test, progress, task)
                    # 根据状态设置图标
                    progress.columns[0].finished_text = status_icons[result.status]
                    # 更新最终状态，移除Running字样，加上结果提示
                    final_status = {
                        "PASS": "[green]Passed[/green]",
                        "PARTIAL": "[yellow]Partial[/yellow]",
                        "FAIL": "[red]Failed[/red]",
                    }[result.status]
                    progress.update(
                        task,
                        completed=total_steps,
                        description=f"{test.meta['name']} [{total_steps}/{total_steps}]: {final_status}",
                    )

                # 如果测试失败，在进度显示完成后输出失败信息
                if not result.success:
                    # 获取失败的步骤信息
                    step_index = result.error_details["step"]
                    step = test.run_steps[step_index - 1]
                    cmd = [self._resolve_path(step["command"], test.path)]
                    if "args" in step:
                        cmd.extend(
                            [
                                self._resolve_path(str(arg), test.path)
                                for arg in step.get("args", [])
                            ]
                        )

                    self.console.print(
                        f"\n[red]Test '{test.meta['name']}' failed at step {step_index}:[/red]"
                    )
                    self.console.print(f"Command: {' '.join(cmd)}")

                    if "stdout" in result.error_details:
                        self.console.print("\nActual output:")
                        self.console.print(result.error_details["stdout"])

                    if "stderr" in result.error_details:
                        self.console.print("\nError output:")
                        self.console.print(result.error_details["stderr"])

                    if "expected_output" in result.error_details:
                        self.console.print("\nExpected output:")
                        self.console.print(result.error_details["expected_output"])

                    if "error_message" in result.error_details:
                        self.console.print("\nError details:")
                        self.console.print(f"  {result.error_details['error_message']}")

                    if "return_code" in result.error_details:
                        self.console.print(
                            f"\nReturn code: {result.error_details['return_code']}"
                        )

                    self.console.print()  # 添加一个空行作为分隔
                return result
            else:
                # 在非 rich 环境下直接执行
                return self._execute_test_steps(test)

        except subprocess.TimeoutExpired:
            return TestResult(
                success=False,
                message="Timeout",
                time=time.perf_counter() - start_time,
                score=0,
                max_score=test.meta["score"],
            )
        except Exception as e:
            return TestResult(
                success=False,
                message=f"Error: {str(e)}",
                time=time.perf_counter() - start_time,
                score=0,
                max_score=test.meta["score"],
            )

    def _execute_test_steps(
        self,
        test: TestCase,
        progress: Optional[Progress] = None,
        task: Optional[Any] = None,
    ) -> TestResult:
        start_time = time.perf_counter()
        step_scores = []
        total_score = 0
        has_step_scores = any("score" in step for step in test.run_steps)
        max_possible_score = (
            sum(step.get("score", 0) for step in test.run_steps)
            if has_step_scores
            else test.meta["score"]
        )

        for i, step in enumerate(test.run_steps, 1):
            if progress is not None and task is not None:
                step_name = step.get("name", step["command"])
                progress.update(
                    task,
                    description=f"Running {test.meta['name']} [{i}/{len(test.run_steps)}]: {step_name}",
                    completed=i - 1,
                )

            result = self._execute_single_step(test, step, i)
            if not result.success:
                if progress is not None and task is not None:
                    progress.update(task, completed=i)
                return result
            total_score += result.score
            if result.step_scores:
                step_scores.extend(result.step_scores)

                if progress is not None and task is not None:
                    progress.update(
                        task,
                        description=f"Running {test.meta['name']} [{i}/{len(test.run_steps)}]: {step_name}",
                        completed=i,
                    )

        if not has_step_scores:
            total_score = test.meta["score"]
            step_scores = None

        success = (
            total_score > 0 if has_step_scores else total_score == test.meta["score"]
        )
        return TestResult(
            success=success,
            message="All steps completed" if success else "Some steps failed",
            time=time.perf_counter() - start_time,
            score=total_score,
            max_score=max_possible_score,
            step_scores=step_scores,
            error_details=None,
        )

    def _execute_single_step(
        self, test: TestCase, step: Dict[str, Any], step_index: int
    ) -> TestResult:
        start_time = time.perf_counter()
        cmd = [self._resolve_path(step["command"], test.path)]
        args = [self._resolve_path(str(arg), test.path) for arg in step.get("args", [])]

        try:
            process = subprocess.run(
                cmd + args,
                cwd=test.path,
                input=self._get_stdin_data(test, step),
                capture_output=True,
                text=True,
                timeout=step.get("timeout", 5.0),
            )
        except subprocess.TimeoutExpired:
            return self._create_timeout_result(test, step, step_index, start_time)

        if "check" in step:
            success, message, score = self.checker.check(
                step,
                process.stdout,
                process.stderr,
                process.returncode,
                test.path,
            )

            if not success:
                return self._create_failure_result(
                    test,
                    step,
                    step_index,
                    message,
                    start_time,
                    process.stdout,
                    process.stderr,
                    process.returncode,
                    process.stdout
                    if "expected_output" in step.get("check", {})
                    else "",
                )

        return self._create_success_result(test, step, score, start_time)

    def _resolve_path(self, path: str, test_dir: Path) -> str:
        build_dir = test_dir / "build"
        build_dir.mkdir(exist_ok=True)

        replacements = {
            "${test_dir}": str(test_dir),
            "${common_dir}": str(self.config.paths["common_dir"]),
            "${root_dir}": str(self.config.project_root),
            "${build_dir}": str(build_dir),
        }

        for var, value in replacements.items():
            path = path.replace(var, value)
        return path

    def _get_stdin_data(self, test: TestCase, step: Dict[str, Any]) -> Optional[str]:
        if "stdin" not in step:
            return None

        stdin_file = test.path / step["stdin"]
        if not stdin_file.exists():
            raise FileNotFoundError(f"Input file {step['stdin']} not found")

        with open(stdin_file) as f:
            return f.read()

    def _create_timeout_result(
        self, test: TestCase, step: Dict[str, Any], step_index: int, start_time: float
    ) -> TestResult:
        error_message = f"Step {step_index} '{step.get('name', step['command'])}' timed out after {step.get('timeout', 5.0)}s"
        return TestResult(
            success=False,
            message=error_message,
            time=time.perf_counter() - start_time,
            score=0,
            max_score=step.get("score", test.meta["score"]),
            error_details={
                "step": step_index,
                "step_name": step.get("name", step["command"]),
                "error_message": error_message,
            },
        )

    def _create_failure_result(
        self,
        test: TestCase,
        step: Dict[str, Any],
        step_index: int,
        message: str,
        start_time: float,
        stdout: str = "",
        stderr: str = "",
        return_code: Optional[int] = None,
        expected_output: str = "",
    ) -> TestResult:
        error_details = {
            "step": step_index,
            "step_name": step.get("name", step["command"]),
            "error_message": message,
        }
        if stdout:
            error_details["stdout"] = stdout
        if stderr:
            error_details["stderr"] = stderr
        if return_code is not None:
            error_details["return_code"] = return_code
        if expected_output:
            error_details["expected_output"] = expected_output

        return TestResult(
            success=False,
            message=f"Step {step_index} '{step.get('name', step['command'])}' failed: {message}",
            time=time.perf_counter() - start_time,
            score=0,
            max_score=step.get("score", test.meta["score"]),
            error_details=error_details,
        )

    def _create_success_result(
        self,
        test: TestCase,
        step: Dict[str, Any],
        score: Optional[float],
        start_time: float,
    ) -> TestResult:
        step_score = score if score is not None else step.get("score", 0)
        return TestResult(
            success=True,
            message="Step completed successfully",
            time=time.perf_counter() - start_time,
            score=step_score,
            max_score=step.get("score", test.meta["score"]),
            step_scores=[
                (step.get("name", step["command"]), step_score, step.get("score", 0))
            ]
            if step.get("score", 0) > 0
            else None,
        )


class ResultFormatter(ABC):
    @abstractmethod
    def format_results(
        self,
        test_cases: List[TestCase],
        results: List[Dict[str, Any]],
        total_score: float,
        max_score: float,
    ) -> None:
        pass


class JsonFormatter(ResultFormatter):
    def format_results(
        self,
        test_cases: List[TestCase],
        results: List[Dict[str, Any]],
        total_score: float,
        max_score: float,
    ) -> None:
        json_result = {
            "total_score": round(total_score, 1),
            "max_score": round(max_score, 1),
            "percentage": round(total_score / max_score * 100, 1),
            "tests": results,
        }
        print(json.dumps(json_result, ensure_ascii=False))


class TableFormatter(ResultFormatter):
    def __init__(self, console: Console):
        self.console = console

    def format_results(
        self,
        test_cases: List[TestCase],
        results: List[Dict[str, Any]],
        total_score: float,
        max_score: float,
    ) -> None:
        self._format_rich_table(test_cases, results, total_score, max_score)

    def _format_rich_table(
        self,
        test_cases: List[TestCase],
        results: List[Dict[str, Any]],
        total_score: float,
        max_score: float,
    ) -> None:
        table = Table(show_header=True, header_style="bold")
        table.add_column("Test Case", style="cyan")
        table.add_column("Result", justify="center")
        table.add_column("Time", justify="right")
        table.add_column("Score", justify="right")
        table.add_column("Message")

        status_style = {
            "PASS": "[green]PASS[/green]",
            "PARTIAL": "[yellow]PARTIAL[/yellow]",
            "FAIL": "[red]FAIL[/red]",
        }

        for test, result in zip(test_cases, results):
            table.add_row(
                test.meta["name"],
                status_style[result["status"]],
                f"{result['time']:.2f}s",
                f"{result['score']:.1f}/{result['max_score']:.1f}",
                result["message"],
            )

        self.console.print(table)
        self._print_summary(total_score, max_score)

    def _format_basic_table(
        self,
        test_cases: List[TestCase],
        results: List[Dict[str, Any]],
        total_score: float,
        max_score: float,
    ) -> None:
        # 定义列宽
        col_widths = {
            "name": max(len(test.meta["name"]) for test in test_cases),
            "status": 8,  # PASS/PARTIAL/FAIL
            "time": 10,  # XX.XXs
            "score": 15,  # XX.X/XX.X
            "message": 40,
        }

        # 打印表头
        header = (
            f"{'Test Case':<{col_widths['name']}} "
            f"{'Result':<{col_widths['status']}} "
            f"{'Time':>{col_widths['time']}} "
            f"{'Score':>{col_widths['score']}} "
            f"{'Message':<{col_widths['message']}}"
        )
        self.console.print("-" * len(header))
        self.console.print(header)
        self.console.print("-" * len(header))

        # 打印每一行
        status_text = {
            "PASS": "PASS",
            "PARTIAL": "PARTIAL",
            "FAIL": "FAIL",
        }

        for test, result in zip(test_cases, results):
            row = (
                f"{test.meta['name']:<{col_widths['name']}} "
                f"{status_text[result['status']]:<{col_widths['status']}} "
                f"{result['time']:.2f}s".rjust(col_widths["time"])
                + " " f"{result['score']:.1f}/{result['max_score']}".rjust(
                    col_widths["score"]
                )
                + " "
                f"{result['message'][:col_widths['message']]:<{col_widths['message']}}"
            )
            self.console.print(row)

        self.console.print("-" * len(header))
        self._print_basic_summary(total_score, max_score)

    def _print_summary(self, total_score: float, max_score: float) -> None:
        summary = Panel(
            f"[bold]Total Score: {total_score:.1f}/{max_score:.1f} "
            f"({total_score/max_score*100:.1f}%)[/bold]",
            border_style="green" if total_score == max_score else "yellow",
        )
        self.console.print()
        self.console.print(summary)
        self.console.print()

    def _print_basic_summary(self, total_score: float, max_score: float) -> None:
        self.console.print()
        self.console.print(
            f"Total Score: {total_score:.1f}/{max_score:.1f} "
            f"({total_score/max_score*100:.1f}%)"
        )
        self.console.print()


class Grader:
    def __init__(self, json_output=False):
        self.config = Config(Path.cwd())
        self.json_output = json_output
        self.console = Console(quiet=json_output)
        self.runner = TestRunner(self.config, self.console)
        self.formatter = (
            JsonFormatter() if json_output else TableFormatter(self.console)
        )
        self.results: Dict[str, TestResult] = {}

    def run_all_tests(self, specific_test: Optional[str] = None):
        if not self._run_setup_steps():
            sys.exit(1)

        test_cases = self._load_test_cases(specific_test)
        if not self.json_output:
            self.console.print(
                f"\n[bold]Running {len(test_cases)} test cases...[/bold]\n"
            )

        total_score = 0
        max_score = 0
        test_results = []

        for test in test_cases:
            result = self.runner.run_test(test)
            self.results[test.path.name] = result
            result_dict = {
                "name": test.meta["name"],
                "success": result.success,
                "status": result.status,
                "time": round(result.time, 2),
                "score": result.score,
                "max_score": result.max_score,
                "step_scores": result.step_scores,
                "message": result.message,
                "error_details": result.error_details,
            }
            test_results.append(result_dict)
            total_score += result.score
            max_score += result.max_score

        self.formatter.format_results(test_cases, test_results, total_score, max_score)

    def _run_setup_steps(self) -> bool:
        for step in self.config.setup_steps:
            if not self._run_setup_step(step):
                return False
        return True

    def _run_setup_step(self, step: Dict[str, Any]) -> bool:
        if not self.json_output and "message" in step:
            self.console.print(f"[bold]{step['message']}[/bold]")

        try:
            if step["type"] != "command":
                if not self.json_output:
                    self.console.print(
                        f"[red]Error:[/red] Unknown setup step type: {step['type']}"
                    )
                return False

            cmd = [step["command"]]
            if "args" in step:
                if isinstance(step["args"], list):
                    cmd.extend(step["args"])
            else:
                cmd.append(step["args"])

            process = subprocess.run(
                cmd,
                cwd=self.config.project_root,
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

    def _load_test_cases(self, specific_test: Optional[str] = None) -> List[TestCase]:
        if specific_test:
            test_path = self.config.paths["cases_dir"] / specific_test
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

        if not self.config.paths["cases_dir"].exists():
            if not self.json_output:
                self.console.print("[red]Error:[/red] tests/cases directory not found")
            else:
                print("Error: tests/cases directory not found", file=sys.stderr)
            sys.exit(1)

        def get_sort_key(path: Path) -> tuple:
            # 尝试从文件夹名称中提取数字前缀
            match = re.match(r"(\d+)", path.name)
            if match:
                # 如果有数字前缀，返回 (0, 数字值, 文件夹名) 元组
                # 0 表示优先级最高
                return (0, int(match.group(1)), path.name)
            else:
                # 如果没有数字前缀，返回 (1, 0, 文件夹名) 元组
                # 1 表示优先级较低，这些文件夹会按字母顺序排在有数字前缀的文件夹后面
                return (1, 0, path.name)

        test_cases = []
        # 使用自定义排序函数
        for test_dir in sorted(
            self.config.paths["cases_dir"].iterdir(), key=get_sort_key
        ):
            if test_dir.is_dir() and (test_dir / "config.toml").exists():
                test_cases.append(self._load_single_test(test_dir))

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
        try:
            with open(test_path / "config.toml", "rb") as f:
                config = tomli.load(f)

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


def main():
    parser = argparse.ArgumentParser(description="Grade student submissions")
    parser.add_argument(
        "--json", action="store_true", help="Output results in JSON format"
    )
    parser.add_argument(
        "--write-result",
        action="store_true",
        help="Write percentage score to .autograder_result file",
    )
    parser.add_argument("test", nargs="?", help="Specific test to run")
    args = parser.parse_args()

    try:
        grader = Grader(json_output=args.json)
        grader.run_all_tests(args.test)

        # 检查是否所有测试都通过
        all_passed = all(result.success for result in grader.results.values())

        # 如果需要写入结果文件
        if args.write_result:
            total_score = sum(result.score for result in grader.results.values())
            max_score = sum(
                test.meta["score"] for test in grader._load_test_cases(args.test)
            )
            percentage = (total_score / max_score * 100) if max_score > 0 else 0
            with open(".autograder_result", "w") as f:
                f.write(f"{percentage:.2f}")

        sys.exit(0 if all_passed else 1)
    except subprocess.CalledProcessError as e:
        print(
            f"Error: Command execution failed with return code {e.returncode}",
            file=sys.stderr,
        )
        sys.exit(1)
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
