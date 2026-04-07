#!/usr/bin/env python3

from __future__ import print_function
from __future__ import absolute_import

import os
import sys
import errno
from fuse import FUSE, FuseOSError, Operations

class MiniUnionFS(Operations):
    def __init__(self, lower_dir, upper_dir):
        self.lower_dir = os.path.realpath(lower_dir)
        self.upper_dir = os.path.realpath(upper_dir)

    def _full_path(self, partial):
        if partial.startswith("/"):
            partial = partial[1:]
        
        # Check if whiteout exists in upper
        whiteout_path = os.path.join(self.upper_dir, f".wh.{os.path.basename(partial)}")
        if os.path.dirname(partial):
            whiteout_path = os.path.join(self.upper_dir, os.path.dirname(partial), f".wh.{os.path.basename(partial)}")
            
        if os.path.exists(whiteout_path):
            raise FuseOSError(errno.ENOENT)

        upper_path = os.path.join(self.upper_dir, partial)
        if os.path.exists(upper_path):
            return upper_path

        lower_path = os.path.join(self.lower_dir, partial)
        if os.path.exists(lower_path):
            return lower_path

        # If we reach here, neither exists, return upper path for creation
        return upper_path
        
    def _is_in_lower_only(self, partial):
        if partial.startswith("/"):
            partial = partial[1:]
        
        upper_path = os.path.join(self.upper_dir, partial)
        lower_path = os.path.join(self.lower_dir, partial)
        
        return os.path.exists(lower_path) and not os.path.exists(upper_path)

    def _copy_up(self, partial):
        if partial.startswith("/"):
            partial = partial[1:]
        
        upper_path = os.path.join(self.upper_dir, partial)
        lower_path = os.path.join(self.lower_dir, partial)
        
        # Simple file copy
        with open(lower_path, 'rb') as f_src:
            with open(upper_path, 'wb') as f_dst:
                f_dst.write(f_src.read())
        
        # Copy permissions
        st = os.stat(lower_path)
        os.chmod(upper_path, st.st_mode)

    # Core Operations
    def getattr(self, path, fh=None):
        try:
             full_path = self._full_path(path)
             if not os.path.exists(full_path):
                 raise FuseOSError(errno.ENOENT)
        except FuseOSError:
             raise
             
        st = os.lstat(full_path)
        return dict((key, getattr(st, key)) for key in ('st_atime', 'st_ctime',
                     'st_gid', 'st_mode', 'st_mtime', 'st_nlink', 'st_size', 'st_uid'))

    def readdir(self, path, fh):
        if path.startswith("/"):
             rel_path = path[1:]
        else:
             rel_path = path
             
        upper_dir_path = os.path.join(self.upper_dir, rel_path)
        lower_dir_path = os.path.join(self.lower_dir, rel_path)
        
        entries = set(['.', '..'])
        whiteouts = set()
        
        if os.path.exists(upper_dir_path) and os.path.isdir(upper_dir_path):
            for e in os.listdir(upper_dir_path):
                if e.startswith(".wh."):
                    whiteouts.add(e[4:])
                else:
                    entries.add(e)
                    
        if os.path.exists(lower_dir_path) and os.path.isdir(lower_dir_path):
            for e in os.listdir(lower_dir_path):
                if e not in whiteouts:
                    entries.add(e)
                    
        for e in entries:
            yield e

    def read(self, path, length, offset, fh):
        full_path = self._full_path(path)
        os.lseek(fh, offset, os.SEEK_SET)
        return os.read(fh, length)

    def write(self, path, buf, offset, fh):
        if self._is_in_lower_only(path):
             self._copy_up(path)
             
        full_path = self._full_path(path)
        os.lseek(fh, offset, os.SEEK_SET)
        return os.write(fh, buf)
        
    def open(self, path, flags):
        if self._is_in_lower_only(path) and (flags & (os.O_WRONLY | os.O_RDWR | os.O_APPEND)):
             self._copy_up(path)
             
        full_path = self._full_path(path)
        return os.open(full_path, flags)

    def create(self, path, mode, fi=None):
        if path.startswith("/"):
            partial = path[1:]
        else:
            partial = path
            
        full_path = os.path.join(self.upper_dir, partial)
        
        # Remove whiteout if it exists
        whiteout_path = os.path.join(self.upper_dir, os.path.dirname(partial), f".wh.{os.path.basename(partial)}")
        if os.path.exists(whiteout_path):
            os.remove(whiteout_path)
            
        return os.open(full_path, os.O_WRONLY | os.O_CREAT, mode)

    def unlink(self, path):
        if path.startswith("/"):
            partial = path[1:]
        else:
            partial = path
            
        upper_path = os.path.join(self.upper_dir, partial)
        lower_path = os.path.join(self.lower_dir, partial)
        
        in_upper = os.path.exists(upper_path)
        in_lower = os.path.exists(lower_path)
        
        if in_upper:
            os.unlink(upper_path)
            
        if in_lower:
            whiteout_path = os.path.join(self.upper_dir, os.path.dirname(partial), f".wh.{os.path.basename(partial)}")
            with open(whiteout_path, 'w') as f:
                pass

    def rmdir(self, path):
        if path.startswith("/"):
            partial = path[1:]
        else:
            partial = path
            
        upper_path = os.path.join(self.upper_dir, partial)
        lower_path = os.path.join(self.lower_dir, partial)
        
        in_upper = os.path.exists(upper_path)
        in_lower = os.path.exists(lower_path)
        
        if in_upper:
            os.rmdir(upper_path)
            
        if in_lower:
            whiteout_path = os.path.join(self.upper_dir, os.path.dirname(partial), f".wh.{os.path.basename(partial)}")
            with open(whiteout_path, 'w') as f:
                pass
                
    def mkdir(self, path, mode):
        if path.startswith("/"):
            partial = path[1:]
        else:
            partial = path
            
        full_path = os.path.join(self.upper_dir, partial)
        
        # Remove whiteout if it exists
        whiteout_path = os.path.join(self.upper_dir, os.path.dirname(partial), f".wh.{os.path.basename(partial)}")
        if os.path.exists(whiteout_path):
            os.remove(whiteout_path)
            
        return os.mkdir(full_path, mode)

    def flush(self, path, fh):
        return os.fsync(fh)

    def release(self, path, fh):
        return os.close(fh)

    def fsync(self, path, fdatasync, fh):
        return self.flush(path, fh)

def main():
    if len(sys.argv) != 4:
        print("Usage: {} lower_dir upper_dir mount_point".format(sys.argv[0]))
        sys.exit(1)

    lower_dir = sys.argv[1]
    upper_dir = sys.argv[2]
    mount_point = sys.argv[3]

    if not os.path.exists(lower_dir):
        print(f"Error: lower_dir '{lower_dir}' does not exist")
        sys.exit(1)
        
    if not os.path.exists(upper_dir):
        os.makedirs(upper_dir)
        
    if not os.path.exists(mount_point):
        os.makedirs(mount_point)

    print(f"Mounting MiniUnionFS at {mount_point} with lower={lower_dir}, upper={upper_dir}")
    FUSE(MiniUnionFS(lower_dir, upper_dir), mount_point, nothreads=True, foreground=True)

if __name__ == '__main__':
    main()
