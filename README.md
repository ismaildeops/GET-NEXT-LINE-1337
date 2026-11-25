*This project has been created as part of the 42 curriculum by ilaakoub.*

# get_next_line

## Description

**Goal:** Write a function `get_next_line(int fd)` that reads a file descriptor and returns **one line at a time**, including the trailing `\n` (if present). It returns `NULL` when there is nothing left to read or when an error occurs.

**Overview:** Because `read()` processes files in fixed chunks of `BUFFER_SIZE` bytes, a single read may capture part of a line, one full line, or multiple lines. To remember leftover text between function calls, `get_next_line` uses a **`static` variable** (`save`), which preserves its value after the function returns.

### How It Works

1. **Validate & Read:** Check `fd` and `BUFFER_SIZE`. While `save` does not contain a `\n`, read chunks into `buffer` and append them to `save` until a `\n` is found, EOF (`0`) is reached, or an error (`-1`) occurs.
2. **Extract Line:** Allocate and copy characters from `save` up to and including the first `\n` into `line`.
3. **Update Remainder:** Store any remaining characters after `\n` in a newly allocated `save` string, and free the old `save`.
4. **Cleanup:** Return `line`. When EOF or an error is reached, free `save`, set it to `NULL`, and return `NULL`.

## Instructions

### Project Files
* `get_next_line.c`
* `get_next_line_utils.c`
* `get_next_line.h`

### Compilation
Compile your `main.c` with the project files and define `BUFFER_SIZE` using `-D`:

```bash
cc -Wall -Wextra -Werror -D BUFFER_SIZE=5 get_next_line.c get_next_line_utils.c main.c -o program
```

### Usage & Execution
Include `"get_next_line.h"`, open a file descriptor, and call `get_next_line(fd)` in a loop until it returns `NULL`. Remember to `free()` each returned line in the caller.

```bash
./gnl file.txt
```

### Checking for Memory Leaks
Compile with `-g` and run Valgrind (Memcheck) to verify there are no leaks or invalid memory accesses:

```bash
cc -Wall -Wextra -Werror -g -D BUFFER_SIZE=5 get_next_line.c get_next_line_utils.c main.c -o gnl
valgrind --leak-check=full ./program file.txt
```

## Resources

* **Manuals:** `man 2 read`, `man 2 open`, `man 3 malloc`, `man 3 free`
* **Documentation:** [Valgrind Quick Start Guide](https://valgrind.org/docs/manual/quick-start.html)
* **42 Curriculum:** `get_next_line` project subject

### Use of AI
AI tools were used strictly as a learning aid—not to generate project code:
* **Concepts:** Clarifying `static` variable lifetimes, `read()` behavior, and memory ownership.
* **Debugging & Review:** Identifying edge-case memory leaks and missing null-terminators so I could fix them myself.
* **Testing & Documentation:** Suggesting `main.c` test cases, Valgrind flags, and helping structure this `README.md`.