#ifndef CS_H
#define CS_H

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
} cs_buffer;

static inline cs_buffer cs_read_file(const char *path) {
    cs_buffer result = {0};
    FILE *file = fopen(path, "rb");
    if (!file) {
        return result;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return result;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return result;
    }
    rewind(file);

    result.data = (char *)malloc((size_t)size + 1);
    if (!result.data) {
        fclose(file);
        return result;
    }

    size_t read_count = fread(result.data, 1, (size_t)size, file);
    result.data[read_count] = '\0';
    result.len = read_count;
    fclose(file);
    return result;
}

static inline int cs_write_file(const char *path, const char *data) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return errno ? -errno : -1;
    }
    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, file);
    fclose(file);
    if (written != len) {
        return -1;
    }
    return 0;
}

static inline int cs_run_cmd(const char *cmd) { return system(cmd); }

static inline cs_buffer cs_run_cmd_capture(const char *cmd) {
    cs_buffer result = {0};
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        return result;
    }

    size_t cap = 4096;
    result.data = (char *)malloc(cap);
    if (!result.data) {
        pclose(pipe);
        return result;
    }

    size_t len = 0;
    while (!feof(pipe)) {
        if (len + 1024 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(result.data, cap);
            if (!next) {
                free(result.data);
                result.data = NULL;
                result.len = 0;
                pclose(pipe);
                return result;
            }
            result.data = next;
        }
        size_t read_count = fread(result.data + len, 1, 1024, pipe);
        len += read_count;
    }

    pclose(pipe);
    result.data[len] = '\0';
    result.len = len;
    return result;
}

static inline int cs_list_dir(const char *path, char ***entries,
                              size_t *count) {
    DIR *dir = opendir(path);
    if (!dir) {
        return errno ? -errno : -1;
    }

    size_t cap = 16;
    size_t len = 0;
    char **list = (char **)malloc(cap * sizeof(char *));
    if (!list) {
        closedir(dir);
        return -1;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (len >= cap) {
            cap *= 2;
            char **next = (char **)realloc(list, cap * sizeof(char *));
            if (!next) {
                for (size_t i = 0; i < len; i++) {
                    free(list[i]);
                }
                free(list);
                closedir(dir);
                return -1;
            }
            list = next;
        }
        list[len] = strdup(ent->d_name);
        if (!list[len]) {
            for (size_t i = 0; i < len; i++) {
                free(list[i]);
            }
            free(list);
            closedir(dir);
            return -1;
        }
        len++;
    }

    closedir(dir);
    *entries = list;
    *count = len;
    return 0;
}

#endif
