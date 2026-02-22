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
#include <sys/utsname.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef CS_VERSION
#define CS_VERSION "dev"
#endif

#ifndef CS_REPO_OWNER
#define CS_REPO_OWNER ""
#endif

#ifndef CS_REPO_NAME
#define CS_REPO_NAME ""
#endif

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
            "  -u, --update          Update cs to latest release\n"
            "  --verbose             Print compile command and cache info\n"
            "  -v, --version         Print version\n"
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
    if (!path || path[0] == '\0') {
        return false;
    }
    if (dir_exists(path)) {
        return true;
    }

    char buffer[PATH_MAX];
    snprintf(buffer, sizeof(buffer), "%s", path);

    for (char *p = buffer + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!dir_exists(buffer)) {
                if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
            *p = '/';
        }
    }

    if (mkdir(buffer, 0755) == 0) {
        return true;
    }
    return errno == EEXIST && dir_exists(buffer);
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

static char *read_command_output(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        return NULL;
    }

    size_t cap = 8192;
    char *buffer = (char *)malloc(cap);
    if (!buffer) {
        pclose(pipe);
        return NULL;
    }

    size_t len = 0;
    while (!feof(pipe)) {
        if (len + 1024 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buffer, cap);
            if (!next) {
                free(buffer);
                pclose(pipe);
                return NULL;
            }
            buffer = next;
        }
        size_t read_count = fread(buffer + len, 1, 1024, pipe);
        len += read_count;
    }

    pclose(pipe);
    buffer[len] = '\0';
    return buffer;
}

static char *read_file_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read_count = fread(buffer, 1, (size_t)size, file);
    buffer[read_count] = '\0';
    fclose(file);
    return buffer;
}

static char *json_find_string(const char *json, const char *key) {
    size_t key_len = strlen(key);
    size_t pattern_len = key_len + 4;
    char *pattern = (char *)malloc(pattern_len);
    if (!pattern) {
        return NULL;
    }
    snprintf(pattern, pattern_len, "\"%s\"", key);

    const char *pos = strstr(json, pattern);
    free(pattern);
    if (!pos) {
        return NULL;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return NULL;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    if (*pos != '"') {
        return NULL;
    }
    pos++;
    const char *end = pos;
    while (*end && *end != '"') {
        if (*end == '\\' && end[1]) {
            end += 2;
        } else {
            end++;
        }
    }
    size_t len = (size_t)(end - pos);
    char *value = (char *)malloc(len + 1);
    if (!value) {
        return NULL;
    }
    memcpy(value, pos, len);
    value[len] = '\0';
    return value;
}

static char *json_find_asset_url(const char *json, const char *asset_name) {
    size_t name_len = strlen(asset_name);
    size_t pattern_len = name_len + 12;
    char *pattern = (char *)malloc(pattern_len);
    if (!pattern) {
        return NULL;
    }
    snprintf(pattern, pattern_len, "\"name\":\"%s\"", asset_name);

    const char *pos = strstr(json, pattern);
    free(pattern);
    if (!pos) {
        return NULL;
    }
    const char *url_key = "\"browser_download_url\":\"";
    const char *url_pos = strstr(pos, url_key);
    if (!url_pos) {
        return NULL;
    }
    url_pos += strlen(url_key);
    const char *end = strchr(url_pos, '"');
    if (!end) {
        return NULL;
    }
    size_t len = (size_t)(end - url_pos);
    char *value = (char *)malloc(len + 1);
    if (!value) {
        return NULL;
    }
    memcpy(value, url_pos, len);
    value[len] = '\0';
    return value;
}

static bool parse_semver(const char *version, int *major, int *minor,
                         int *patch) {
    if (!version || !major || !minor || !patch) {
        return false;
    }
    return sscanf(version, "%d.%d.%d", major, minor, patch) == 3;
}

static int compare_versions(const char *current, const char *latest) {
    int cmaj = 0, cmin = 0, cpat = 0;
    int lmaj = 0, lmin = 0, lpat = 0;
    if (!parse_semver(current, &cmaj, &cmin, &cpat)) {
        return -1;
    }
    if (!parse_semver(latest, &lmaj, &lmin, &lpat)) {
        return -1;
    }
    if (cmaj != lmaj)
        return (cmaj > lmaj) ? 1 : -1;
    if (cmin != lmin)
        return (cmin > lmin) ? 1 : -1;
    if (cpat != lpat)
        return (cpat > lpat) ? 1 : -1;
    return 0;
}

static char *resolve_self_path(const char *argv0) {
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = '\0';
        return dup_string(buffer);
    }
    if (argv0 && strchr(argv0, '/')) {
        return dup_string(argv0);
    }
    const char *path = getenv("PATH");
    if (!path || !argv0) {
        return NULL;
    }
    char *path_copy = dup_string(path);
    if (!path_copy) {
        return NULL;
    }
    char *saveptr = NULL;
    for (char *dir = strtok_r(path_copy, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr)) {
        snprintf(buffer, sizeof(buffer), "%s/%s", dir, argv0);
        if (file_exists(buffer)) {
            char *resolved = dup_string(buffer);
            free(path_copy);
            return resolved;
        }
    }
    free(path_copy);
    return NULL;
}

