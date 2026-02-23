# cs

`cs` is for people who want to use C for real, everyday scripting tasks without
falling back to Python/Node or brittle bash one-liners. It removes the
compile/run friction so you can write small utilities in C, run them
immediately, and build practical skills on real problems instead of getting
stuck in leetcode-style exercises.

If you're trying to improve at C by building quick tools that actually work,
`cs` gives you the scripting ergonomics while keeping C's performance,
portability, and single-binary output.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/ryangerardwilson/cs/main/install.sh | CS_REPO=ryangerardwilson/cs sh
```

## Build

```sh
make
```

Binary: `cs`

## Usage

```sh
cs test_hello.c -- arg1 arg2
```

Enable shebang usage:

```c
#!/usr/bin/env cs
#include <stdio.h>

int main(void) {
    puts("hello");
    return 0;
}
```

```sh
chmod +x test_hello.c
./test_hello.c
```

## Options

- `-v, --version`
- `-u, --update`
- `-h, --help`

## Versioning

Release builds use the git tag as the version (tags like `v0.1.1`).

## Update

`cs -u` runs the GitHub install script under the hood. Set `CS_REPO_OWNER`
and `CS_REPO_NAME`, or `CS_REPO=owner/repo`.

## Install requirements

`install.sh` requires `python3`. For private repos or rate-limited API calls,
export `GITHUB_TOKEN` or `GH_TOKEN`.

## Helper header

`cs.h` provides small utilities for file IO, process helpers, and directory listing.

## Bash completion

`cs` auto-generates a bash completion script in your config dir and adds a
source block to your shell rc file the first time it runs.

To skip this behavior, set `CS_SKIP_COMPLETION_CHECK=1`.
