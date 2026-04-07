#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

// Global state passed to fuse_main
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

static void get_upper_path(char *upper_path, const char *path) {
    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
}

static void get_lower_path(char *lower_path, const char *path) {
    snprintf(lower_path, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);
}

static void get_whiteout_path(char *whiteout_path, const char *path) {
    const char *filename = strrchr(path, '/');
    if (filename == NULL) {
        filename = path;
    } else {
        filename++; // skip the slash
    }
    
    char dir_path[PATH_MAX];
    strncpy(dir_path, path, filename - path);
    dir_path[filename - path] = '\0';
    
    snprintf(whiteout_path, PATH_MAX, "%s%s.wh.%s", UNIONFS_DATA->upper_dir, dir_path, filename);
}
    

/* Helper function to resolve paths:
 * 1. Check if upper_dir + "/.wh.config.txt" exists -> return ENOENT.
 * 2. Check if upper_dir + "/config.txt" exists -> return this path.
 * 3. Check if lower_dir + "/config.txt" exists -> return this path.
 * 4. Otherwise, return -ENOENT.
 */
int resolve_path(const char *path, char *resolved_path) {
    char whiteout_path[PATH_MAX];
    get_whiteout_path(whiteout_path, path);
    if (access(whiteout_path, F_OK) != -1) {
        return -ENOENT;
    }
    
    char upper_path[PATH_MAX];
    get_upper_path(upper_path, path);
    if (access(upper_path, F_OK) != -1) {
        strcpy(resolved_path, upper_path);
        return 0;
    }
    
    char lower_path[PATH_MAX];
    get_lower_path(lower_path, path);
    if (access(lower_path, F_OK) != -1) {
        strcpy(resolved_path, lower_path);
        return 0;
    }
    
    strcpy(resolved_path, upper_path); // Default to upper for create/mkdir
    return -ENOENT;
}

static int unionfs_getattr(const char *path, struct stat *stbuf) {
    char resolved_path[PATH_MAX];
    int res = resolve_path(path, resolved_path);
    
    if (res == -ENOENT && access(resolved_path, F_OK) == -1)
        return -ENOENT;

    res = lstat(resolved_path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

int copy_up(const char *path) {
    char lower_path[PATH_MAX];
    char upper_path[PATH_MAX];
    
    get_lower_path(lower_path, path);
    get_upper_path(upper_path, path);
    
    int fd_lower = open(lower_path, O_RDONLY);
    if (fd_lower == -1) {
        return -errno;
    }
    
    struct stat st;
    if (fstat(fd_lower, &st) == -1) {
        close(fd_lower);
        return -errno;
    }
    
    int fd_upper = open(upper_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_upper == -1) {
        close(fd_lower);
        return -errno;
    }
    
    char buf[4096];
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(fd_lower, buf, sizeof(buf))) > 0) {
        char *ptr = buf;
        while (bytes_read > 0) {
            bytes_written = write(fd_upper, ptr, bytes_read);
            if (bytes_written < 0) {
                close(fd_lower);
                close(fd_upper);
                return -errno;
            }
            bytes_read -= bytes_written;
            ptr += bytes_written;
        }
    }
    
    close(fd_lower);
    close(fd_upper);
    return 0;
}

static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved_path[PATH_MAX];
    int res = resolve_path(path, resolved_path);
    if (res == -ENOENT) {
        return -ENOENT;
    }
    
    char lower_path[PATH_MAX];
    get_lower_path(lower_path, path);
    
    // Check if the resolved path is the lower file, and we are opening for write
    if (strcmp(resolved_path, lower_path) == 0 && (fi->flags & (O_WRONLY | O_RDWR | O_APPEND))) {
        int cow_res = copy_up(path);
        if (cow_res != 0) {
            return cow_res;
        }
        get_upper_path(resolved_path, path); // Now open the upper path
    }

    int fd = open(resolved_path, fi->flags);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path;
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1)
        res = -errno;
    return res;
}

static int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path;
    int res = pwrite(fi->fh, buf, size, offset);
    if (res == -1)
        res = -errno;
    return res;
}

static int unionfs_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    close(fi->fh);
    return 0;
}


static int unionfs_unlink(const char *path) {
    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    
    get_upper_path(upper_path, path);
    get_lower_path(lower_path, path);
    
    int in_upper = (access(upper_path, F_OK) != -1);
    int in_lower = (access(lower_path, F_OK) != -1);
    
    if (in_upper) {
        if (unlink(upper_path) == -1) {
            return -errno;
        }
    }
    
    if (in_lower) {
        char whiteout_path[PATH_MAX];
        get_whiteout_path(whiteout_path, path);
        int fd = open(whiteout_path, O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
            return -errno;
        }
        close(fd);
    }
    return 0;
}

static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    
    char upper_dir_path[PATH_MAX];
    get_upper_path(upper_dir_path, path);
    
    char lower_dir_path[PATH_MAX];
    get_lower_path(lower_dir_path, path);
    
    DIR *dp;
    struct dirent *de;
    
    // Maximum number of entries is upper + lower. Simplification for tracking duplicates
    // In a real implementation this would be a hash set.
    char **seen_entries = malloc(1024 * sizeof(char*));
    int seen_count = 0;
    
    char **whiteouts = malloc(1024 * sizeof(char*));
    int whiteout_count = 0;

    dp = opendir(upper_dir_path);
    if (dp != NULL) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
                
            if (strncmp(de->d_name, ".wh.", 4) == 0) {
                whiteouts[whiteout_count++] = strdup(de->d_name + 4);
            } else {
                seen_entries[seen_count++] = strdup(de->d_name);
                filler(buf, de->d_name, NULL, 0);
            }
        }
        closedir(dp);
    }

    dp = opendir(lower_dir_path);
    if (dp != NULL) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
                
            int is_whiteout = 0;
            for (int i=0; i<whiteout_count; i++) {
                if (strcmp(de->d_name, whiteouts[i]) == 0) {
                    is_whiteout = 1;
                    break;
                }
            }
            if (is_whiteout) continue;
            
            int is_seen = 0;
            for (int i=0; i<seen_count; i++) {
                if (strcmp(de->d_name, seen_entries[i]) == 0) {
                    is_seen = 1;
                    break;
                }
            }
            if (!is_seen) {
                filler(buf, de->d_name, NULL, 0);
            }
        }
        closedir(dp);
    }
    
    for (int i=0; i<seen_count; i++) free(seen_entries[i]);
    free(seen_entries);
    for (int i=0; i<whiteout_count; i++) free(whiteouts[i]);
    free(whiteouts);

    return 0;
}

static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .release = unionfs_release,
    .unlink  = unionfs_unlink,
    .readdir = unionfs_readdir,
};

int main(int argc, char *argv[]) {
    // Basic argument parsing
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point> [fuse options]\n", argv[0]);
        return 1;
    }
    
    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);
    
    if (!state->lower_dir || !state->upper_dir) {
        fprintf(stderr, "Error resolving absolute paths for lower or upper dir\n");
        return 1;
    }

    // Shift arguments for FUSE
    argv[1] = argv[3];
    for (int i = 2; i < argc - 2; i++) {
        argv[i] = argv[i+2];
    }
    argc -= 2;

    return fuse_main(argc, argv, &unionfs_oper, state);
}
