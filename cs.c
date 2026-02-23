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
    fprintf(out, "Usage: cs [options] <file.c> [--] [args...]\n"
                 "\n"
                 "Options:\n"
                 "  -u, --update          Update cs to latest release\n"
                 "  -v, --version         Print version\n"
                 "  -h, --help            Show this help\n");
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

static bool file_has_shebang(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    int c1 = fgetc(file);
    int c2 = fgetc(file);
    fclose(file);
    return c1 == '#' && c2 == '!';
}

static char *strip_shebang_to_temp(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        return NULL;
    }

    char tmp_path[] = "/tmp/cs-src-XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        fclose(in);
        return NULL;
    }

    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(tmp_path);
        fclose(in);
        return NULL;
    }

    bool first_line = true;
    int ch = 0;
    while ((ch = fgetc(in)) != EOF) {
        if (first_line) {
            if (ch == '\n') {
                first_line = false;
            }
            continue;
        }
        fputc(ch, out);
    }

    fclose(in);
    fclose(out);
    return dup_string(tmp_path);
}

static bool write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, file);
    fclose(file);
    return written == len;
}

static bool append_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "ab");
    if (!file) {
        return false;
    }
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, file);
    fclose(file);
    return written == len;
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

static const char *get_config_dir(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return xdg;
    }
    return getenv("HOME");
}

static bool file_contains_markers(const char *path, const char *begin,
                                  const char *end) {
    char *text = read_file_text(path);
    if (!text) {
        return false;
    }
    bool ok = strstr(text, begin) && strstr(text, end);
    free(text);
    return ok;
}

static bool completion_file_needs_update(const char *path) {
    char *text = read_file_text(path);
    if (!text) {
        return true;
    }
    bool ok = strstr(text, "complete -o filenames -F _cs_files cs") != NULL &&
              strstr(text, "CS_HIDE_DOTFILES") != NULL &&
              strstr(text, "CS_ONLY_C_FILES") != NULL;
    free(text);
    return !ok;
}

