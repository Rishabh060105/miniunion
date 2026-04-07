# Mini-UnionFS Design Document

## 1. Introduction
Mini-UnionFS is a simplified Union File System that runs in userspace via FUSE (Filesystem in Userspace). Its primary objective is to mimic the core mechanism behind Docker containers: stacking a read-write "container layer" (the `upper_dir`) on top of one or more read-only "base image" layers (`lower_dirs`). It provides a unified, single view for users.

This project is implemented in both **Python** (using the `fusepy` library) and **C** (using `libfuse3`), offering dual perspectives on the same filesystem logic. The C implementation additionally features a real-time **Metrics Dashboard** and supports the newer **FUSE 3** API.

## 2. Core Architecture
The filesystem logic revolves around resolving file states across multiple `lower_dirs` and the single `upper_dir`.

### Python
The main class `MiniUnionFS` inherits from `fusepy`'s `Operations` class. It stores an ordered list of `lower_dirs` and a single `upper_dir`, routing POSIX system calls (`open`, `read`, `write`, etc.) to the correct backing path.

### C
The `mini_unionfs_state` struct holds `upper_dir`, an array of `lower_dirs[MAX_LAYERS]`, and `lower_count`. FUSE operations access this state via `fuse_get_context()->private_data`. A separate `pthread` runs the dashboard loop.

### Global State & Path Resolution
Path resolution follows a strict priority order:
1. **Whiteout Check**: The resolver searches for `upper_dir/.wh.[filename]`. If a whiteout marker is found, it returns `ENOENT`, simulating deletion.
2. **Upper Layer**: If the file exists in `upper_dir`, its absolute path is returned immediately.
3. **Lower Layers (reverse order)**: The system iterates through `lower_dirs` from the highest-priority (last added) to the lowest. The first match is returned.
4. **Creation Default**: If absent from all layers, the `upper_dir` path is returned for file creation.

## 3. Multi-Layer Stacking
Mini-UnionFS supports up to **10 lower layers** (configurable via `MAX_LAYERS` in C). Each layer is stacked in the order provided on the command line. During path resolution and directory listing, later layers have higher priority over earlier ones.

### Directory Listing (`readdir`)
`readdir` creates a unified view by:
- Iterating the `upper_dir` first, collecting normal entries and whiteout markers (`.wh.*` prefixed files) into separate sets.
- Iterating each `lower_dir` in order, adding entries to the result set only if they are not whiteout-hidden and not already seen.
- This deduplication ensures a clean, merged directory listing.

## 4. Copy-on-Write (CoW) Mechanism
When a user attempts to modify a file that exists only in a lower layer:
- **Trigger**: Detected via write flags (`O_WRONLY | O_RDWR | O_APPEND`) in `open()`, or during explicit `write()` calls.
- **Implementation**: The file is located in the appropriate `lower_dir` via path resolution. Its binary contents and permissions are duplicated to the `upper_dir`. In the C version, parent directories are created via `mkdir()` if needed. A metrics counter (`m.cow`) is incremented.
- **Result**: All subsequent operations target the new `upper_dir` copy, leaving the original lower layer untouched.

## 5. Whiteout Mechanism (Deletions)
Deletions route via `unlink()` or `rmdir()`:
- **Upper-only file**: Physically deleted from `upper_dir`.
- **Lower-layer file**: The system creates a zero-byte sentinel file `.wh.[filename]` in `upper_dir`. No changes propagate to any lower layer. Successive path resolution checks detect this marker and return `ENOENT`.
- **Re-creation**: Creating a file whose name matches an existing whiteout marker automatically removes the marker, restoring visibility.

## 6. Real-Time Metrics Dashboard (C Only)
The C implementation launches a background `pthread` that refreshes a terminal dashboard every 2 seconds, displaying:

| Metric | Description |
|---|---|
| **Upper Reads** | Number of `read()` operations served from `upper_dir`. |
| **Lower Reads** | Number of `read()` operations served from any `lower_dir`. |
| **Copy-on-Write** | Total CoW copy-up operations performed. |
| **Whiteouts** | Total whiteout markers created via deletions. |

The Python implementation mirrors this with a `threading.Thread` daemon.

## 7. Conclusion
Mini-UnionFS reliably handles advanced file state operations across unified virtual mounts with multiple stacked layers, without sacrificing read-only immutability. Both the Python and C implementations are fully tested via Docker-based automated test suites.
