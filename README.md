# cs

Use C like a scripting language by compiling and running in one step.

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
- `--help`
- `--version`

## Helper header

`cs.h` provides small utilities for file IO, process helpers, and directory listing.
