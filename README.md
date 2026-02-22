# cs

Use C like a scripting language by compiling and running in one step.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/ryangerardwilson/cs/main/install.sh | CS_REPO=ryangerardwilson/cs sh
```

## Build

```sh
make
```

Binary: `bin_cs`

## Usage

```sh
./bin_cs test_hello.c -- arg1 arg2
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

- `--cc <compiler>`
- `--cflags <flags>`
- `--ldflags <flags>`
- `--cache-dir <dir>`
- `--no-cache`
- `--verbose`
- `-v, --version`
- `-u, --update`
- `--help`

## Versioning

Update `VERSION` before tagging. Releases build on tags like `v0.1.1`.

## Update

`cs -u` uses GitHub Releases. Set `CS_REPO_OWNER` and `CS_REPO_NAME`, or
`CS_REPO=owner/repo`.

## Helper header

`cs.h` provides small utilities for file IO, process helpers, and directory listing.
