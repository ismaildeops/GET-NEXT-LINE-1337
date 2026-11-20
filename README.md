*This project has been created as part of the 42 curriculum by ilaakoub.*

# get_next_line

## Description

**Goal:** write a function `get_next_line(int fd)` that reads a file descriptor
and returns **one line at a time**, including the `\n` at the end (if there is one).
It returns `NULL` when there is nothing more to read, or when an error happens.

**Overview:** the function reads the file in small blocks of `BUFFER_SIZE` bytes
with `read()`. A block can contain half a line, one line, or many lines, so the
function must remember what it has already read between two calls. To do this,
it uses a **static variable**, which keeps its value between calls.

### How it works

Variables used:

- `save` : static string. It keeps the text that was read but not returned yet.
- `buffer` : temporary memory that receives the new data from `read()`.
- `bytes_read` : the number of bytes that `read()` returned.
- `line` : the line that we return to the caller.

Steps of each call:

1. Check the arguments (`BUFFER_SIZE`, `fd`) and allocate `buffer`.
2. While `save` has no `\n`: call `read()`, then join the new data to `save`.
   Stop if `read()` returns `0` (end of file) or `-1` (error).
3. Copy from `save` into a new string `line`, up to and including the first `\n`.
4. Create a new string with what is left after the `\n`. This becomes the new
   `save`, and the old `save` is freed.
5. At the end of the file or on error: free `save`, set it to `NULL`, return `NULL`.

All memory allocated with `malloc` is freed on every path (no memory leaks).

## Instructions

Files: `get_next_line.c`, `get_next_line_utils.c`, `get_next_line.h`.

**Compilation:** `BUFFER_SIZE` can be chosen at compile time with `-D`.

```
cc -Wall -Wextra -Werror -D BUFFER_SIZE=42 get_next_line.c get_next_line_utils.c main.c -o gnl
```

**Usage:** include `get_next_line.h`, open a file to get a `fd`, and call
`get_next_line(fd)` in a loop until it returns `NULL`.
The caller must `free()` every line that is returned.

**Execution:**

```
./gnl file.txt
```

**Test for leaks:**

```
valgrind --leak-check=full ./gnl file.txt
```

## Resources

- `man 2 read`, `man 2 open`, `man 3 malloc`, `man 3 free`
- Valgrind documentation: https://valgrind.org/docs/manual/quick-start.html
- The get_next_line subject (42 curriculum)

**Use of AI:** I used AI (Claude) as a tutor, not to write the project code.
It was used for:

- explaining ideas (static variables, how `read()` works, who owns and frees memory),
- reviewing my code and pointing out problems (missing `'\0'`, memory leaks,
  dangling pointer), so that I could fix them myself,
- suggesting a small `main.c` and valgrind commands to test the function,
- helping to write this README.

I wrote the code of `get_next_line` myself and I can explain every line of it.

### HOW CODE WORKS
1. Goal of get_next_line is read a file and return one line on every call, and save other lines 
2. we call save as a static because we want to its value after the function end.
3. while (!has_newline(save)). 