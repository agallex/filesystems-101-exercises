#include <solution.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_FILEPATH_LENGTH 4096 // In linux, the maximum path length is 255 bytes

void abspath(const char *path) {
    char resolved_path[MAX_FILEPATH_LENGTH];

    if (realpath(path, resolved_path) == NULL) {
        report_error("/", path, errno);
        return;
    }

    struct stat path_stat;
    if (lstat(resolved_path, &path_stat) != 0) {
        report_error("/", resolved_path, errno);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        size_t len = strlen(resolved_path);
        if (resolved_path[len - 1] != '/') {
            strcat(resolved_path, "/");
        }
    }

    report_path(resolved_path);
}

