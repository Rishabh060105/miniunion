#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>

#define MAX_LAYERS 10

struct mini_unionfs_state {
    char *upper_dir;
    char *lower_dirs[MAX_LAYERS];
    int lower_count;
};

#define UFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* ================= METRICS ================= */

struct metrics {
    int upper_reads;
    int lower_reads;
    int cow;
    int whiteouts;
} m = {0};

/* ================= DASHBOARD ================= */

void *dashboard_thread(void *arg) {
    while (1) {
        system("clear");

        printf("\nMini-UnionFS Dashboard\n");
        printf("---------------------------\n");
        printf("Upper Reads   : %d\n", m.upper_reads);
        printf("Lower Reads   : %d\n", m.lower_reads);
        printf("Copy-on-Write : %d\n", m.cow);
        printf("Whiteouts     : %d\n", m.whiteouts);

        sleep(2);
    }
    return NULL;
}

/* ================= PATH HELPERS ================= */

static void upper_path(char *buf, const char *path) {
    snprintf(buf, PATH_MAX, "%s%s", UFS_DATA->upper_dir, path);
}

static void whiteout_path(char *buf, const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, PATH_MAX, "%s", path);

    char *slash = strrchr(tmp, '/');

    if (slash) {
        *slash = '\0';
        char *base = slash + 1;
        snprintf(buf, PATH_MAX, "%s%s/.wh.%s",
                 UFS_DATA->upper_dir, tmp, base);
    } else {
        snprintf(buf, PATH_MAX, "%s/.wh.%s",
                 UFS_DATA->upper_dir, path);
    }
}

/* ================= RESOLVE ================= */

static int resolve_path(const char *path, char *resolved_path) {
    char wh[PATH_MAX], up[PATH_MAX];

    whiteout_path(wh, path);
    upper_path(up, path);

    if (access(wh, F_OK) == 0) return -ENOENT;

    if (access(up, F_OK) == 0) {
        strncpy(resolved_path, up, PATH_MAX);
        return 0;
    }

    for (int i = UFS_DATA->lower_count - 1; i >= 0; i--) {
        char lo[PATH_MAX];
        snprintf(lo, PATH_MAX, "%s%s",
                 UFS_DATA->lower_dirs[i], path);

        if (access(lo, F_OK) == 0) {
            strncpy(resolved_path, lo, PATH_MAX);
            return 0;
        }
    }

    return -ENOENT;
}

/* ================= COPY-ON-WRITE ================= */

static int cow_copy(const char *path) {
    char src_path[PATH_MAX];
    int ret = resolve_path(path, src_path);
    if (ret < 0) return ret;

    char dst_path[PATH_MAX];
    upper_path(dst_path, path);

    char dir[PATH_MAX];
    strcpy(dir, dst_path);
    char *slash = strrchr(dir, '/');

    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    int src = open(src_path, O_RDONLY);
    if (src < 0) return -errno;

    struct stat st;
    fstat(src, &st);

    int dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (dst < 0) {
        close(src);
        return -errno;
    }

    char buf[65536];
    ssize_t n;

    while ((n = read(src, buf, sizeof(buf))) > 0)
        write(dst, buf, n);

    close(src);
    close(dst);

    m.cow++;
    return 0;
}

/* ================= FUSE OPS ================= */

static int unionfs_getattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];
    int ret = resolve_path(path, resolved);

    if (ret < 0) return ret;

    if (lstat(resolved, stbuf) < 0)
        return -errno;

    return 0;
}

static int unionfs_readdir(const char *path, void *buf,
                           fuse_fill_dir_t filler,
                           off_t offset,
                           struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {

    (void) offset;
    (void) fi;
    (void) flags;

    char up[PATH_MAX];
    upper_path(up, path);

    char seen[512][NAME_MAX];
    int seen_count = 0;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    DIR *du = opendir(up);
    if (du) {
        struct dirent *de;

        while ((de = readdir(du)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;

            strncpy(seen[seen_count++], de->d_name, NAME_MAX);

            if (strncmp(de->d_name, ".wh.", 4) == 0)
                continue;

            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(du);
    }

    for (int l = 0; l < UFS_DATA->lower_count; l++) {
        char lo[PATH_MAX];
        snprintf(lo, PATH_MAX, "%s%s",
                 UFS_DATA->lower_dirs[l], path);

        DIR *dl = opendir(lo);
        if (!dl) continue;

        struct dirent *de;

        while ((de = readdir(dl)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;

            char wh[PATH_MAX];
            snprintf(wh, PATH_MAX, "%s/.wh.%s", up, de->d_name);

            if (access(wh, F_OK) == 0)
                continue;

            int dup = 0;
            for (int i = 0; i < seen_count; i++)
                if (!strcmp(seen[i], de->d_name)) dup = 1;

            if (!dup) {
                strncpy(seen[seen_count++], de->d_name, NAME_MAX);
                filler(buf, de->d_name, NULL, 0, 0);
            }
        }
        closedir(dl);
    }

    return 0;
}

static int unionfs_open(const char *path,
                        struct fuse_file_info *fi) {

    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        char up[PATH_MAX];
        upper_path(up, path);

        if (access(up, F_OK) != 0) {
            int ret = cow_copy(path);
            if (ret < 0) return ret;
        }
    }
    return 0;
}

static int unionfs_read(const char *path, char *buf,
                        size_t size, off_t offset,
                        struct fuse_file_info *fi) {

    (void) fi;

    char resolved[PATH_MAX];
    int ret = resolve_path(path, resolved);
    if (ret < 0) return ret;

    int fd = open(resolved, O_RDONLY);
    if (fd < 0) return -errno;

    ssize_t n = pread(fd, buf, size, offset);
    close(fd);

    if (strstr(resolved, UFS_DATA->upper_dir))
        m.upper_reads++;
    else
        m.lower_reads++;

    return (n < 0) ? -errno : n;
}

static int unionfs_write(const char *path, const char *buf,
                         size_t size, off_t offset,
                         struct fuse_file_info *fi) {

    (void) fi;

    char up[PATH_MAX];
    upper_path(up, path);

    if (access(up, F_OK) != 0) {
        int ret = cow_copy(path);
        if (ret < 0) return ret;
    }

    int fd = open(up, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) return -errno;

    ssize_t n = pwrite(fd, buf, size, offset);
    close(fd);

    return (n < 0) ? -errno : n;
}

static int unionfs_unlink(const char *path) {
    char up[PATH_MAX];
    upper_path(up, path);

    if (access(up, F_OK) == 0)
        unlink(up);

    char wh[PATH_MAX];
    whiteout_path(wh, path);

    int fd = open(wh, O_CREAT | O_WRONLY, 0000);
    if (fd < 0) return -errno;

    close(fd);

    m.whiteouts++;
    return 0;
}

/* ================= MAIN ================= */

static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .unlink  = unionfs_unlink,
};

int main(int argc, char *argv[]) {

    if (argc < 5) {
        fprintf(stderr,
        "Usage: %s <lower1> <lower2> ... <upper> <mount>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state =
        calloc(1, sizeof(*state));

    state->upper_dir = realpath(argv[argc - 2], NULL);
    state->lower_count = argc - 3;

    for (int i = 0; i < state->lower_count; i++)
        state->lower_dirs[i] = realpath(argv[i + 1], NULL);

    pthread_t tid;
    pthread_create(&tid, NULL, dashboard_thread, NULL);

    argv[1] = argv[argc - 1];
    argc = 2;

    return fuse_main(argc, argv, &unionfs_oper, state);
}