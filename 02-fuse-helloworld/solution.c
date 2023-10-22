#include <solution.h>

#include <fuse.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SIZE_BUFFER 100

static int fs_getattr(const char* path, struct stat* st, struct fuse_file_info* fi) {
    (void)fi;
    memset(st, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0775;
        st->st_nlink = 2;
    } else if (strcmp(path, "/hello") == 0) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
    } else {
        return -ENOENT;
    }
    st->st_size = 32;
    st->st_atime = st->st_mtime = st->st_ctime = time(NULL);
    return 0;
}

static int fs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t off,
                           struct fuse_file_info* fi, enum fuse_readdir_flags readdir_flags) {
    (void)off;
    (void)fi;
    (void)readdir_flags;

    if (strcmp(path, "/") != 0) {
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, "hello", NULL, 0, 0);
    return 0;
}

static int fs_open(const char* path, struct fuse_file_info* fi) {
    if (strcmp(path, "/") == 0) {
        return -EISDIR;
    }
    if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)) {
        return -EROFS;
    }
    if (strcmp(path, "hello")) {
        return 0;
    }
    return -ENOENT;
}

static int fs_read(const char* path, char* buf, size_t size, off_t off, struct fuse_file_info* fi) {
    (void) fi;
    if (strcmp(path, "/hello") != 0) {
        return -ENOENT;
    }

    struct fuse_context* fuse_context = fuse_get_context();
    pid_t pid_hello = fuse_context->pid;
    char hello_pid[SIZE_BUFFER] = {0};
    snprintf(hello_pid, 100, "hello, %d\n", pid_hello);
    int n = strlen(hello_pid);

    if (off < n) {
        if (off + size > (unsigned)n)
            size = n - off;
        memcpy(buf, hello_pid + off, size);
        return size;
    }
    else {
        return 0;
    }
}

static int fs_mkdir(const char* path, mode_t mode) {
    (void) path;
    (void) mode;

    return -EROFS;
}

static int fs_write(const char* path, const char* buf,
                         size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) path;
    (void) buf;
    (void) size;
    (void) offset;
    (void) fi;

    return -EROFS;
}

static int fs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void) path;
    (void) mode;
    (void) fi;

    return -EROFS;
}

static const struct fuse_operations hellofs_ops = {
        .getattr = fs_getattr,
        .readdir = fs_readdir,
        .open = fs_open,
        .read = fs_read,
        .mkdir = fs_mkdir,
        .write = fs_write,
        .create = fs_create
};

int helloworld(const char *mntp) {
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
