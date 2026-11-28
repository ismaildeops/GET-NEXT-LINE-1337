*This project has been created as part of the 42 curriculum by ilaakoub.*

# get_next_line

## Description

**get_next_line** is a function that reads a file descriptor and returns
one line at a time, each time it is called. The goal is to learn how
`static` variables keep their value between calls, how `read()` works on a
file descriptor, and how to manage heap memory without leaks.

The function prototype is:

```c
char *get_next_line(int fd);
```

### Behavior

- Each call returns the next line of the file, **including the terminating
  `\n`**. The only exception is the last line of a file that does not end
  with `\n`.
- It returns `NULL` when there is nothing else to read, or when an error
  occurred.
- It works on regular files and on the standard input.
- The returned line is allocated with `malloc()`: the caller must free it.
- Allowed external functions: `read`, `malloc`, `free`. The use of `libft`,
  `lseek()` and global variables is forbidden.

## Instructions

### Compilation

There is no Makefile. The files are compiled together with a test program,
using `cc` and the flags `-Wall -Wextra -Werror`. The size of the buffer
given to `read()` is set with `-D BUFFER_SIZE=n`:

```bash
cc -Wall -Wextra -Werror -D BUFFER_SIZE=42 \
    get_next_line.c get_next_line_utils.c main.c -o gnl
```

The project also compiles without the `-D BUFFER_SIZE` flag: a default
value is defined in `get_next_line.h`.

### Usage

Include the header, open a file, and call `get_next_line()` in a loop until
it returns `NULL`. Free every line after using it:

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "get_next_line.h"

int main(void)
{
    int     fd;
    char    *line;

    fd = open("test.txt", O_RDONLY);
    if (fd < 0)
        return (1);
    while ((line = get_next_line(fd)))
    {
        printf("%s", line);
        free(line);
    }
    close(fd);
    return (0);
}
```

To test different buffer sizes:

```bash
for n in 1 42 9999 10000000; do
    cc -Wall -Wextra -Werror -D BUFFER_SIZE=$n \
        get_next_line.c get_next_line_utils.c main.c -o gnl && ./gnl
done
```

### Project structure

```
- README.md
- get_next_line.h        # prototype, BUFFER_SIZE default, helper prototypes
- get_next_line.c        # get_next_line() and its static helpers
- get_next_line_utils.c  # ft_strlen, ft_strjoin, has_newline
```

## Algorithm and data structure

**Data structure.** The only state kept between two calls is one
`static char *save`: a heap-allocated string that contains the characters
already read from the file but not yet returned to the caller. A temporary
`buffer` of `BUFFER_SIZE + 1` bytes is used for each call to `read()` and is
freed before the function returns.

**Algorithm.** Each call to `get_next_line(fd)` does three steps:

1. **Read into `save`.** While `save` does not contain a `\n`, call
   `read(fd, buffer, BUFFER_SIZE)`. If `read()` returns a positive number
   of bytes, end the buffer with `'\0'` and append it to `save` with
   `ft_strjoin()`. If `read()` returns `0` (end of file) or `-1` (error),
   stop reading.
2. **Check if there is something to return.** If `save` is `NULL` or empty,
   free it, set it to `NULL` and return `NULL`.
3. **Extract the line and update `save`.**
   - `extract_line()` copies the characters from the start of `save` up to
     and including the first `\n` (or up to the end of the string) into a
     new string: this is the returned line.
   - `update_save()` allocates a new string with the characters that come
     after the line, frees the old `save`, and replaces it.

Example with the file `"ab\ncd\n"` and `BUFFER_SIZE = 42`:

```
call 1: read -> save = "ab\ncd\n"   returns "ab\n"   save = "cd\n"
call 2: no read needed (\n in save)  returns "cd\n"   save = ""
call 3: read returns 0, save empty   returns NULL
```

**Why this design.**

- A `static` variable is the only way to keep the extra characters between
  calls without a global variable (forbidden). `read()` may read more than
  one line at once, and the kernel does not let us read those bytes again,
  so they must be saved.
- The loop stops as soon as a `\n` is in `save`, so the function reads as
  little as possible and never reads the whole file at once.
- The line is built from `save` and not directly from `buffer`, so the
  result is correct for any `BUFFER_SIZE` (1, 9999, 10000000...) and for
  lines longer or shorter than the buffer.
- `buffer` is freed at every call, and `save` is freed when the end of the
  file is reached, so there are no memory leaks.

**Trade-off.** `ft_strjoin()` allocates a new string at each read, so a
very long line read with a very small `BUFFER_SIZE` costs many copies. This
choice keeps the code simple and the Norm-compliant helpers short.

## Resources

### References

- `man 2 read`
- `man 3 malloc`
- [Storage duration and static variables in C (cppreference)](https://en.cppreference.com/w/c/language/storage_duration)

### Use of AI

- AI was used to explain how `static` variables keep their value between
  calls, and how `read()` moves the offset of a file descriptor.
- AI was used to review my code: it pointed out where a bug or a memory
  leak was, and I fixed the code myself.
- AI was used as a mock evaluator, to ask me defense questions about my
  implementation.
- AI was used to help write this README.
- All the code was written by me.
