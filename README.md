# Mini-UnionFS 🗂️

A simplified Union File System running in userspace via **FUSE** (Filesystem in Userspace). Mini-UnionFS mimics the core mechanism behind Docker containers by stacking a read-write "container layer" (`upper_dir`) on top of a read-only "base image" (`lower_dir`), providing a single unified view.

Implemented in both **Python** and **C**, and fully testable via **Docker**.

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Prerequisites](#prerequisites)
- [Running with Docker (Recommended)](#running-with-docker-recommended)
  - [Python Implementation](#python-implementation)
  - [C Implementation](#c-implementation)
- [Running Natively (macOS / Linux)](#running-natively-macos--linux)
  - [Python (Native)](#python-native)
  - [C (Native)](#c-native)
- [Test Suite](#test-suite)
- [Design Document](#design-document)

---

## Features

| Feature | Description |
|---|---|
| **Multiple Lower Layers** | Stack an arbitrary number of read-only `lower_dir` layers. The upper-most layer overrides duplicates, mimicking advanced container union systems. |
| **Layer Visibility** | Files across all layers are transparently merged into a unified mount point. |
| **Copy-on-Write (CoW)** | Modifying any lower-layer file automatically copies it to the `upper_dir` before mutation — the originals remain untouched. |
| **Whiteout Mechanism** | Deleting a lower-layer file creates a `.wh.<filename>` marker in `upper_dir`, hiding it from the merged view without altering the base layers. |
| **Real-time Dashboard** | C implementation features an interactive metrics thread tracking reads, writes, and whiteout activity live. |
| **FUSE 3 Support** | Complete compatibility with the newer `fuse3` / `libfuse3` API. |

---

## Architecture

```
┌─────────────────────────────────────┐
│          Unified Mount Point        │
│           (mount_dir)               │
├─────────────────────────────────────┤
│                                     │
│     ┌───────────────────────┐       │
│     │   Upper Layer (R/W)   │       │
│     │   - New files         │       │
│     │   - Modified files    │       │
│     │   - Whiteout markers  │       │
│     └───────────┬───────────┘       │
│                 │ fallback           │
│     ┌───────────▼───────────┐       │
│     │   Lower Layer (R/O)   │       │
│     │   - Base image files  │       │
│     └───────────────────────┘       │
│                                     │
└─────────────────────────────────────┘
```

### Path Resolution Order

1. **Whiteout Check** → If `.wh.<filename>` exists in `upper_dir`, return `ENOENT`.
2. **Upper Layer** → If file exists in `upper_dir`, return its path.
3. **Lower Layer** → If file exists in `lower_dir`, return its path.
4. **Default** → Return `upper_dir` path (for file creation).

---

## Project Structure

```
MiniUnion/
├── main.py              # Python implementation (fusepy)
├── main.c               # C implementation (libfuse)
├── Makefile             # Build rules for the C binary
├── Dockerfile           # Ubuntu 22.04 image with FUSE + Python + GCC
├── .dockerignore        # Excludes .venv and test dirs from Docker build
├── test_docker.sh       # Automated test suite (Docker / C)
├── test_unionfs.sh      # Automated test suite (native / Python)
├── run_python.sh        # Helper script for native Python execution
├── design_document.md   # Detailed design document
└── README.md            # This file
```

---

## Prerequisites

### Docker (Recommended)
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) installed and running.

### Native
- **macOS**: [macFUSE](https://osxfuse.github.io/) installed and system extension allowed in Privacy & Security settings.
- **Linux**: `libfuse-dev` (or `libfuse3-dev`) and `fuse` packages installed.
- **Python**: Python 3 with `fusepy` (`pip install fusepy`).
- **C**: GCC and `pkg-config`.

---

## Running with Docker (Recommended)

Docker provides a clean Linux environment with FUSE pre-installed, avoiding any macOS kernel extension issues.

### Build the Docker Image

```bash
docker build -t miniunion_test .
```

### Python Implementation

Run the Python test suite inside Docker:

```bash
# First, update test_docker.sh to use Python:
# Change FUSE_BINARY to:
#   source .venv/bin/activate
#   FUSE_BINARY="python3 ./main.py"

docker run --rm --privileged miniunion_test ./test_docker.sh
```

### C Implementation

Compile and run the C test suite inside Docker:

```bash
docker run --rm --privileged \
  -v $(pwd)/test_docker.sh:/app/test_docker.sh \
  miniunion_test bash -c \
  "gcc -Wall -O2 -D_FILE_OFFSET_BITS=64 \
   \$(pkg-config --cflags fuse) \
   -o mini_unionfs main.c \
   \$(pkg-config --libs fuse) && \
   ./test_docker.sh"
```

> **Note**: The `--privileged` flag is required because FUSE needs elevated permissions to mount filesystems inside a container.

---

## Running Natively (macOS / Linux)

### Python (Native)

```bash
# Create and activate virtual environment
python3 -m venv .venv
source .venv/bin/activate
pip install fusepy

# Create test directories
mkdir -p test_env/lower test_env/upper test_env/mnt
echo "Hello from the base layer" > test_env/lower/hello.txt

# Mount the filesystem (runs in foreground)
python3 main.py test_env/lower1 test_env/lower2 test_env/upper test_env/mnt
```

**Usage**: `python3 main.py <lower1> [lower2 ...] <upper_dir> <mount_point>`

### C (Native)

```bash
# Compile
make

# Create test directories
mkdir -p test_env/lower1 test_env/lower2 test_env/upper test_env/mnt
echo "Hello from the base layer" > test_env/lower1/hello.txt

# Mount the filesystem (-f for foreground)
./mini_unionfs test_env/lower1 test_env/lower2 test_env/upper test_env/mnt -f
```

**Usage**: `./mini_unionfs <lower1> [lower2 ...] <upper_dir> <mount_point> [fuse_options]`

### Unmounting

```bash
# Linux
umount test_env/mnt

# macOS
diskutil unmount test_env/mnt
```

---

## Test Suite

The test suite (`test_docker.sh` / `test_unionfs.sh`) validates three core features:

| Test | What It Validates |
|---|---|
| **Layer Visibility** | A file created in `lower_dir` is readable through the mount point. |
| **Copy-on-Write** | Appending to a lower-layer file creates a modified copy in `upper_dir` while the original in `lower_dir` stays unchanged. |
| **Whiteout** | Deleting a lower-layer file via the mount creates a `.wh.*` marker in `upper_dir`, hides the file from the mount, and preserves the original in `lower_dir`. |

### Expected Output

```
Starting Mini-UnionFS Test Suite...
Test 1: Layer Visibility... PASSED
Test 2: Copy-on-Write... PASSED
Test 3: Whiteout mechanism... PASSED
Test Suite Completed.
```

---

## Design Document

For a detailed explanation of the internal architecture, path resolution, Copy-on-Write mechanics, and Whiteout mechanism, see [design_document.md](design_document.md).
