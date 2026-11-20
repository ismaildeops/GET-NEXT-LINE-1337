# get_next_line tester (mandatory part only)

Tests **your** `get_next_line.c`, `get_next_line_utils.c`, `get_next_line.h` without modifying them.
One file descriptor at a time - no bonus behaviour, no simultaneous fds.

## Quick start

```sh
# 1. put this folder inside your project, next to get_next_line.c:
#      my_gnl/get_next_line.c  get_next_line_utils.c  get_next_line.h  gnl_tester/
cd my_gnl/gnl_tester
make            # everything (about 5-10 minutes with the default 15 BUFFER_SIZE values)
make quick      # BUFFER_SIZE 1, 4, 42, 1024, plain + ASan (about 1 minute)
```
Your files elsewhere? `GNL_DIR=/path/to/project make`.

| command | what it does |
|---|---|
| `make` | static checks + plain run + AddressSanitizer/UBSan run (+ valgrind if installed) |
| `make quick` | fewer BUFFER_SIZE values, no valgrind |
| `make asan` / `make valgrind` | only that tool |
| `make heavy` | adds the 1 MB-line tests (big BUFFER_SIZE only) |
| `make one BS=5 ID=B07` | run a single test at one BUFFER_SIZE, with its details |
| `make helpers` | your helper functions (template, see below) |
| `make clean` | remove `build/` and `results/` |

Options: `BUFFER_SIZES="1 2 42" make`, `MODES="plain asan" make`, `LEVEL=0|1|2`, `CC=clang`.
Results: `results/report.txt` (final report), `results/<mode>_bs<N>.log` (every test + reasons + sanitizer output).

## What is tested

Default BUFFER_SIZE values: 1 2 3 4 5 7 8 16 32 42 100 1024 4096 9999 10000000
(+ two extra builds with BUFFER_SIZE=0 and -1, robustness only).
Every test runs in its **own process** (a crash, a hang or a stale `static` variable cannot leak into the next test).

| Section of your brief | Category in the report | Where |
|---|---|---|
| A Basic functionality | Basic functionality (A..) | empty file, 1 char/word, with/without final `\n`, only newlines, mixed empty lines... |
| B BUFFER_SIZE | Buffer boundaries (B..) | lines of BS-2, BS-1, BS, BS+1, 2BS-1, 2BS, 2BS+1, 3BS chars; runs of newlines; staircase lengths; 8 fuzz files; **partial reads** (read() returning 1..n bytes) |
| C Advanced content | Advanced content (C..) | 10 KB / ~100 KB lines, 1 MB with `make heavy`, 5000-10000 lines, tabs, spaces, UTF-8, CRLF, alternating short/long lines |
| D EOF | EOF behavior (D..) | up to 100 calls after EOF, last line without `\n`, same file re-opened, several files one after another |
| E fd / errors | File descriptor and error handling (E..) | fd -1, 9999, closed fd, directory fd (EISDIR), write-only fd (EBADF), **stdin (fd 0) and pipes**, BUFFER_SIZE 0 / -1 |
| F Memory | Memory management (F..) | leak checks, **malloc() failure injection** at every allocation point, **read() failure injection**, plus ASan/UBSan/valgrind runs |
| G Helpers | Helper functions | `src/helpers_test.c` is a **template** - see below |

Checks done on every single line returned: exact content, `'\0'` terminator inside the allocation, pointer really
obtained from `malloc`, **heap overflow canary** (catches off-by-one `malloc` sizes even without ASan), memory
filled with `0xA5` on `malloc` and `0xDD` on `free` (exposes missing terminators / use of freed data), and that after
the `NULL` at EOF **nothing allocated by your code is still alive** (leak, or `save` kept after EOF).

How it hooks your code without touching it: your `.c` files are compiled with `-include src/gnlt_hook.h`, which
renames `malloc/free/read` to instrumented versions. That is what makes leak accounting and fault injection possible.

### Mandatory vs robustness: `[M]` / `[R]`
* `[M]` = follows from the subject: one line per call with its `\n`; `NULL` at EOF or on error; works on files and
  stdin; any BUFFER_SIZE given with `-D`; no leaks; no crash; only `read/malloc/free`; no global variables; header guards.
* `[R]` = extra robustness that the subject does not define: BUFFER_SIZE <= 0, `malloc()` returning NULL, `read()` failing
  half-way. A `[R]` failure is **not** a mandatory bug. Check the wording against *your* version of the subject PDF.
* Not tested because the subject calls it undefined: a file that changes between calls, binary files.

## Reading a failure
```
FAIL    [M] B07 3 lines of BS-1=41 chars, last without \n
          reason: line #3 differs (first difference at byte 0): expected 41 bytes "..." / got 0 bytes ""
          why this test exists: Line length vs BUFFER_SIZE: the '\n' lands on the last byte of a read ...
```
Statuses: `FAIL` wrong output/leak - `CRASH` signal (segfault, abort from a double free) - `TIMEOUT` infinite loop or extremely slow -
`SAN` ASan/UBSan/valgrind error (first line shown, full stack trace in the log). `(12.3s)` after a name = slow
(usually quadratic string joining); not a failure by itself.

## Exact manual commands (if you want to run your own `main.c`)
```sh
# AddressSanitizer + UBSan
cc -Wall -Wextra -Werror -g -D BUFFER_SIZE=42 -fsanitize=address,undefined -fno-omit-frame-pointer \
   get_next_line.c get_next_line_utils.c main.c -o gnl_asan && ASAN_OPTIONS=detect_leaks=1 ./gnl_asan file.txt
# valgrind (Linux)
cc -Wall -Wextra -Werror -g -D BUFFER_SIZE=42 get_next_line.c get_next_line_utils.c main.c -o gnl_vg
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./gnl_vg file.txt
# without -D BUFFER_SIZE (must compile too)
cc -Wall -Wextra -Werror -c get_next_line.c get_next_line_utils.c
```

## Helper functions (section G)
Helpers differ in every project (names, argument order, who frees what), so `src/helpers_test.c` is a template:
fill in the tests for your own prototypes, set `ADAPTED` to 1, run `make helpers` (built with ASan/UBSan).
Until then the report shows "-" for that row - it is not counted as passed.

## Limits - what a green run does NOT prove
* Tests are finite; it is strong evidence, not a proof. Valgrind and macOS were not available where this tester was developed;
  the valgrind mode is skipped automatically if `valgrind` is missing, and `--wrap`-style linker tricks are not used,
  so it should build with gcc or clang on Linux/macOS.
* Timing is only reported (and a 40 s-per-test timeout applies); evaluators may be stricter or looser.
* Behaviour with several fds at once (bonus) is deliberately not tested.
