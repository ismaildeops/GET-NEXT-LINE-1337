/* ************************************************************************** */
/*  harness.c  -  get_next_line tester (MANDATORY part only, one fd at a      */
/*  time).  Every test runs in its own forked process, so a crash, a hang or  */
/*  a stale static `save` in one test can never contaminate the next one.     */
/* ************************************************************************** */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "get_next_line.h"

#ifndef BUFFER_SIZE
# define BUFFER_SIZE 42
#endif
#define BSV ((long)(BUFFER_SIZE))

/* ======================================================================== */
/*  1. Instrumented malloc / free / read (used by YOUR code via the hook)   */
/* ======================================================================== */
void	*gnlt_malloc(size_t n);
void	gnlt_free(void *p);
ssize_t	gnlt_read(int fd, void *buf, size_t n);

typedef struct {
	unsigned long	magic;
	size_t			size;
	unsigned long	seq;
	unsigned long	tracked;
	unsigned long	pad0;
	unsigned long	pad1;
}	Hdr;

#define HMAGIC		0x474E4C48444F4B31UL
#define CANARY_LEN	16
#define CANARY_BYTE	0xC3
#define FILL_MAX	65536
#define REC_MAX		(1u << 18)

static int				g_track;
static unsigned long	g_malloc_calls, g_seq, g_read_calls;
static long				g_live_cnt;
static size_t			g_live_bytes;
static unsigned long	g_fail_malloc_at, g_fail_read_at;
static int				g_fail_triggered, g_read_fail_triggered, g_short_reads;
static unsigned			g_rng = 2463534242u;
static struct { size_t size; unsigned char alive; } g_rec[REC_MAX];

static char	g_reason[1024];
static int	g_reason_fd = -1;

static unsigned	rnd(void)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return (g_rng);
}

static void	die_fail(const char *fmt, ...) __attribute__((noreturn));
static void	die_fail(const char *fmt, ...)
{
	va_list	ap;
	ssize_t	r;

	va_start(ap, fmt);
	vsnprintf(g_reason, sizeof g_reason, fmt, ap);
	va_end(ap);
	if (g_reason_fd >= 0)
		r = write(g_reason_fd, g_reason, strlen(g_reason));
	(void)r;
	_exit(10);
}

void	*gnlt_malloc(size_t n)
{
	unsigned char	*base;
	Hdr				*h;

	if (g_track)
	{
		g_malloc_calls++;
		if (g_fail_malloc_at && g_malloc_calls == g_fail_malloc_at)
		{
			g_fail_triggered = 1;
			errno = ENOMEM;
			return (NULL);
		}
	}
	if (n > ((size_t)-1) / 2)
	{
		errno = ENOMEM;
		return (NULL);
	}
	base = malloc(sizeof(Hdr) + n + CANARY_LEN);
	if (!base)
		return (NULL);
	h = (Hdr *)base;
	h->magic = HMAGIC;
	h->size = n;
	h->tracked = (unsigned long)g_track;
	h->seq = g_track ? ++g_seq : 0;
	memset(base + sizeof(Hdr), 0xA5, n < FILL_MAX ? n : FILL_MAX);
	memset(base + sizeof(Hdr) + n, CANARY_BYTE, CANARY_LEN);
	if (g_track)
	{
		g_live_cnt++;
		g_live_bytes += n;
		g_rec[h->seq % REC_MAX].size = n;
		g_rec[h->seq % REC_MAX].alive = 1;
	}
	return (base + sizeof(Hdr));
}

void	gnlt_free(void *p)
{
	Hdr				*h;
	unsigned char	*u;
	size_t			i;

	if (!p)
		return ;
	u = (unsigned char *)p;
	h = (Hdr *)(u - sizeof(Hdr));
	if (h->magic != HMAGIC)
		die_fail("free() was called on a pointer that is not a live malloc'd "
			"block: double free, free of an offset pointer (e.g. free(line + i)), "
			"or memory from a forbidden function (calloc/realloc/strdup...)");
	for (i = 0; i < CANARY_LEN; i++)
		if (u[h->size + i] != CANARY_BYTE)
			die_fail("heap buffer overflow: your code wrote past the end of a "
				"%zu-byte malloc block (off-by-one in the malloc size, e.g. "
				"missing room for the final '\\0')", h->size);
	if (h->tracked)
	{
		g_live_cnt--;
		g_live_bytes -= h->size;
		g_rec[h->seq % REC_MAX].alive = 0;
	}
	memset(u, 0xDD, h->size < FILL_MAX ? h->size : FILL_MAX);
	h->magic = 0;
	free(h);
}

ssize_t	gnlt_read(int fd, void *buf, size_t n)
{
	if (g_track)
	{
		g_read_calls++;
		if (g_fail_read_at && g_read_calls == g_fail_read_at)
		{
			g_read_fail_triggered = 1;
			errno = EIO;
			return (-1);
		}
		if (g_short_reads && n > 1)
		{
			unsigned r = rnd();
			size_t lim = (r & 1) ? 8 : n;
			if (lim > n)
				lim = n;
			n = 1 + (r >> 1) % lim;
		}
	}
	return (read(fd, buf, n));
}

static const char	*leak_report(void)
{
	static char		buf[320];
	size_t			off;
	int				shown;
	unsigned long	s;

	shown = 0;
	off = (size_t)snprintf(buf, sizeof buf, "%ld allocation(s) / %zu byte(s) "
			"still allocated", g_live_cnt, g_live_bytes);
	for (s = 1; s <= g_seq && s <= REC_MAX && shown < 3; s++)
	{
		if (g_rec[s % REC_MAX].alive && off < sizeof buf - 70)
		{
			off += (size_t)snprintf(buf + off, sizeof buf - off,
					"%s malloc call #%lu (%zu bytes)",
					shown ? "," : " - e.g.", s, g_rec[s % REC_MAX].size);
			shown++;
		}
	}
	return (buf);
}

/* ======================================================================== */
/*  2. Small helpers: dynamic buffer, escaping, results                      */
/* ======================================================================== */
typedef struct { char *d; size_t n, cap; } Buf;

static void	b_reserve(Buf *b, size_t extra)
{
	size_t	nc;
	char	*p;

	if (b->n + extra + 1 <= b->cap)
		return ;
	nc = b->cap ? b->cap : 256;
	while (nc < b->n + extra + 1)
		nc *= 2;
	p = realloc(b->d, nc);
	if (!p)
	{
		fprintf(stderr, "tester: out of memory\n");
		_exit(2);
	}
	b->d = p;
	b->cap = nc;
}

static void	b_add(Buf *b, const char *s, size_t n)
{
	b_reserve(b, n);
	memcpy(b->d + b->n, s, n);
	b->n += n;
	b->d[b->n] = 0;
}

static void	b_rep(Buf *b, char c, size_t n)
{
	b_reserve(b, n);
	memset(b->d + b->n, c, n);
	b->n += n;
	b->d[b->n] = 0;
}

static void	b_pat(Buf *b, size_t n, unsigned seed)
{
	static const char	al[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVW"
		"XYZ0123456789 .,;:-_";
	unsigned			s;
	size_t				i;

	b_reserve(b, n);
	s = seed * 2654435761u + 12345u;
	for (i = 0; i < n; i++)
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		b->d[b->n + i] = al[s % (sizeof al - 1)];
	}
	b->n += n;
	b->d[b->n] = 0;
}

