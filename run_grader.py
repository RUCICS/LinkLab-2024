import subprocess
import sys
import venv
from pathlib import Path

# Check Python version
MIN_PYTHON = (3, 6)
if sys.version_info < MIN_PYTHON:
    print(
        f"Error: Python {MIN_PYTHON[0]}.{MIN_PYTHON[1]} or higher is required",
        file=sys.stderr,
    )
    sys.exit(1)


def create_venv(venv_path):
    """Create virtual environment"""
    print("Creating virtual environment...", flush=True)
    venv.create(venv_path, with_pip=True)


def install_requirements(venv_path):
    """Install dependencies"""
    pip_path = venv_path / ("Scripts" if sys.platform == "win32" else "bin") / "pip"
    requirements_path = Path(__file__).parent / "requirements.txt"

    print("Installing dependencies...", flush=True)
    subprocess.run([str(pip_path), "install", "-r", str(requirements_path)], check=True)


def run_grader():
    """Run grader script"""
    venv_dir = Path(__file__).parent / ".venv"
    python_path = (
        venv_dir / ("Scripts" if sys.platform == "win32" else "bin") / "python"
    )
    grader_path = Path(__file__).parent / "grader.py"

    # If virtual environment doesn't exist, create it and install dependencies
    if not venv_dir.exists():
        create_venv(venv_dir)
        install_requirements(venv_dir)

    # Run grader script with all command line arguments
    subprocess.run([str(python_path), str(grader_path)] + sys.argv[1:])


if __name__ == "__main__":
    try:
        run_grader()
    except subprocess.CalledProcessError as e:
        print(
            f"Error: Command execution failed with return code {e.returncode}",
            file=sys.stderr,
        )
        sys.exit(1)
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)
