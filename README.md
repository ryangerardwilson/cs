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
curl -fsSL https://raw.githubusercontent.com/ryangerardwilson/cs/main/install.sh | bash
```

Install a specific version:

```sh
curl -fsSL https://raw.githubusercontent.com/ryangerardwilson/cs/main/install.sh | bash -s -- -v 0.1.0
```

## Build

```sh
make
```

Binary: `bin_cs`

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

`cs -v` prints the installed app version from the runtime `_version.py`
source. The checked-in file stays at `0.0.0`; tagged release automation stamps
the shipped binary with the real version.

## Update

`cs -u` delegates to `install.sh -u`.

```sh
cs -v
cs -u
```

## Install requirements

`install.sh` requires `curl` plus `sha256sum` or `shasum`.

## Helper header

`cs.h` provides small utilities for file IO, process helpers, and directory listing.

## Bash completion

`cs` auto-generates a bash completion script in your config dir and adds a
source block to your shell rc file the first time it runs.

To skip this behavior, set `CS_SKIP_COMPLETION_CHECK=1`.
