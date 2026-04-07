#!/bin/bash
FUSE_BINARY="python3 ./main.py"
TEST_DIR="./unionfs_test_env"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

umount "$MOUNT_DIR" 2>/dev/null
rm -rf "$TEST_DIR"
mkdir -p "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR"
echo "base_only_content" > "$LOWER_DIR/base.txt"

$FUSE_BINARY "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR" &
FUSE_PID=$!
sleep 2

echo "Running Test 2 Commands..."
echo "modified_content" >> "$MOUNT_DIR/base.txt"

kill $FUSE_PID
sleep 1
umount "$MOUNT_DIR" 2>/dev/null
