#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CS_VERSION "0.1.0"

static void print_usage(FILE *out) {
    fprintf(out,
            "Usage: cs [options] <file.c> [--] [args...]\n"
            "\n"
            "Options:\n"
            "  --cc <compiler>       Compiler to use (default: cc)\n"
            "  --cflags <flags>      Extra compiler flags (can repeat)\n"
            "  --ldflags <flags>     Extra linker flags (can repeat)\n"
            "  --cache-dir <dir>     Cache directory override\n"
            "  --no-cache            Disable cache\n"
            "  --verbose             Print compile command and cache info\n"
            "  --version             Print version\n"
            "  --help                Show this help\n");
}

static uint64_t fnv1a_update(uint64_t hash, const void *data, size_t len) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t fnv1a_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }

    uint64_t hash = 1469598103934665603ULL;
    unsigned char buffer[8192];
    size_t read_count = 0;
    while ((read_count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        hash = fnv1a_update(hash, buffer, read_count);
    }

    fclose(file);
    return hash;
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool ensure_dir(const char *path) {
    if (dir_exists(path)) {
        return true;
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST && dir_exists(path);
}

static char *dup_string(const char *value) {
    size_t len = strlen(value);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, len + 1);
    return copy;
}

static bool append_flag(char **dest, const char *flag) {
    if (!flag || flag[0] == '\0') {
        return true;
    }

    if (!*dest) {
        *dest = dup_string(flag);
        return *dest != NULL;
    }

    size_t current_len = strlen(*dest);
    size_t add_len = strlen(flag);
    char *next = realloc(*dest, current_len + 1 + add_len + 1);
    if (!next) {
        return false;
    }
    next[current_len] = ' ';
    memcpy(next + current_len + 1, flag, add_len + 1);
    *dest = next;
    return true;
}

static char *get_default_cache_dir(void) {
    const char *env = getenv("CS_CACHE_DIR");
    if (env && env[0] != '\0') {
        return dup_string(env);
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return NULL;
    }

    size_t len = strlen(home) + strlen("/.cache/cs") + 1;
    char *path = malloc(len);
    if (!path) {
        return NULL;
    }
    snprintf(path, len, "%s/.cache/cs", home);
    return path;
}

static char *get_exe_dir(void) {
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len < 0) {
        return NULL;
    }
    buffer[len] = '\0';
    char *slash = strrchr(buffer, '/');
    if (!slash) {
        return NULL;
    }
    *slash = '\0';
    return dup_string(buffer);
}

static char *build_compile_command(const char *cc, const char *include_dir,
                                   const char *cflags, const char *source,
                                   const char *output, const char *ldflags) {
    const char *include_part = "";
    char include_buffer[PATH_MAX + 4];
    if (include_dir && include_dir[0] != '\0') {
        snprintf(include_buffer, sizeof(include_buffer), "-I\"%s\"",
                 include_dir);
        include_part = include_buffer;
    }

    const char *cflags_part = cflags ? cflags : "";
    const char *ldflags_part = ldflags ? ldflags : "";

    size_t len = strlen(cc) + strlen(include_part) + strlen(cflags_part) +
                 strlen(source) + strlen(output) + strlen(ldflags_part) + 64;
    char *cmd = malloc(len);
    if (!cmd) {
        return NULL;
    }

    snprintf(cmd, len, "%s %s %s \"%s\" -o \"%s\" %s", cc, include_part,
             cflags_part, source, output, ldflags_part);
    return cmd;
}