static void	b_free(Buf *b)
{
	free(b->d);
	b->d = NULL;
	b->n = b->cap = 0;
}

static const char	*esc(const char *s, size_t n)
{
	static char	buf[4][300];
	static int	k;
	char		*o;
	size_t		i, j;
	unsigned char	c;

	o = buf[k++ & 3];
	j = 0;
	for (i = 0; i < n && j < 240; i++)
	{
		c = (unsigned char)s[i];
		if (c == '\n')
		{ o[j++] = '\\'; o[j++] = 'n'; }
		else if (c == '\t')
		{ o[j++] = '\\'; o[j++] = 't'; }
		else if (c == '\r')
		{ o[j++] = '\\'; o[j++] = 'r'; }
		else if (c == 0)
		{ o[j++] = '\\'; o[j++] = '0'; }
		else if (c < 32 || c > 126)
			j += (size_t)snprintf(o + j, 8, "\\x%02x", c);
		else
			o[j++] = (char)c;
	}
	if (i < n)
	{ o[j++] = '.'; o[j++] = '.'; o[j++] = '.'; }
	o[j] = 0;
	return (o);
}

static const char	*esc_win(const char *s, size_t n, size_t at)
{
	size_t	start, len;

	start = at > 12 ? at - 12 : 0;
	len = n - start;
	if (len > 40)
		len = 40;
	return (esc(s + start, len));
}

static int	fail(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(g_reason, sizeof g_reason, fmt, ap);
	va_end(ap);
	return (10);
}

