#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Activate virtual environment if found (check both tests/ and project root)
for base_dir in "$SCRIPT_DIR" "$PROJECT_ROOT"; do
    for venv_dir in venv .venv; do
        if [ -f "$base_dir/$venv_dir/bin/activate" ]; then
            echo "Activating virtual environment: $base_dir/$venv_dir"
            source "$base_dir/$venv_dir/bin/activate"
            break 2
        fi
    done
done

# Ensure PYTHONPATH includes the project root so tests can import packages
export PYTHONPATH="${PROJECT_ROOT}:${PYTHONPATH:-}"

# Install test dependencies if pytest is not available
if ! python3 -c "import pytest" 2>/dev/null; then
    echo "Installing test dependencies..."
    pip install pytest pytest-cov pytest-mock
fi

echo "Running tests..."
echo "Project root: $PROJECT_ROOT"
echo "PYTHONPATH: $PYTHONPATH"
echo ""

# Pass any extra arguments through to pytest (e.g. specific test files, -k filters)
python3 -m pytest -c pytest.ini "$@"