static void ensure_completion_ready(void) {
    const char *skip = getenv("CS_SKIP_COMPLETION_CHECK");
    if (skip && strcmp(skip, "1") == 0) {
        return;
    }

    const char *active = getenv("CS_BASH_COMPLETION_ACTIVE");
    if (active && strcmp(active, "1") == 0) {
        return;
    }

    const char *config_root = get_config_dir();
    if (!config_root || config_root[0] == '\0') {
        return;
    }

    char config_dir[PATH_MAX];
    char completions_dir[PATH_MAX];
    char completion_file[PATH_MAX];
    const char *config_suffix = ".config/cs";
    if (getenv("XDG_CONFIG_HOME") && getenv("XDG_CONFIG_HOME")[0] != '\0') {
        config_suffix = "cs";
    }

    size_t config_needed = strlen(config_root) + 1 + strlen(config_suffix) + 1;
    if (config_needed > sizeof(config_dir)) {
        return;
    }
    strcpy(config_dir, config_root);
    strcat(config_dir, "/");
    strcat(config_dir, config_suffix);

    size_t completions_needed = strlen(config_dir) + strlen("/completions") + 1;
    if (completions_needed > sizeof(completions_dir)) {
        return;
    }
    strcpy(completions_dir, config_dir);
    strcat(completions_dir, "/completions");

    size_t completion_needed = strlen(completions_dir) + strlen("/cs.bash") + 1;
    if (completion_needed > sizeof(completion_file)) {
        return;
    }
    strcpy(completion_file, completions_dir);
    strcat(completion_file, "/cs.bash");

    if (!ensure_dir(completions_dir)) {
        return;
    }

    if (completion_file_needs_update(completion_file)) {
        const char *script =
            "# cs bash completion (CS_HIDE_DOTFILES CS_ONLY_C_FILES)\n"
            "if [[ -z \"${CS_BASH_COMPLETION_ACTIVE:-}\" ]]; then\n"
            "    export CS_BASH_COMPLETION_ACTIVE=1\n"
            "fi\n"
            "\n"
            "_cs_files() {\n"
            "    local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
            "    local cmd=\"${COMP_WORDS[0]##*/}\"\n"
            "    local hide_dotfiles=1\n"
            "    [[ \"$cur\" == .* ]] && hide_dotfiles=0\n"
            "\n"
            "    [[ \"$cmd\" == \"cs\" ]] || return 0\n"
            "    [[ $COMP_CWORD -eq 1 ]] || return 0\n"
            "\n"
            "    COMPREPLY=()\n"
            "    while IFS= read -r f; do\n"
            "        local f_base=\"${f##*/}\"\n"
            "        if (( hide_dotfiles )) && [[ \"$f_base\" == .* ]]; then\n"
            "            continue\n"
            "        fi\n"
            "        if [[ \"$f\" == *.c ]]; then\n"
            "            COMPREPLY+=(\"$f\")\n"
            "        fi\n"
            "    done < <(compgen -f -X '!*.c' -- \"$cur\")\n"
            "    return 0\n"
            "}\n"
            "\n"
            "complete -o filenames -F _cs_files cs\n";

        write_text_file(completion_file, script);
    }

    const char *begin = "# >>> cs bash completion >>>";
    const char *end = "# <<< cs bash completion <<<";

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return;
    }

    char bashrc[PATH_MAX];
    char bash_profile[PATH_MAX];
    char profile[PATH_MAX];
    snprintf(bashrc, sizeof(bashrc), "%s/.bashrc", home);
    snprintf(bash_profile, sizeof(bash_profile), "%s/.bash_profile", home);
    snprintf(profile, sizeof(profile), "%s/.profile", home);

    bool has_marker = file_contains_markers(bashrc, begin, end) ||
                      file_contains_markers(bash_profile, begin, end) ||
                      file_contains_markers(profile, begin, end);

    if (has_marker) {
        return;
    }

    const char *target_rc =
        file_exists(bashrc) ? bashrc
                            : (file_exists(bash_profile)
                                   ? bash_profile
                                   : (file_exists(profile) ? profile : bashrc));

    const char *prefix = "\n";
    const char *if_start = "\nif [ -f \"";
    const char *if_mid = "\" ]; then\n    source \"";
    const char *if_end = "\"\nfi\n";
    const char *suffix = "\n";
    size_t block_needed = strlen(prefix) + strlen(begin) + strlen(if_start) +
                          strlen(completion_file) + strlen(if_mid) +
                          strlen(completion_file) + strlen(if_end) +
                          strlen(end) + strlen(suffix) + 1;
    char block[PATH_MAX * 2];
    if (block_needed > sizeof(block)) {
        return;
    }
    block[0] = '\0';
    strcat(block, prefix);
    strcat(block, begin);
    strcat(block, if_start);
    strcat(block, completion_file);
    strcat(block, if_mid);
    strcat(block, completion_file);
    strcat(block, if_end);
    strcat(block, end);
    strcat(block, suffix);

    if (!append_text_file(target_rc, block)) {
        fprintf(
            stderr,
            "cs bash completion is not active; continuing without completion.\n"
            "Failed to update %s.\n"
            "Completion script location: %s\n",
            target_rc, completion_file);
        return;
    }

    fprintf(stderr,
            "cs bash completion enabled. Reload your shell or run: source %s\n",
            target_rc);
}

