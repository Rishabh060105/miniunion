# Mini-UnionFS Design Document

## 1. Introduction
Mini-UnionFS is a simplified Union File System that runs in userspace via FUSE (Filesystem in Userspace). Its primary objective is to mimic the core mechanism behind Docker containers: stacking a read-write "container layer" (the `upper_dir`) on top of a read-only "base image" (the `lower_dir`). It provides a unified, single view for users. 

This project is implemented in Python using the `fusepy` library, ensuring rapid development and clear, readable representations of complex file system logic like Copy-on-Write (CoW) and Whiteout.

## 2. Core Architecture
The filesystem logic revolves around resolving file states between the `lower_dir` and `upper_dir`. The main class `MiniUnionFS` inherits from `fusepy`'s `Operations` class and acts as a router mapping normal POSIX system calls (like `open`, `read`, `write`) to the respective backing directories.

### Global State & Path Resolution
Path resolution is isolated in `_full_path()` and `_is_in_lower_only()`.
When a user requests a file action at an abstract local path (e.g., `/config.txt`):
1. **Whiteout Check**: The system searches `upper_dir/.[directories]/.wh.[filename]`. If a whiteout is found, it mimics a deletion by raising an `ENOENT` error.
2. **Precedence**: The system checks if the file exists in the `upper_dir`. If so, its absolute path is returned.
3. **Fallback**: If not found in the `upper_dir` but exists in the `lower_dir`, the `lower_dir` absolute path is returned.
4. **Creation Default**: If present in neither, operations that create new elements defere to the `upper_dir` absolute path.

## 3. Advanced Features Handling
### Layer Stacking (Directory Union)
Directory listing is handled through the `readdir` FUSE operation.
`readdir` creates a unified `Set` structure:
- It iterates the `upper_dir` directly, gathering normal items, whilst also gathering all whiteout files (items prefixed with `.wh.`) into a separate subset.
- It iterates the `lower_dir` and adds elements to the final set if they are *not* present in the whiteout subset.
- Combining sets ensures duplicate files across layers are reduced to single instances, providing a clean "stacked" view where priority is inherently derived from `upper_dir` tracking.

### Copy-on-Write (CoW) Mechanism
When a user attempts to modify an existing file (checked strictly via `os.O_WRONLY | os.O_RDWR | os.O_APPEND` flags inside `open()`, or during explicit `write()` commands), the CoW function `_copy_up()` is dynamically routed.
- **Trigger**: Only triggers if the file explicitly exists in `lower_dir` *but not* in `upper_dir`.
- **Implementation**: The system opens the `lower_dir` path, reads its binary contents, and duplicates both its data payload and underlying file permissions directly to the `upper_dir` hierarchy safely prior to continuing the file mutation.

### Whiteout Mechanism (Deletions)
Deletions route via `unlink()` or `rmdir()`.
- **`upper_dir` Deletion**: If the file explicitly lives in the upper (read-write) sector, it physically triggers an OS-level delete.
- **`lower_dir` Deletion**: If the file resides purely on the base sector, the system intercepts the POSIX request and creates a dummy 0-byte file initialized as `.wh.[filename]` locally in the `upper_dir`. Thus, no changes propagate to the lower sector, but successive requests hit the Path Resolution logic, identifying the whiteout token and hiding it from the final rendered virtual node.
Any subsequent creation of a file overlapping a whiteout token seamlessly triggers its implicit and automated removal.

## 4. Conclusion
Mini-UnionFS reliably handles advanced file state operations across unified virtual mounts without sacrificing read-only immutability. All core assignment expectations are implemented efficiently over OS-level proxy interactions via FUSE.