int main(int argc, char **argv) {
    const char *cc = "cc";
    char *cflags = NULL;
    char *ldflags = NULL;
    char *cache_dir = NULL;
    bool no_cache = false;
    bool verbose = false;

    const char *source_path = NULL;
    int args_index = -1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc) {
                args_index = i + 1;
            }
            break;
        }
        if (strcmp(arg, "--help") == 0) {
            print_usage(stdout);
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("cs %s\n", CS_VERSION);
            return 0;
        }
        if (strcmp(arg, "--cc") == 0 && i + 1 < argc) {
            cc = argv[++i];
            continue;
        }
        if (strcmp(arg, "--cflags") == 0 && i + 1 < argc) {
            if (!append_flag(&cflags, argv[++i])) {
                fprintf(stderr, "Failed to append cflags\n");
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "--ldflags") == 0 && i + 1 < argc) {
            if (!append_flag(&ldflags, argv[++i])) {
                fprintf(stderr, "Failed to append ldflags\n");
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "--cache-dir") == 0 && i + 1 < argc) {
            free(cache_dir);
            cache_dir = dup_string(argv[++i]);
            if (!cache_dir) {
                fprintf(stderr, "Failed to set cache dir\n");
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "--no-cache") == 0) {
            no_cache = true;
            continue;
        }
        if (strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        }
        if (arg[0] == '-' && !source_path) {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return 1;
        }

        if (!source_path) {
            source_path = arg;
            if (i + 1 < argc) {
                args_index = i + 1;
            }
        }
    }

    if (!source_path) {
        print_usage(stderr);
        return 1;
    }

    if (!file_exists(source_path)) {
        fprintf(stderr, "Source file not found: %s\n", source_path);
        return 1;
    }

    if (!cache_dir) {
        cache_dir = get_default_cache_dir();
    }

    if (!cache_dir && !no_cache) {
        fprintf(stderr, "Failed to resolve cache dir\n");
        return 1;
    }

    if (!no_cache && !ensure_dir(cache_dir)) {
        fprintf(stderr, "Failed to create cache dir: %s\n", cache_dir);
        return 1;
    }

    uint64_t hash = fnv1a_file(source_path);
    if (hash == 0) {
        fprintf(stderr, "Failed to read source file: %s\n", source_path);
        return 1;
    }
    hash = fnv1a_update(hash, cc, strlen(cc));
    if (cflags) {
        hash = fnv1a_update(hash, cflags, strlen(cflags));
    }
    if (ldflags) {
        hash = fnv1a_update(hash, ldflags, strlen(ldflags));
    }

    const char *base = path_basename(source_path);
    char output_path[PATH_MAX];
    if (no_cache) {
        snprintf(output_path, sizeof(output_path), "/tmp/cs-%s-%016llx", base,
                 (unsigned long long)hash);
    } else {
        snprintf(output_path, sizeof(output_path), "%s/%s-%016llx", cache_dir,
                 base, (unsigned long long)hash);
    }

    bool need_compile = !file_exists(output_path) || no_cache;
    if (verbose) {
        fprintf(stderr, "%s: %s\n", need_compile ? "compile" : "cache",
                output_path);
    }

    if (need_compile) {
        char *exe_dir = get_exe_dir();
        char include_parent[PATH_MAX];
        char include_file[PATH_MAX];
        const char *include_path = NULL;
        if (exe_dir) {
            snprintf(include_file, sizeof(include_file), "%s/cs.h", exe_dir);
            if (file_exists(include_file)) {
                include_path = exe_dir;
            } else {
                snprintf(include_parent, sizeof(include_parent), "%s/../cs.h",
                         exe_dir);
                if (file_exists(include_parent)) {
                    include_path = include_parent;
                }
            }
        }

        char *command = build_compile_command(
            cc, include_path, cflags, source_path, output_path, ldflags);
        if (!command) {
            fprintf(stderr, "Failed to build compile command\n");
            free(exe_dir);
            return 1;
        }

        if (verbose) {
            fprintf(stderr, "%s\n", command);
        }

        int compile_status = system(command);
        free(command);
        free(exe_dir);

        if (compile_status != 0) {
            fprintf(stderr, "Compile failed (%d)\n", compile_status);
            return compile_status;
        }
    }

    int exec_argc = 1;
    if (args_index > 0) {
        exec_argc += argc - args_index;
    }

    char **exec_argv = calloc((size_t)exec_argc + 1, sizeof(char *));
    if (!exec_argv) {
        fprintf(stderr, "Failed to allocate argv\n");
        return 1;
    }

    exec_argv[0] = output_path;
    if (args_index > 0) {
        for (int i = args_index; i < argc; i++) {
            exec_argv[i - args_index + 1] = argv[i];
        }
    }
    exec_argv[exec_argc] = NULL;

    execv(output_path, exec_argv);
    fprintf(stderr, "Failed to run %s: %s\n", output_path, strerror(errno));
    free(exec_argv);
    return 1;
}