static double	now(void)
{
	struct timespec	ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

/* ---- temp files ------------------------------------------------------- */
static char	g_tmpdir[256];
static int	g_fcount;

static void	new_tmp_path(char *out, size_t sz)
{
	snprintf(out, sz, "%s/f.%d.%d", g_tmpdir, (int)getpid(), ++g_fcount);
}

static int	write_file(const char *path, const char *data, size_t n)
{
	int		fd;
	size_t	off;
	ssize_t	w;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return (-1);
	off = 0;
	while (off < n)
	{
		w = write(fd, data + off, n - off);
		if (w <= 0)
		{
			close(fd);
			return (-1);
		}
		off += (size_t)w;
	}
	close(fd);
	return (0);
}

static void	cleanup_tmpdir(void)
{
	DIR				*d;
	struct dirent	*e;
	char			p[600];

	d = opendir(g_tmpdir);
	if (!d)
		return ;
	while ((e = readdir(d)))
	{
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue ;
		snprintf(p, sizeof p, "%s/%s", g_tmpdir, e->d_name);
		unlink(p);
	}
	closedir(d);
	rmdir(g_tmpdir);
}

/* ======================================================================== */
/*  3. Calling get_next_line and checking a whole stream                    */
/* ======================================================================== */
static char	*call_gnl(int fd)
{
	char	*r;

	g_track = 1;
	r = get_next_line(fd);
	g_track = 0;
	return (r);
}

static size_t	safe_len(const char *line)
{
	const Hdr	*h;
	const char	*z;

	h = (const Hdr *)line - 1;
	if (h->magic != HMAGIC)
		return (strlen(line));
	z = memchr(line, 0, h->size);
	return (z ? (size_t)(z - line) : h->size);
}

static int	validate_line(const char *line, const char *exp, size_t explen,
				int idx)
{
	const Hdr	*h;
	const char	*z;
	size_t		got, i;

	h = (const Hdr *)line - 1;
	if (h->magic != HMAGIC)
		return (fail("line #%d: the returned pointer was not obtained from "
				"malloc(), so the caller's free(line) would be invalid", idx));
	z = memchr(line, 0, h->size);
	if (!z)
		return (fail("line #%d: no '\\0' inside the %zu-byte allocation "
				"(missing terminator, or malloc size too small)", idx, h->size));
	got = (size_t)(z - line);
	if (got == explen && !memcmp(line, exp, explen))
		return (0);
	for (i = 0; i < got && i < explen && line[i] == exp[i]; i++)
		;
	return (fail("line #%d differs (first difference at byte %zu): expected %zu "
			"bytes \"%s\" / got %zu bytes \"%s\"", idx, i, explen,
			esc_win(exp, explen, i), got, esc_win(line, got, i)));
}

static int	check_stream(int fd, const char *data, size_t n, int extra)
{
	size_t		pos;
	size_t		explen;
	const char	*nl;
	char		*line;
	int			idx, rc, i;

	pos = 0;
	idx = 0;
	while (pos < n)
	{
		nl = memchr(data + pos, '\n', n - pos);
		explen = nl ? (size_t)(nl - (data + pos)) + 1 : n - pos;
		line = call_gnl(fd);
		idx++;
		if (!line)
			return (fail("call #%d returned NULL, but line #%d was expected "
					"(%zu bytes: \"%s\"): data lost, or EOF/error reported too "
					"early", idx, idx, explen, esc(data + pos, explen)));
		rc = validate_line(line, data + pos, explen, idx);
		gnlt_free(line);
		if (rc)
			return (rc);
		pos += explen;
	}
	for (i = 1; i <= extra; i++)
	{
		line = call_gnl(fd);
		if (line)
		{
			const char *e = esc(line, safe_len(line));
			rc = fail("call #%d (%d call(s) after the last line) returned \"%s\" "
					"instead of NULL: a line was returned twice, or a phantom "
					"(empty) line appears at EOF", idx + i, i, e);
			gnlt_free(line);
			return (rc);
		}
		if (g_live_cnt != 0)
			return (fail("[MEM] get_next_line returned NULL at EOF (call #%d) "
					"but memory is still allocated: %s - leak, or `save` kept "
					"after EOF", idx + i, leak_report()));
	}
	return (0);
}

/* ======================================================================== */
/*  4. Test registry                                                        */
/* ======================================================================== */
typedef void	(*GenFn)(Buf *, long, long, long);
typedef enum { M_FILE, M_SHORT, M_PIPE, M_STDIN, M_MALLOC_FAIL, M_READ_FAIL,
	M_CUSTOM }	Mode;
enum { C_BASIC, C_BOUND, C_ADV, C_EOF, C_FD, C_MEM, C_N };
static const char	*CATNAME[C_N] = {"Basic functionality", "Buffer boundaries",
	"Advanced content", "EOF behavior", "File descriptor and error handling",
	"Memory management"};
static const char	CATLET[C_N] = {'A', 'B', 'C', 'D', 'E', 'F'};

typedef struct {
	int			cat;
	char		id[8];
	char		name[150];
	int			mand;
	int			weight;
	Mode		mode;
	const char	*lit;
	size_t		litn;
	GenFn		gen;
	long		a, b, c;
	int			eof;
	int			(*custom)(void);
	const char	*why;
}	Test;

#define MAX_TESTS 700
static Test	g_tests[MAX_TESTS];
static int	g_nt;
static int	g_catcnt[C_N];
static int	g_skipped_notes;

static Test	*T(int cat, int mand, int weight, Mode mode, const char *why,
				const char *fmt, ...)
{
	Test	*t;
	va_list	ap;

	if (g_nt >= MAX_TESTS)
	{
		fprintf(stderr, "tester: too many tests\n");
		exit(2);
	}
	t = &g_tests[g_nt++];
	memset(t, 0, sizeof *t);
	t->cat = cat;
	snprintf(t->id, sizeof t->id, "%c%02d", CATLET[cat], ++g_catcnt[cat]);
	va_start(ap, fmt);
	vsnprintf(t->name, sizeof t->name, fmt, ap);
	va_end(ap);
	t->mand = mand;
	t->weight = weight;
	t->mode = mode;
	t->eof = 3;
	t->why = why;
	return (t);
}

static Test	*Tlit(int cat, const char *lit, const char *why, const char *name)
{
	Test	*t = T(cat, 1, 0, M_FILE, why, "%s", name);

	t->lit = lit;
	t->litn = strlen(lit);
	return (t);
}

static Test	*Tgen(int cat, int weight, Mode mode, const char *why, GenFn g,
				long a, long b, long c, const char *fmt, ...)
{
	Test	*t;
	va_list	ap;
	char	nm[150];

	va_start(ap, fmt);
	vsnprintf(nm, sizeof nm, fmt, ap);
	va_end(ap);
	t = T(cat, 1, weight, mode, why, "%s", nm);
	t->gen = g;
	t->a = a;
	t->b = b;
	t->c = c;
	return (t);
}

static void	build_content(const Test *t, Buf *b)
{
	if (t->gen)
		t->gen(b, t->a, t->b, t->c);
	else
		b_add(b, t->lit, t->litn);
}

/* ---- content generators ---------------------------------------------- */
static void	g_lines(Buf *b, long n, long len, long nl_end)
{
	long	i;

	for (i = 0; i < n; i++)
	{
		b_pat(b, (size_t)len, (unsigned)(i + 1));
		if (i + 1 < n || nl_end)
			b_rep(b, '\n', 1);
	}
}

static void	g_run(Buf *b, long ch, long n, long nl_end)
{
	b_rep(b, (char)ch, (size_t)n);
	if (nl_end)
		b_rep(b, '\n', 1);
}

static void	g_rand_lines(Buf *b, long n, long maxlen, long nl_end)
{
	long	i;

	g_rng = (unsigned)(777 + maxlen * 3 + n);
	for (i = 0; i < n; i++)
	{
		b_pat(b, rnd() % (unsigned)(maxlen + 1), (unsigned)(i + 5));
		if (i + 1 < n || nl_end)
			b_rep(b, '\n', 1);
	}
}

static void	g_alt(Buf *b, long pairs, long s, long l)
{
	long	i;

	for (i = 0; i < pairs; i++)
	{
		b_pat(b, (size_t)s, (unsigned)(2 * i + 1));
		b_rep(b, '\n', 1);
		b_pat(b, (size_t)l, (unsigned)(2 * i + 2));
		b_rep(b, '\n', 1);
	}
}

static void	g_mixlong(Buf *b, long l1, long l2, long unused)
{
	(void)unused;
	b_add(b, "short\n", 6);
	b_pat(b, (size_t)l1, 11);
	b_rep(b, '\n', 1);
	b_add(b, "x\n\n", 3);
	b_pat(b, (size_t)l2, 12);
	b_rep(b, '\n', 1);
	b_add(b, "end\n", 4);
}

static void	g_stair(Buf *b, long maxlen, long dir, long nl_end)
{
	long	i, len;

	for (i = 0; i <= maxlen; i++)
	{
		len = dir ? maxlen - i : i;
		b_pat(b, (size_t)len, (unsigned)(i + 3));
		if (i < maxlen || nl_end)
			b_rep(b, '\n', 1);
	}
}

static void	g_cycle(Buf *b, long n, long unused, long nl_end)
{
	long	cyc[8], i, len;

	(void)unused;
	cyc[0] = BSV - 1; cyc[1] = BSV; cyc[2] = BSV + 1; cyc[3] = 0;
	cyc[4] = 1; cyc[5] = 2 * BSV; cyc[6] = 2 * BSV + 1; cyc[7] = BSV - 2;
	for (i = 0; i < n; i++)
	{
		len = cyc[i % 8];
		if (len < 0)
			len = 0;
		b_pat(b, (size_t)len, (unsigned)(i + 9));
		if (i + 1 < n || nl_end)
			b_rep(b, '\n', 1);
	}
}

static void	g_fuzz(Buf *b, long seed, long n, long nl_end)
{
	long	i, len, cap;

	g_rng = (unsigned)(seed * 7919 + 13);
	cap = 2 * BSV + 2;
	if (cap > 300)
		cap = 300;
	for (i = 0; i < n; i++)
	{
		switch (rnd() % 4)
		{
			case 0: len = rnd() % 4; break ;
			case 1: len = BSV - 1 + (long)(rnd() % 3); break ;
			default: len = (long)(rnd() % (unsigned)(cap + 1)); break ;
		}
		if (len < 0)
			len = 0;
		if (len > cap)
			len = cap;
		b_pat(b, (size_t)len, (unsigned)(seed * 1000 + i));
		if (i + 1 < n || nl_end)
			b_rep(b, '\n', 1);
	}
}

/* ======================================================================== */
/*  5. Test bodies                                                          */
/* ======================================================================== */
static int	run_content(const Test *t, int short_reads)
{
	Buf		in = {NULL, 0, 0};
	char	path[600];
	int		fd, rc;

	build_content(t, &in);
	new_tmp_path(path, sizeof path);
	if (write_file(path, in.d ? in.d : "", in.n) < 0)
	{
		b_free(&in);
		return (fail("tester: cannot create temp file %s", path));
	}
	fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		b_free(&in);
		return (fail("tester: cannot open temp file"));
	}
	if (short_reads)
	{
		g_short_reads = 1;
		g_rng = 99991;
	}
	rc = check_stream(fd, in.d ? in.d : "", in.n, t->eof);
	close(fd);
	unlink(path);
	b_free(&in);
	return (rc);
}

static int	seq_read(const char *content, int eof_calls)
{
	Buf		b = {NULL, 0, 0};
	char	path[600];
	int		fd, rc;

	b_add(&b, content, strlen(content));
	new_tmp_path(path, sizeof path);
	if (write_file(path, b.d, b.n) < 0)
	{
		b_free(&b);
		return (fail("tester: cannot create temp file"));
	}
	fd = open(path, O_RDONLY);
	rc = check_stream(fd, b.d, b.n, eof_calls);
	close(fd);
	unlink(path);
	b_free(&b);
	return (rc);
}

static int	expect_null_calls(int fd, int n, const char *what)
{
	int		i, rc;
	char	*l;

	for (i = 1; i <= n; i++)
	{
		l = call_gnl(fd);
		if (l)
		{
			rc = fail("%s: call #%d returned \"%s\" instead of NULL", what, i,
					esc(l, safe_len(l)));
			gnlt_free(l);
			return (rc);
		}
		if (g_live_cnt != 0)
			return (fail("[MEM] %s: call #%d returned NULL but memory is still "
					"allocated: %s", what, i, leak_report()));
	}
	return (0);
}

