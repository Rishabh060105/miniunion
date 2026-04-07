#!/bin/bash
# Helper script to run MiniUnionFS with Python

PROJECT_DIR=$(pwd)
VENV_PATH="$PROJECT_DIR/.venv"
MAIN_PY="$PROJECT_DIR/main.py"
TEST_ENV="$PROJECT_DIR/test_env"
LOWER="$TEST_ENV/lower"
UPPER="$TEST_ENV/upper"
MNT="$TEST_ENV/mnt"

# Ensure directories exist
mkdir -p "$LOWER" "$UPPER" "$MNT"

# Add a sample file to lower if empty
if [ ! -f "$LOWER/hello.txt" ]; then
    echo "This is content from the lower layer." > "$LOWER/hello.txt"
fi

echo "Activating virtual environment..."
source "$VENV_PATH/bin/activate"

echo "Attempting to mount MiniUnionFS..."
echo "Mount point: $MNT"
python3 "$MAIN_PY" "$LOWER" "$UPPER" "$MNT"
