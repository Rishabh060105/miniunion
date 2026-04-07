#!/bin/bash
FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
LOWER1="$TEST_DIR/lower1"
LOWER2="$TEST_DIR/lower2"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo "Starting Mini-UnionFS Multi-Layer Test Suite..."

# Compile the C implementation
echo "Compiling..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

# Setup
rm -rf "$TEST_DIR"
mkdir -p "$LOWER1" "$LOWER2" "$UPPER_DIR" "$MOUNT_DIR"
echo "base_content" > "$LOWER1/base.txt"
echo "layer2_content" > "$LOWER2/layer2.txt"
echo "to_be_deleted" > "$LOWER1/delete_me.txt"

# Run in background to let the bash script continue
$FUSE_BINARY "$LOWER1" "$LOWER2" "$UPPER_DIR" "$MOUNT_DIR" -f &
FUSE_PID=$!
sleep 2

# Test 1: Layer Visibility
echo -n "Test 1: Multi-Layer Visibility... "
if grep -q "base_content" "$MOUNT_DIR/base.txt" && grep -q "layer2_content" "$MOUNT_DIR/layer2.txt"; then
    echo -e "${GREEN}PASSED${NC}"
else
    echo -e "${RED}FAILED${NC}"
fi

# Test 2: Copy-on-Write
echo -n "Test 2: Copy-on-Write... "
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
if [ $(grep -c "modified_content" "$MOUNT_DIR/base.txt" 2>/dev/null) -eq 1 ] && [ $(grep -c "modified_content" "$UPPER_DIR/base.txt" 2>/dev/null) -eq 1 ] && [ $(grep -c "modified_content" "$LOWER1/base.txt" 2>/dev/null) -eq 0 ]; then
    echo -e "${GREEN}PASSED${NC}"
else
    echo -e "${RED}FAILED${NC}"
fi

# Test 3: Whiteout
echo -n "Test 3: Whiteout mechanism... "
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
if [ ! -f "$MOUNT_DIR/delete_me.txt" ] && [ -f "$LOWER1/delete_me.txt" ] && [ -f "$UPPER_DIR/.wh.delete_me.txt" ]; then
    echo -e "${GREEN}PASSED${NC}"
else
    echo -e "${RED}FAILED${NC}"
fi

# Teardown
kill $FUSE_PID
sleep 1
umount "$MOUNT_DIR" 2>/dev/null
rm -rf "$TEST_DIR"
echo "Multi-Layer Test Suite Completed."