static int perform_update(void) {
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

    char install_url[512];
    snprintf(install_url, sizeof(install_url),
             "https://raw.githubusercontent.com/%s/%s/main/install.sh", owner,
             repo);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -fsSL \"%s\" | CS_REPO=%s/%s sh",
             install_url, owner, repo);

    char api_url[512];
    snprintf(api_url, sizeof(api_url),
             "https://api.github.com/repos/%s/%s/releases/latest", owner, repo);

    char curl_cmd[768];
    snprintf(curl_cmd, sizeof(curl_cmd), "curl -fsSL \"%s\"", api_url);
    char *json = read_command_output(curl_cmd);
    if (!json) {
        fprintf(stderr,
                "Unable to determine latest version; attempting upgrade...\n");
        return system(cmd);
    }

    char *tag = json_find_string(json, "tag_name");
    if (!tag) {
        fprintf(stderr,
                "Unable to determine latest version; attempting upgrade...\n");
        free(json);
        return system(cmd);
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

    printf("Upgrading to cs %s...\n", latest);
    int rc = system(cmd);
    free(tag);
    free(json);
    return rc;
}

int main(int argc, char **argv) {
    const char *cc = "cc";
    char *cflags = NULL;
    char *ldflags = NULL;
    char *cache_dir = NULL;

    const char *source_path = NULL;
    int args_index = -1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc) {
                source_path = argv[i + 1];
                args_index = i + 1;
            }
            break;
        }

        if (!source_path && arg[0] == '-') {
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
                print_usage(stdout);
                return 0;
            }
            if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
                printf("cs %s\n", CS_VERSION);
                return 0;
            }
            if (strcmp(arg, "--update") == 0 || strcmp(arg, "-u") == 0) {
                return perform_update();
            }
            fprintf(stderr, "Unknown option: %s\n", arg);
            return 1;
        }

        if (!source_path) {
            source_path = arg;
            if (i + 1 < argc) {
                args_index = i + 1;
            }
            break;
        }
    }

    ensure_completion_ready();

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

    if (!cache_dir) {
        fprintf(stderr, "Failed to resolve cache dir\n");
        return 1;
    }

    if (!ensure_dir(cache_dir)) {
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
    snprintf(output_path, sizeof(output_path), "%s/%s-%016llx", cache_dir, base,
             (unsigned long long)hash);

    bool need_compile = !file_exists(output_path);

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

        char *compile_source = NULL;
        char *compile_cflags = NULL;
        if (file_has_shebang(source_path)) {
            compile_source = strip_shebang_to_temp(source_path);
            if (!compile_source) {
                fprintf(stderr, "Failed to preprocess shebang\n");
                free(exe_dir);
                return 1;
            }
            if (cflags) {
                compile_cflags = dup_string(cflags);
                if (!compile_cflags) {
                    fprintf(stderr, "Failed to allocate cflags\n");
                    unlink(compile_source);
                    free(compile_source);
                    free(exe_dir);
                    return 1;
                }
            }
            if (!append_flag(&compile_cflags, "-x c")) {
                fprintf(stderr, "Failed to set shebang cflags\n");
                if (compile_cflags) {
                    free(compile_cflags);
                }
                unlink(compile_source);
                free(compile_source);
                free(exe_dir);
                return 1;
            }
        }

        const char *source_for_compile =
            compile_source ? compile_source : source_path;
        char *command = build_compile_command(
            cc, include_path, compile_cflags ? compile_cflags : cflags,
            source_for_compile, output_path, ldflags);
        if (!command) {
            fprintf(stderr, "Failed to build compile command\n");
            if (compile_source) {
                unlink(compile_source);
                free(compile_source);
            }
            if (compile_cflags) {
                free(compile_cflags);
            }
            free(exe_dir);
            return 1;
        }

        int compile_status = system(command);
        free(command);
        free(exe_dir);
        if (compile_source) {
            unlink(compile_source);
            free(compile_source);
        }
        if (compile_cflags) {
            free(compile_cflags);
        }

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

    if (args_index > 0) {
        exec_argv[0] = argv[args_index];
        for (int i = args_index + 1; i < argc; i++) {
            exec_argv[i - args_index] = argv[i];
        }
    } else {
        exec_argv[0] = (char *)source_path;
    }
    exec_argv[exec_argc] = NULL;

    execv(output_path, exec_argv);
    fprintf(stderr, "Failed to run %s: %s\n", output_path, strerror(errno));
    free(exec_argv);
    return 1;
}