static char *detect_os_arch(char *os, size_t os_len, char *arch,
                            size_t arch_len) {
    struct utsname info;
    if (uname(&info) != 0) {
        return NULL;
    }
    if (strcmp(info.sysname, "Linux") == 0) {
        snprintf(os, os_len, "linux");
    } else if (strcmp(info.sysname, "Darwin") == 0) {
        snprintf(os, os_len, "darwin");
    } else {
        snprintf(os, os_len, "%s", info.sysname);
    }

    if (strcmp(info.machine, "x86_64") == 0) {
        snprintf(arch, arch_len, "amd64");
    } else if (strcmp(info.machine, "aarch64") == 0 ||
               strcmp(info.machine, "arm64") == 0) {
        snprintf(arch, arch_len, "arm64");
    } else {
        snprintf(arch, arch_len, "%s", info.machine);
    }
    return os;
}

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buffer[8192];
    size_t read_count = 0;
    while ((read_count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, read_count, out) != read_count) {
            fclose(in);
            fclose(out);
            return false;
        }
    }
    fclose(in);
    fclose(out);
    return true;
}

static int perform_update(const char *argv0, bool verbose) {
    const char *owner = getenv("CS_REPO_OWNER");
    const char *repo = getenv("CS_REPO_NAME");
    if (!owner || owner[0] == '\0') {
        owner = CS_REPO_OWNER;
    }
    if (!repo || repo[0] == '\0') {
        repo = CS_REPO_NAME;
    }
    if (!owner || owner[0] == '\0' || !repo || repo[0] == '\0') {
        fprintf(stderr, "Update requires CS_REPO_OWNER and CS_REPO_NAME\n");
        return 1;
    }

    char api_url[512];
    snprintf(api_url, sizeof(api_url),
             "https://api.github.com/repos/%s/%s/releases/latest", owner, repo);

    char curl_cmd[768];
    snprintf(curl_cmd, sizeof(curl_cmd), "curl -fsSL \"%s\"", api_url);
    char *json = read_command_output(curl_cmd);
    if (!json) {
        fprintf(stderr, "Failed to fetch release info\n");
        return 1;
    }

    char *tag = json_find_string(json, "tag_name");
    if (!tag) {
        fprintf(stderr, "Failed to parse release tag\n");
        free(json);
        return 1;
    }

    const char *latest = tag;
    if (tag[0] == 'v') {
        latest = tag + 1;
    }

    if (strcmp(CS_VERSION, "dev") != 0) {
        int cmp = compare_versions(CS_VERSION, latest);
        if (cmp >= 0) {
            printf("cs %s already up to date\n", CS_VERSION);
            free(tag);
            free(json);
            return 0;
        }
    }

    char os[32];
    char arch[32];
    if (!detect_os_arch(os, sizeof(os), arch, sizeof(arch))) {
        fprintf(stderr, "Failed to detect platform\n");
        free(tag);
        free(json);
        return 1;
    }

    char asset_name[128];
    char checksum_name[160];
    snprintf(asset_name, sizeof(asset_name), "cs-%s-%s", os, arch);
    snprintf(checksum_name, sizeof(checksum_name), "%s.sha256", asset_name);

    char *asset_url = json_find_asset_url(json, asset_name);
    char *checksum_url = json_find_asset_url(json, checksum_name);
    if (!asset_url || !checksum_url) {
        fprintf(stderr, "Release asset not found for %s/%s\n", os, arch);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    char tmp_path[] = "/tmp/cs-update-XXXXXX";
    int tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) {
        fprintf(stderr, "Failed to create temp file\n");
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }
    close(tmp_fd);

    char checksum_path[] = "/tmp/cs-update-sha-XXXXXX";
    int checksum_fd = mkstemp(checksum_path);
    if (checksum_fd < 0) {
        fprintf(stderr, "Failed to create checksum file\n");
        unlink(tmp_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }
    close(checksum_fd);

    char download_cmd[1024];
    snprintf(download_cmd, sizeof(download_cmd), "curl -fsSL -o \"%s\" \"%s\"",
             tmp_path, asset_url);
    if (verbose) {
        fprintf(stderr, "%s\n", download_cmd);
    }
    if (system(download_cmd) != 0) {
        fprintf(stderr, "Failed to download update\n");
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    char checksum_cmd[1024];
    snprintf(checksum_cmd, sizeof(checksum_cmd), "curl -fsSL -o \"%s\" \"%s\"",
             checksum_path, checksum_url);
    if (system(checksum_cmd) != 0) {
        fprintf(stderr, "Failed to download checksum\n");
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    char *checksum_text = read_file_text(checksum_path);
    if (!checksum_text) {
        fprintf(stderr, "Failed to read checksum\n");
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    char expected_hash[128] = {0};
    if (sscanf(checksum_text, "%127s", expected_hash) != 1) {
        fprintf(stderr, "Invalid checksum file\n");
        free(checksum_text);
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    free(checksum_text);

    char hash_cmd[1024];
    snprintf(hash_cmd, sizeof(hash_cmd), "sha256sum \"%s\"", tmp_path);
    char *hash_out = read_command_output(hash_cmd);
    if (!hash_out) {
        snprintf(hash_cmd, sizeof(hash_cmd), "shasum -a 256 \"%s\"", tmp_path);
        hash_out = read_command_output(hash_cmd);
    }
    if (!hash_out) {
        fprintf(stderr, "sha256 tool not available\n");
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    char actual_hash[128] = {0};
    if (sscanf(hash_out, "%127s", actual_hash) != 1) {
        fprintf(stderr, "Failed to read hash\n");
        free(hash_out);
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }
    free(hash_out);

    if (strcmp(actual_hash, expected_hash) != 0) {
        fprintf(stderr, "Checksum mismatch\n");
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    chmod(tmp_path, 0755);

    char *self_path = resolve_self_path(argv0);
    if (!self_path) {
        fprintf(stderr, "Failed to resolve current binary path\n");
        unlink(tmp_path);
        unlink(checksum_path);
        free(asset_url);
        free(checksum_url);
        free(tag);
        free(json);
        return 1;
    }

    if (rename(tmp_path, self_path) != 0) {
        if (!copy_file(tmp_path, self_path)) {
            fprintf(stderr, "Failed to replace binary: %s\n", strerror(errno));
            free(self_path);
            unlink(tmp_path);
            unlink(checksum_path);
            free(asset_url);
            free(checksum_url);
            free(tag);
            free(json);
            return 1;
        }
        unlink(tmp_path);
    }

    unlink(checksum_path);
    printf("Updated to cs %s\n", latest);
    free(self_path);
    free(asset_url);
    free(checksum_url);
    free(tag);
    free(json);
    return 0;
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
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            printf("cs %s\n", CS_VERSION);
            return 0;
        }
        if (strcmp(arg, "--update") == 0 || strcmp(arg, "-u") == 0) {
            return perform_update(argv[0], verbose);
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