/* ---- stdin / pipe ---------------------------------------------------- */
static int	run_pipe(const Test *t)
{
	Buf			in = {NULL, 0, 0};
	int			p[2], fd, rc;
	pid_t		w;
	size_t		off, ck, i;
	static const size_t	sizes[6] = {1, 2, 3, 5, 8, 13};

	build_content(t, &in);
	if (pipe(p) < 0)
	{
		b_free(&in);
		return (fail("tester: pipe() failed"));
	}
	fflush(NULL);
	w = fork();
	if (w == 0)
	{
		close(p[0]);
		off = 0;
		i = 0;
		while (off < in.n)
		{
			ck = sizes[i++ % 6];
			if (ck > in.n - off)
				ck = in.n - off;
			if (write(p[1], in.d + off, ck) < 0)
				_exit(1);
			off += ck;
			usleep(100);
		}
		close(p[1]);
		_exit(0);
	}
	close(p[1]);
	fd = p[0];
	if (t->mode == M_STDIN)
	{
		dup2(p[0], 0);
		close(p[0]);
		fd = 0;
	}
	rc = check_stream(fd, in.d ? in.d : "", in.n, t->eof);
	close(fd);
	waitpid(w, NULL, 0);
	b_free(&in);
	return (rc);
}

/* ---- fd / error custom tests ----------------------------------------- */
static int	t_fd_neg(void)
{
	return (expect_null_calls(-1, 3, "fd = -1"));
}

static int	t_fd_big(void)
{
	return (expect_null_calls(9999, 3, "fd = 9999 (never opened)"));
}

static int	t_fd_closed(void)
{
	char	path[600];
	int		fd;

	new_tmp_path(path, sizeof path);
	if (write_file(path, "abc\ndef\n", 8) < 0)
		return (fail("tester: cannot create temp file"));
	fd = open(path, O_RDONLY);
	close(fd);
	return (expect_null_calls(fd, 3, "closed fd"));
}

static int	t_fd_dir(void)
{
	int	fd, rc;

	fd = open(".", O_RDONLY);
	if (fd < 0)
		return (fail("tester: cannot open('.')"));
	rc = expect_null_calls(fd, 3, "fd of a directory (read() fails with EISDIR)");
	close(fd);
	return (rc);
}

static int	t_fd_wronly(void)
{
	char	path[600];
	int		fd, rc;

	new_tmp_path(path, sizeof path);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return (fail("tester: cannot create temp file"));
	rc = expect_null_calls(fd, 3, "write-only fd (read() fails with EBADF)");
	close(fd);
	return (rc);
}

static int	t_err_then_ok(void)
{
	int	rc;

	rc = expect_null_calls(-1, 2, "fd = -1");
	if (!rc)
		rc = expect_null_calls(9999, 1, "fd = 9999");
	if (!rc)
		rc = seq_read("ok line 1\nok line 2\nlast", 3);
	return (rc);
}

static int	t_dir_then_ok(void)
{
	int	fd, rc;

	fd = open(".", O_RDONLY);
	rc = expect_null_calls(fd, 2, "directory fd");
	close(fd);
	if (!rc)
		rc = seq_read("after the error\nstill fine\n", 3);
	return (rc);
}

static int	t_reopen_same(void)
{
	int	rc;

	rc = seq_read("alpha\nbeta\ngamma\n", 3);
	if (!rc)
		rc = seq_read("alpha\nbeta\ngamma\n", 3);
	return (rc);
}

static int	t_reopen_nonl(void)
{
	int	rc;

	rc = seq_read("alpha\nbeta\ngamma", 3);
	if (!rc)
		rc = seq_read("alpha\nbeta\ngamma", 3);
	return (rc);
}

static int	t_two_files_seq(void)
{
	int	rc;

	rc = seq_read("first file line 1\nfirst file line 2\n", 2);
	if (!rc)
		rc = seq_read("second\nfile\nis different\nand longer than the first one\n", 2);
	if (!rc)
		rc = seq_read("", 2);
	if (!rc)
		rc = seq_read("third without newline", 2);
	return (rc);
}

static int	t_mem_seq_growth(void)
{
	int	i, rc;

	rc = 0;
	for (i = 0; i < 20 && !rc; i++)
		rc = seq_read("a line\nanother line\nlast line without nl", 1);
	return (rc);
}

#ifdef GNL_BADBS
static int	t_bad_file(void)
{
	char	path[600];
	int		fd, rc;

	new_tmp_path(path, sizeof path);
	if (write_file(path, "abc\ndef\n", 8) < 0)
		return (fail("tester: cannot create temp file"));
	fd = open(path, O_RDONLY);
	rc = expect_null_calls(fd, 3, "BUFFER_SIZE <= 0 on a valid file");
	close(fd);
	return (rc);
}

static int	t_bad_empty(void)
{
	char	path[600];
	int		fd, rc;

	new_tmp_path(path, sizeof path);
	if (write_file(path, "", 0) < 0)
		return (fail("tester: cannot create temp file"));
	fd = open(path, O_RDONLY);
	rc = expect_null_calls(fd, 3, "BUFFER_SIZE <= 0 on an empty file");
	close(fd);
	return (rc);
}

static int	t_bad_fd(void)
{
	return (expect_null_calls(-1, 3, "BUFFER_SIZE <= 0 with fd = -1"));
}
#endif

/* ---- fault injection ------------------------------------------------- */
typedef enum { R_PASS, R_FAIL, R_CRASH, R_TIMEOUT, R_SAN } RStatus;
typedef struct {
	RStatus	st;
	char	reason[1024];
	char	detail[1500];
	double	secs;
}	Res;
typedef struct { const Test *t; unsigned long k; }	Att;

static unsigned	g_tmo_mult = 1;
static int		g_iso_counter;

