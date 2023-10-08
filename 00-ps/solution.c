#include <solution.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#define MAX_ARG 4096
#define MAX_ARG_LENGTH 4096
#define MAX_FILEPATH_LENGTH 4096 // In linux, the maximum path length is 255 bytes
#define EXE_MAX_LENGTH 4096

static const char* PROC_PATH = "/proc/";
static const char* EXE_PATH = "/exe";
static const char* CMDLINE_PATH = "/cmdline";
static const char* ENVIRON_PATH = "/environ";

void ps(void)
{
    DIR* proc_directory = opendir(PROC_PATH); // open proc_directory

    if (proc_directory == NULL) {
        report_error(PROC_PATH, errno);
        return;
    }

    char* exe = (char*) malloc(EXE_MAX_LENGTH); // allocation exe
    char** argv = (char**) calloc(MAX_ARG, sizeof(char*)); // allocation argv
    char** envp = (char**) calloc(MAX_ARG, sizeof(char*)); // allocation envp

    for (int i = 0; i < MAX_ARG; ++i) {
        argv[i] = (char*) calloc(MAX_ARG_LENGTH, sizeof(char)); // allocation argv[i]
        envp[i] = (char*) calloc(MAX_ARG_LENGTH, sizeof(char)); // allocation envp[i]
    }

    struct dirent* proc_dirent;
    char* current_path = (char*) calloc(MAX_FILEPATH_LENGTH, sizeof(char)); // allocation current_path
    while ((proc_dirent = readdir(proc_directory)) != NULL) {

        char* p_end;
        pid_t pid = (pid_t) strtol(proc_dirent->d_name, &p_end, 10);

        if (*p_end) {
            continue;
        }


        sprintf(current_path, "%s%s%s", PROC_PATH, proc_dirent->d_name, EXE_PATH);
        if (readlink(current_path, exe, EXE_MAX_LENGTH) == -1) {
            report_error(current_path, errno);
            continue;
        }


        sprintf(current_path, "%s%s%s", PROC_PATH, proc_dirent->d_name, CMDLINE_PATH);
        FILE* ptr_file1;
        if ((ptr_file1 = fopen(current_path, "r")) == NULL) { // open file1
            report_error(current_path, errno);
            continue;
        }
        char** argv_report_process = (char**) calloc(MAX_ARG, sizeof(char*)); // allocation for argv
        for (int i = 0; i < MAX_ARG; ++i) {
            size_t max_arg_length = MAX_ARG_LENGTH;
            if (getdelim(&argv[i], &max_arg_length, '\0', ptr_file1) != -1 && argv[i][0] != '\0') {
                argv_report_process[i] = argv[i];
            } else {
                argv_report_process[i] = NULL;
                break;
            }
        }
        fclose(ptr_file1); // close file1


        sprintf(current_path, "%s%s%s", PROC_PATH, proc_dirent->d_name, ENVIRON_PATH);
        FILE* ptr_file2;
        if ((ptr_file2 = fopen(current_path, "r")) == NULL) { // open file2
            free(argv_report_process); // free argv
            report_error(current_path, errno);
            continue;
        }
        char** envp_report_process = (char**) calloc(MAX_ARG, sizeof(char*));
        for (int i = 0; i < MAX_ARG; ++i) {
            size_t max_arg_length = MAX_ARG_LENGTH;
            if (getdelim(&envp[i], &max_arg_length, '\0', ptr_file2) != -1 && envp[i][0] != '\0') {
                envp_report_process[i] = envp[i];
            } else {
                envp_report_process[i] = NULL;
                break;
            }
        }
        fclose(ptr_file2); // close file2

        report_process(pid, exe, argv_report_process, envp_report_process);
        free(argv_report_process);
        free(envp_report_process);
    }

    free(current_path); // free current_path
    closedir(proc_directory); // close proc_directory

    for (int i = 0; i < MAX_ARG; ++i) {
        free(argv[i]); // free argv[i]
        free(envp[i]); // free envp[i]
    }

    free(exe); // free exe
    free(argv); // free argv
    free(envp); // free envp
}