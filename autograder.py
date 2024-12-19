import json
import subprocess
import sys
from rich.console import Console
from rich.table import Table
from rich.panel import Panel


def run_grader():
    """运行grader并获取结果"""
    with open(".autograder_result", "w") as f:
        f.write(str(0))

    try:
        # 运行grader.py并获取JSON输出
        result = subprocess.run(
            [sys.executable, "grader.py", "--json"],
            capture_output=True,
            text=True,
            check=False,
        )

        # 解析JSON输出
        try:
            grader_output = json.loads(result.stdout)

            console = Console()
            table = Table(show_header=True, header_style="bold")
            table.add_column("Test Case", style="cyan")
            table.add_column("Result", justify="center")
            table.add_column("Time", justify="right")
            table.add_column("Score", justify="right")
            table.add_column("Message")

            for test in grader_output["tests"]:
                table.add_row(
                    test["name"],
                    "[green]PASS[/green]" if test["success"] else "[red]FAIL[/red]",
                    f"{test['time']:.2f}s",
                    f"{test['score']:.1f}/{test['max_score']}",
                    test["message"],
                )

            console.print(table)

            summary = Panel(
                f"[bold]Total Score: {grader_output['total_score']:.1f}/{grader_output['max_score']:.1f} "
                f"({grader_output['percentage']:.1f}%)[/bold]",
                border_style="green"
                if grader_output["total_score"] == grader_output["max_score"]
                else "yellow",
            )
            console.print()
            console.print(summary)
            console.print()
            final_score = grader_output.get("percentage", 0)
        except json.JSONDecodeError:
            print("Error: Failed to parse grader output", file=sys.stderr)
            final_score = 0

        # 写入分数到.autograder_result文件
        with open(".autograder_result", "w") as f:
            f.write(str(final_score))

        # 保持与grader.py相同的退出代码
        sys.exit(result.returncode)

    except Exception as e:
        print(f"Error running grader: {str(e)}", file=sys.stderr)
        with open(".autograder_result", "w") as f:
            f.write("0")
        sys.exit(1)


if __name__ == "__main__":
    run_grader()