static void	run_isolated(int (*fn)(const void *), const void *arg,
				unsigned tmo, int idx, Res *r)
{
	int		rp[2], efd, status;
	char	errpath[700];
	pid_t	pid;
	double	t0;
	ssize_t	n;
	int		ef;

	memset(r, 0, sizeof *r);
	if (pipe(rp) < 0)
	{
		r->st = R_FAIL;
		snprintf(r->reason, sizeof r->reason, "tester: pipe() failed");
		return ;
	}
	snprintf(errpath, sizeof errpath, "%s/err.%d.%d", g_tmpdir, idx,
		++g_iso_counter);
	efd = open(errpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	fflush(NULL);
	t0 = now();
	pid = fork();
	if (pid == 0)
	{
		int rc;

		close(rp[0]);
		g_reason_fd = rp[1];
		g_reason[0] = 0;
		if (efd >= 0)
		{
			dup2(efd, 2);
			close(efd);
		}
		alarm(tmo * g_tmo_mult);
		rc = fn(arg);
		if (g_reason[0])
		{
			n = write(rp[1], g_reason, strlen(g_reason));
			(void)n;
		}
		if (rc != 0)
			_exit(10);
		close(rp[1]);
		exit(0);
	}
	close(rp[1]);
	if (efd >= 0)
		close(efd);
	waitpid(pid, &status, 0);
	r->secs = now() - t0;
	n = read(rp[0], r->reason, sizeof r->reason - 1);
	if (n < 0)
		n = 0;
	r->reason[n] = 0;
	close(rp[0]);
	ef = open(errpath, O_RDONLY);
	if (ef >= 0)
	{
		n = read(ef, r->detail, sizeof r->detail - 1);
		if (n < 0)
			n = 0;
		r->detail[n] = 0;
		close(ef);
	}
	unlink(errpath);
	if (WIFSIGNALED(status))
	{
		if (WTERMSIG(status) == SIGALRM)
		{
			r->st = R_TIMEOUT;
			snprintf(r->reason, sizeof r->reason, "timed out after %us "
				"(infinite loop, or extremely slow)", tmo * g_tmo_mult);
		}
		else
		{
			r->st = R_CRASH;
			snprintf(r->reason, sizeof r->reason, "killed by signal %d (%s)",
				WTERMSIG(status), strsignal(WTERMSIG(status)));
		}
	}
	else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		r->st = R_PASS;
	else if (WIFEXITED(status) && WEXITSTATUS(status) == 10)
		r->st = R_FAIL;
	else
	{
		static const char	*keys[] = {"ERROR: ", "runtime error:", "Invalid ",
			"definitely lost", "indirectly lost", "Mismatched free",
			"Conditional jump", "Use of uninit", NULL};
		const char			*hit;
		char				line1[200];
		size_t				k, len;

		r->st = R_SAN;
		hit = NULL;
		for (k = 0; keys[k] && !hit; k++)
			hit = strstr(r->detail, keys[k]);
		line1[0] = 0;
		if (hit)
		{
			len = strcspn(hit, "\n");
			if (len > sizeof line1 - 1)
				len = sizeof line1 - 1;
			memcpy(line1, hit, len);
			line1[len] = 0;
		}
		snprintf(r->reason, sizeof r->reason, "sanitizer/valgrind report "
			"(exit status %d): %s", WIFEXITED(status) ? WEXITSTATUS(status) : -1,
			line1[0] ? line1 : "see the log for details");
	}
}

static int	open_content(const Test *t, Buf *in, int *fd)
{
	char	path[600];

	build_content(t, in);
	new_tmp_path(path, sizeof path);
	if (write_file(path, in->d ? in->d : "", in->n) < 0)
		return (fail("tester: cannot create temp file"));
	*fd = open(path, O_RDONLY);
	unlink(path);
	return (*fd < 0 ? fail("tester: cannot open temp file") : 0);
}

static long	count_lines(const Buf *b)
{
	long	n;
	size_t	i;

	n = 0;
	for (i = 0; i < b->n; i++)
		if (b->d[i] == '\n')
			n++;
	if (b->n && b->d[b->n - 1] != '\n')
		n++;
	return (n);
}

static int	measure_clean(const void *arg)
{
	const Test	*t = arg;
	Buf			in = {NULL, 0, 0};
	int			fd, rc;

	rc = open_content(t, &in, &fd);
	if (rc)
		return (rc);
	rc = check_stream(fd, in.d ? in.d : "", in.n, 2);
	close(fd);
	b_free(&in);
	if (rc)
		return (rc);
	snprintf(g_reason, sizeof g_reason, "%lu %lu", g_malloc_calls, g_read_calls);
	return (0);
}

static int	drain(int fd, long maxcalls, int is_read_inj)
{
	char	*line;
	int		before, now_trig, rc;
	long	calls;

	for (calls = 0;; calls++)
	{
		if (calls >= maxcalls)
			return (fail("never reached EOF: still returning lines after %ld "
					"calls (state corrupted after the injected failure?)",
					calls));
		before = is_read_inj ? g_read_fail_triggered : g_fail_triggered;
		line = call_gnl(fd);
		now_trig = !before && (is_read_inj ? g_read_fail_triggered
				: g_fail_triggered);
		if (now_trig)
		{
			g_fail_malloc_at = 0;
			g_fail_read_at = 0;
			if (is_read_inj && line)
			{
				rc = fail("read() returned -1 but get_next_line returned a line "
						"(\"%s\") instead of NULL", esc(line, safe_len(line)));
				gnlt_free(line);
				return (rc);
			}
		}
		if (line)
		{
			rc = 0;
			if (((const Hdr *)line - 1)->magic != HMAGIC)
				rc = fail("returned pointer not obtained from malloc");
			else if (!memchr(line, 0, ((const Hdr *)line - 1)->size))
				rc = fail("returned line is not '\\0'-terminated");
			gnlt_free(line);
			if (rc)
				return (rc);
			continue ;
		}
		if (!now_trig)
			break ;
	}
	return (0);
}

static int	attempt_inject(const void *arg)
{
	const Att	*a = arg;
	Buf			in = {NULL, 0, 0};
	int			fd, rc, is_read;

	is_read = (a->t->mode == M_READ_FAIL);
	rc = open_content(a->t, &in, &fd);
	if (rc)
		return (rc);
	if (is_read)
		g_fail_read_at = a->k;
	else
		g_fail_malloc_at = a->k;
	rc = drain(fd, count_lines(&in) + 8, is_read);
	close(fd);
	b_free(&in);
	if (!rc && g_live_cnt != 0)
		return (fail("[MEM] after the injected failure and reading the rest of "
				"the file to EOF: %s", leak_report()));
	return (rc);
}

static int	run_inject(const Test *t)
{
	Res				r;
	Att				a;
	unsigned long	total, mc, rc_, k, step, bad, shown;
	char			first[1200];
	int				is_read;

	is_read = (t->mode == M_READ_FAIL);
	run_isolated(measure_clean, t, 30, 900, &r);
	if (r.st != R_PASS)
		return (fail("tester: the clean (no fault) run already fails - fix the "
				"normal-reading bugs first: %s", r.reason));
	mc = 0;
	rc_ = 0;
	sscanf(r.reason, "%lu %lu", &mc, &rc_);
	total = is_read ? rc_ : mc;
	if (total == 0)
		return (0);
	step = total > 120 ? total / 120 + 1 : 1;
	bad = 0;
	shown = 0;
	first[0] = 0;
	a.t = t;
	for (k = 1; k <= total; k += step)
	{
		a.k = k;
		run_isolated(attempt_inject, &a, 15, 901, &r);
		if (r.st != R_PASS)
		{
			bad++;
			if (!shown)
			{
				shown = k;
				snprintf(first, sizeof first, "%s #%lu failed -> %.700s%s%s",
					is_read ? "read() call" : "malloc call", k, r.reason,
					r.detail[0] ? " | " : "", r.detail[0] ? "(sanitizer output)" : "");
			}
		}
	}
	if (bad)
		return (fail("%lu of the %lu tested injection points break your code; "
				"first: %.800s", bad, (total + step - 1) / step, first));
	return (0);
}

static int	run_test(const void *arg)
{
	const Test	*t = arg;

	switch (t->mode)
	{
		case M_FILE:
			return (run_content(t, 0));
		case M_SHORT:
			return (run_content(t, 1));
		case M_PIPE:
		case M_STDIN:
			return (run_pipe(t));
		case M_MALLOC_FAIL:
		case M_READ_FAIL:
			return (run_inject(t));
		case M_CUSTOM:
			return (t->custom());
	}
	return (fail("tester: unknown mode"));
}

/* ======================================================================== */
/*  6. Test list                                                            */
/* ======================================================================== */
static Test	*Tcustom(int cat, int mand, const char *why, int (*fn)(void),
				const char *fmt, ...)
{
	Test	*t;
	va_list	ap;
	char	nm[150];

	va_start(ap, fmt);
	vsnprintf(nm, sizeof nm, fmt, ap);
	va_end(ap);
	t = T(cat, mand, 0, M_CUSTOM, why, "%s", nm);
	t->custom = fn;
	return (t);
}

static void	reg_basic(void)
{
	const int	C = C_BASIC;
	const char	*w = "Plain behaviour required by the subject: one call = one "
		"line, '\\n' included, NULL when nothing is left.";

	Tlit(C, "", w, "empty file");
	Tlit(C, "a", w, "one character, no newline");
	Tlit(C, "a\n", w, "one character + newline");
	Tlit(C, "hello", w, "one word, no newline");
	Tlit(C, "hello\n", w, "one word + newline");
	Tlit(C, "hello world 42", w, "one line with spaces, no newline");
	Tlit(C, "hello world 42\n", w, "one line with spaces + newline");
	Tlit(C, "\n", w, "only a newline");
	Tlit(C, "\n\n\n", w, "three newlines (3 empty lines)");
	Tlit(C, "line one\nline two\nline three\n", w, "3 lines, all end with \\n");
	Tlit(C, "line one\nline two\nline three", w,
		"3 lines, the last one has no \\n");
	Tlit(C, "a\nb", w, "two lines 'a' / 'b' (no final newline)");
	Tlit(C, "a\n\nb\n\n\nc\n", w, "empty and non-empty lines mixed");
	Tlit(C, "\n\nabc\n", w, "leading empty lines");
	Tlit(C, "abc\n\n\n", w, "trailing empty lines");
	Tlit(C, "\nabc", w, "empty first line, last line without \\n");
	Tlit(C, "x\n\n\n\n\n\n\n\n\n\ny\n", w,
		"10 consecutive newlines between two lines");
	Tgen(C, 0, M_FILE, w, g_lines, 1000, 2, 1, "1000 short lines (2 chars each)");
}

static void	reg_boundaries(void)
{
	const int	C = C_BOUND;
	struct { const char *lab; long len; }	c[8];
	long		seen[8], v;
	int			ns, i, j, dup;
	const char	*w1 = "Line length vs BUFFER_SIZE: the '\\n' lands on the last "
		"byte of a read, or on the first byte of the next one. The leftover "
		"after the line must be exact.";
	const char	*w2 = "Several lines inside one read(): each call must return "
		"exactly one line and keep the rest, without losing or repeating bytes.";
	const char	*w3 = "read() may return fewer bytes than asked (stdin, pipes, "
		"terminals). Only the bytes actually read may be used.";

	if (BSV > 1000000)
	{
		printf("NOTE  boundary-size tests skipped: BUFFER_SIZE=%ld is too large "
			"to build files around it\n", BSV);
		g_skipped_notes++;
	}
	else
	{
		c[0].lab = "BS-2"; c[0].len = BSV - 2;
		c[1].lab = "BS-1"; c[1].len = BSV - 1;
		c[2].lab = "BS"; c[2].len = BSV;
		c[3].lab = "BS+1"; c[3].len = BSV + 1;
		c[4].lab = "2BS-1"; c[4].len = 2 * BSV - 1;
		c[5].lab = "2BS"; c[5].len = 2 * BSV;
		c[6].lab = "2BS+1"; c[6].len = 2 * BSV + 1;
		c[7].lab = "3BS"; c[7].len = 3 * BSV;
		ns = 0;
		for (i = 0; i < 8; i++)
		{
			v = c[i].len;
			dup = 0;
			for (j = 0; j < ns; j++)
				if (seen[j] == v)
					dup = 1;
			if (v < 0 || dup)
				continue ;
			seen[ns++] = v;
			Tgen(C, v > 100000 ? 1 : 0, M_FILE, w1, g_lines, 1, v, 1,
				"1 line of %s=%ld chars + \\n", c[i].lab, v);
			Tgen(C, v > 100000 ? 1 : 0, M_FILE, w1, g_lines, 1, v, 0,
				"1 line of %s=%ld chars, no \\n", c[i].lab, v);
			Tgen(C, v > 100000 ? 1 : 0, M_FILE, w1, g_lines, 3, v, 1,
				"3 lines of %s=%ld chars, all with \\n", c[i].lab, v);
			Tgen(C, v > 100000 ? 1 : 0, M_FILE, w1, g_lines, 3, v, 0,
				"3 lines of %s=%ld chars, last without \\n", c[i].lab, v);
		}
		Tgen(C, 0, M_FILE, w1, g_run, '\n', BSV - 1 < 0 ? 0 : BSV - 1, 0,
			"%ld newlines in a row (BS-1)", BSV - 1 < 0 ? 0 : BSV - 1);
		Tgen(C, 0, M_FILE, w1, g_run, '\n', BSV, 0,
			"%ld newlines in a row (BS)", BSV);
		Tgen(C, 0, M_FILE, w1, g_run, '\n', BSV + 1, 0,
			"%ld newlines in a row (BS+1)", BSV + 1);
		Tgen(C, 0, M_FILE, w1, g_run, '\n', 3 * BSV + 1, 0,
			"%ld newlines in a row (3BS+1)", 3 * BSV + 1);
		Tgen(C, 0, M_FILE, w1, g_cycle, 40, 0, 1,
			"40 lines cycling BS-1, BS, BS+1, 0, 1, 2BS, 2BS+1, BS-2");
		Tgen(C, 0, M_FILE, w1, g_cycle, 40, 0, 0,
			"same cycling lines, last one without \\n");
	}
	Tgen(C, 0, M_FILE, w1, g_stair, BSV * 3 + 3 > 120 ? 120 : BSV * 3 + 3, 0, 1,
		"line lengths 0,1,2,... up to %ld (every offset vs a read)",
		BSV * 3 + 3 > 120 ? 120L : BSV * 3 + 3);
	Tgen(C, 0, M_FILE, w1, g_stair, BSV * 3 + 3 > 120 ? 120 : BSV * 3 + 3, 1, 0,
		"line lengths descending to 0, last without \\n");
	Tgen(C, 0, M_FILE, w2, g_lines, 200, 1, 1, "200 one-char lines (many \\n per read)");
	Tgen(C, 0, M_FILE, w2, g_lines, 60, 7, 1, "60 lines of 7 chars (several per read)");
	Tgen(C, 0, M_FILE, w2, g_lines, 60, 7, 0, "60 lines of 7 chars, last without \\n");
	for (i = 1; i <= 8; i++)
		Tgen(C, 0, M_FILE, w2, g_fuzz, i, 200, i % 2,
			"fuzz #%d: 200 random-length lines%s", i,
			i % 2 ? "" : ", last without \\n");
	Tgen(C, 0, M_SHORT, w3, g_lines, 12, 9, 1, "partial reads: 12 lines of 9 chars");
	Tgen(C, 0, M_SHORT, w3, g_lines, 12, 9, 0, "partial reads: last line without \\n");
	Tgen(C, 0, M_SHORT, w3, g_lines, 1, 3000, 1, "partial reads: one 3000-char line");
	Tgen(C, 0, M_SHORT, w3, g_rand_lines, 150, 60, 1, "partial reads: 150 random lines");
	Tgen(C, 0, M_SHORT, w3, g_fuzz, 21, 150, 0, "partial reads: fuzz, last without \\n");
	Tgen(C, 0, M_SHORT, w3, g_run, '\n', 300, 0, "partial reads: 300 newlines");
}

static long	big100k(void)
{
	return (BSV >= 16 ? 100000 : (BSV >= 4 ? 50000 : 25000));
}

static void	reg_advanced(void)
{
	const int	C = C_ADV;
	const char	*w = "Larger / stranger inputs than the basic cases.";
	const char	*wl = "Very long lines: reading must stay correct (and not "
		"crazy slow) when the line is many times BUFFER_SIZE.";

	Tgen(C, 0, M_FILE, wl, g_lines, 1, 10000, 1, "one 10 KB line + \\n");
	Tgen(C, 0, M_FILE, wl, g_lines, 1, 10000, 0, "one 10 KB line, no \\n");
	Tgen(C, 1, M_FILE, wl, g_lines, 1, big100k(), 1, "one %ld-char line + \\n",
		big100k());
	Tgen(C, 1, M_FILE, wl, g_lines, 1, big100k(), 0, "one %ld-char line, no \\n",
		big100k());
	if (BSV >= 1024)
	{
		Tgen(C, 2, M_FILE, wl, g_lines, 1, 1000000, 1, "one 1 MB line + \\n");
		Tgen(C, 2, M_FILE, wl, g_lines, 1, 1000000, 0, "one 1 MB line, no \\n");
	}
	Tgen(C, 0, M_FILE, w, g_rand_lines, 5000, 40, 1,
		"5000 lines of random length 0..40");
	Tgen(C, 0, M_FILE, w, g_rand_lines, 5000, 40, 0,
		"5000 random lines, last without \\n");
	Tgen(C, 0, M_FILE, w, g_run, '\n', 10000, 0, "10000 empty lines");
	Tgen(C, 0, M_FILE, w, g_lines, 10000, 1, 1, "10000 one-char lines");
	Tlit(C, " \t \n\t\t\n\t \t\n", w, "spaces and tabs");
	Tlit(C, "0123456789\n!@#$%^&*()_+-=[]{}|;':\",./<>?\n`~\\\n", w,
		"numbers and special characters");
	Tlit(C, "     \n  \n \n          \n", w, "lines containing only spaces");
	Tlit(C, "   ", w, "only spaces, no newline");
	Tgen(C, 0, M_FILE, wl, g_mixlong, 15000, 30000, 0,
		"short lines mixed with 15000 and 30000-char lines");
	Tgen(C, 0, M_FILE, wl, g_lines, 1, 32000, 0, "32 KB file without any newline");
	Tlit(C, "abc\n\n\n\n", w, "file ending with several newlines");
	Tgen(C, 0, M_FILE, w, g_run, 'a', 3000, 1, "3000 times 'a' + \\n");
	Tgen(C, 0, M_FILE, w, g_run, 'a', 3000, 0, "3000 times 'a', no \\n");
	Tgen(C, 0, M_FILE, w, g_alt, 100, 3, 300, "100 x (3-char line, 300-char line)");
	Tlit(C, "h\xc3\xa9llo w\xc3\xb6rld\n\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\n"
		"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n\xff\xfe end\n", w,
		"UTF-8 / high bytes (bytes > 127, no NUL)");
	Tlit(C, "a\r\nb\r\n\r\nc\rd\n", w,
		"CRLF: only '\\n' ends a line, '\\r' stays in the line");
	Tlit(C, "same\nsame\nsame\n", w, "three identical lines");
}

static void	reg_eof(void)
{
	const int	C = C_EOF;
	const char	*w = "After the last line the function must return NULL, "
		"every time, without hanging, without leaks and without re-returning data.";
	Test		*t;

	t = Tlit(C, "", w, "empty file: 10 calls in a row -> NULL");
	t->eof = 10;
	t = Tlit(C, "a\nb\nc\n", w, "3 lines + \\n: then 100 calls -> NULL");
	t->eof = 100;
	t = Tlit(C, "a\nb\nc", w, "last line without \\n returned once, then 100 x NULL");
	t->eof = 100;
	Tlit(C, "dup\ndup", w, "last line without \\n is not returned twice");
	Tlit(C, "x\n", w, "EOF right after a single line + \\n");
	Tgen(C, 0, M_FILE, w, g_run, 'z', BSV > 100000 ? 5 : 2 * BSV, 0,
		"file size = 2*BS (read returns exactly full buffers, then 0), no \\n");
	Tgen(C, 0, M_FILE, w, g_run, 'z', BSV > 100000 ? 5 : 2 * BSV - 1, 1,
		"file size = 2*BS ending with \\n on the very last byte");
	Tlit(C, "a\nb\nc\nd\ne\nf\n", w, "all lines arrive in one buffer: leftovers in save");
	Tcustom(C, 1, "A fresh open after a complete read must not see leftovers "
		"from the previous file (static `save` must be empty after EOF).",
		t_reopen_same, "re-open the same file after EOF and read it again");
	Tcustom(C, 1, "Same, when the previous file ended without \\n.",
		t_reopen_nonl, "re-open (file without final \\n) and read it again");
	Tcustom(C, 1, "Different files read one after the other, each to EOF.",
		t_two_files_seq, "4 different files read one after another (to EOF each)");
	Tlit(C, "   ", w, "only spaces, no newline, then EOF");
}

static void	reg_fd(void)
{
	const int	C = C_FD;
	const char	*we = "Subject: 'if an error occurred, it should return NULL'. "
		"read() fails -> NULL, no crash, no leak.";
	const char	*wp = "Subject: must work on a file AND on the standard input.";
#ifdef GNL_BADBS
	const char	*wb = "NOT required by the subject (behaviour with BUFFER_SIZE <= 0 "
		"is undefined there); evaluators like NULL / no crash / no hang.";

	Tcustom(C, 0, wb, t_bad_file, "BUFFER_SIZE=%ld: valid file -> NULL, no crash/hang", BSV);
	Tcustom(C, 0, wb, t_bad_empty, "BUFFER_SIZE=%ld: empty file -> NULL", BSV);
	Tcustom(C, 0, wb, t_bad_fd, "BUFFER_SIZE=%ld: fd=-1 -> NULL", BSV);
	(void)we;
	(void)wp;
#else
	Tcustom(C, 1, we, t_fd_neg, "fd = -1");
	Tcustom(C, 1, we, t_fd_big, "fd = 9999 (never opened)");
	Tcustom(C, 1, we, t_fd_closed, "closed fd");
	Tcustom(C, 1, we, t_fd_dir, "fd of a directory (read fails: EISDIR)");
	Tcustom(C, 1, we, t_fd_wronly, "fd opened write-only (read fails: EBADF)");
	Tcustom(C, 1, we, t_err_then_ok, "errors (fd -1, 9999) then a valid file works");
	Tcustom(C, 1, we, t_dir_then_ok, "read error on a directory, then a valid file works");
	Tgen(C, 0, M_STDIN, wp, g_lines, 5, 8, 1, "stdin (fd 0) through a pipe: 5 lines");
	Tgen(C, 0, M_STDIN, wp, g_lines, 5, 8, 0,
		"stdin: tiny writes, last line without \\n");
	Tgen(C, 0, M_STDIN, wp, g_lines, 1, 3000, 1, "stdin: one 3000-char line, tiny writes");
	Tlit(C, "", wp, "stdin: closed immediately (empty input)")->mode = M_STDIN;
	Tgen(C, 0, M_PIPE, wp, g_rand_lines, 40, 30, 0,
		"pipe fd with partial reads: 40 random lines");
#endif
}

static void	reg_memory(void)
{
	const int	C = C_MEM;
	const char	*w = "Subject: no memory leaks tolerated. After NULL at EOF nothing "
		"allocated by get_next_line may remain.";
	const char	*wi = "NOT required by the subject (extra robustness): inject a "
		"failure and make sure there is no crash and no leak.";
	Test		*t;

	Tlit(C, "one line\n", w, "leak check: one line + \\n");
	Tlit(C, "one line", w, "leak check: one line, no \\n");
	Tlit(C, "", w, "leak check: empty file");
	Tgen(C, 0, M_FILE, w, g_lines, 2000, 5, 1,
		"leak check: 2000 lines (a leak per call shows up here)");
	Tgen(C, 0, M_FILE, w, g_lines, 1, 2500, 0,
		"leak check: one 2500-char line built from many reads");
	Tcustom(C, 1, w, t_mem_seq_growth,
		"leak check: the same small file read 20 times in a row");
	Tgen(C, 0, M_FILE, w, g_run, '\n', 500, 0, "leak check: 500 empty lines");
	t = Tlit(C, "abc\ndef\nghi\n", wi, "malloc() fails: 3 short lines");
	t->mode = M_MALLOC_FAIL; t->mand = 0;
	t = Tlit(C, "abc\ndef", wi, "malloc() fails: last line without \\n");
	t->mode = M_MALLOC_FAIL; t->mand = 0;
	t = Tgen(C, 0, M_MALLOC_FAIL, wi, g_lines, 1, 3 * (BSV > 200 ? 200 : BSV) + 1, 1,
		"malloc() fails: one line spanning several reads");
	t->mand = 0;
	t = Tgen(C, 0, M_MALLOC_FAIL, wi, g_fuzz, 31, 12, 1,
		"malloc() fails: 12 random lines");
	t->mand = 0;
	t = Tlit(C, "abc\ndef\nghi\n", wi, "read() fails (-1): 3 short lines");
	t->mode = M_READ_FAIL; t->mand = 0;
	t = Tgen(C, 0, M_READ_FAIL, wi, g_lines, 1, 3 * (BSV > 200 ? 200 : BSV) + 1, 1,
		"read() fails (-1): one line spanning several reads");
	t->mand = 0;
	t = Tlit(C, "abc\ndef", wi, "read() fails (-1): last line without \\n");
	t->mode = M_READ_FAIL; t->mand = 0;
}

static void	register_tests(void)
{
#ifdef GNL_BADBS
	reg_fd();
#else
	reg_basic();
	reg_boundaries();
	reg_advanced();
	reg_eof();
	reg_fd();
	reg_memory();
#endif
}

/* ======================================================================== */
/*  7. Runner                                                               */
/* ======================================================================== */
static const char	*st_name(RStatus s)
{
	switch (s)
	{
		case R_PASS: return ("PASS");
		case R_FAIL: return ("FAIL");
		case R_CRASH: return ("CRASH");
		case R_TIMEOUT: return ("TIMEOUT");
		default: return ("SAN");
	}
}

static void	print_detail(const char *s)
{
	int	lines;

	lines = 0;
	while (*s && lines < 14)
	{
		const char *e = strchr(s, '\n');
		size_t len = e ? (size_t)(e - s) : strlen(s);
		if (len)
			printf("          | %.*s\n", (int)(len > 160 ? 160 : len), s);
		lines++;
		if (!e)
			break ;
		s = e + 1;
	}
}

int	main(int argc, char **argv)
{
	int			level = 1, i, total = 0, passed = 0, skipped = 0, nfail_m = 0;
	int			cat_t[C_N] = {0}, cat_p[C_N] = {0};
	const char	*only = NULL, *tsv_path = NULL, *env;
	FILE		*tsv = NULL;
	char		tmpl[256];
	const char	*tmpbase;

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--level") && i + 1 < argc)
			level = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--only") && i + 1 < argc)
			only = argv[++i];
		else if (!strcmp(argv[i], "--tsv") && i + 1 < argc)
			tsv_path = argv[++i];
		else if (!strcmp(argv[i], "--list"))
			level = 99;
		else
		{
			fprintf(stderr, "usage: %s [--level 0|1|2] [--only ID] [--tsv FILE]\n",
				argv[0]);
			return (2);
		}
	}
	env = getenv("GNL_TIMEOUT_MULT");
	if (env && atoi(env) > 0)
		g_tmo_mult = (unsigned)atoi(env);
	tmpbase = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
	snprintf(tmpl, sizeof tmpl, "%s/gnl_tester.XXXXXX", tmpbase);
	if (!mkdtemp(tmpl))
	{
		perror("mkdtemp");
		return (2);
	}
	snprintf(g_tmpdir, sizeof g_tmpdir, "%s", tmpl);
	if (tsv_path)
		tsv = fopen(tsv_path, "w");
	signal(SIGPIPE, SIG_IGN);
	register_tests();
	printf("==== get_next_line tester | BUFFER_SIZE=%ld | level=%d ====\n",
		BSV, level);
	for (i = 0; i < g_nt; i++)
	{
		Test	*t = &g_tests[i];
		Res		r;
		unsigned tmo;

		if (only && strcmp(only, t->id))
			continue ;
		if (!only && t->weight > level)
		{
			skipped++;
			continue ;
		}
		tmo = 40 + 40 * (unsigned)t->weight;
		if (t->mode == M_MALLOC_FAIL || t->mode == M_READ_FAIL)
			tmo = 600;
		run_isolated(run_test, t, tmo, i, &r);
		total++;
		cat_t[t->cat]++;
		if (r.st == R_PASS)
		{
			passed++;
			cat_p[t->cat]++;
		}
		else if (t->mand)
			nfail_m++;
		printf("%-7s [%c] %s %s", st_name(r.st), t->mand ? 'M' : 'R', t->id, t->name);
		if (r.secs > 2.0)
			printf("  (%.1fs)", r.secs);
		printf("\n");
		if (r.st != R_PASS)
		{
			printf("          reason: %s\n", r.reason);
			if (t->why)
				printf("          why this test exists: %s\n", t->why);
			if (r.detail[0])
				print_detail(r.detail);
		}
		if (tsv)
		{
			char	rs[1030];
			size_t	j;

			snprintf(rs, sizeof rs, "%s", r.reason);
			for (j = 0; rs[j]; j++)
				if (rs[j] == '\t' || rs[j] == '\n')
					rs[j] = ' ';
			fprintf(tsv, "%s\t%c\t%d\t%s\t%s\t%.2f\t%s\n", st_name(r.st),
				t->mand ? 'M' : 'R', t->cat, t->id, t->name, r.secs,
				r.st == R_PASS ? "" : rs);
		}
		fflush(NULL);
	}
	printf("---- summary (BUFFER_SIZE=%ld): %d run, %d passed, %d failed "
		"(%d mandatory), %d skipped by level ----\n", BSV, total, passed,
		total - passed, nfail_m, skipped);
	for (i = 0; i < C_N; i++)
		if (cat_t[i])
			printf("     %-36s %3d tests  %3d passed  %3d failed\n", CATNAME[i],
				cat_t[i], cat_p[i], cat_t[i] - cat_p[i]);
	if (tsv)
		fclose(tsv);
	cleanup_tmpdir();
	return (total == passed ? 0 : 1);
}
