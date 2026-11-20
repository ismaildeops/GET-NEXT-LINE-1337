/* ************************************************************************** */
/*  helpers_test.c  -  TEMPLATE for testing YOUR helper functions one by one  */
/*                                                                            */
/*  The helpers (has_newline, ft_strlen, ft_strlcat, ft_strjoin, ft_free ...) */
/*  are different in every project (names, argument order, who frees what),   */
/*  so this part cannot be generic.  Edit the section marked  >>> ADAPT <<<   */
/*  to match YOUR prototypes, set ADAPTED to 1, then run:   make helpers      */
/*                                                                            */
/*  It is built with AddressSanitizer + UBSan, so overflows, leaks and double */
/*  frees inside the helpers are reported even when the output looks right.   */
/* ************************************************************************** */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "get_next_line.h"

#define ADAPTED 0		/* >>> ADAPT <<<  set to 1 once the tests below match your code */

static int	g_pass, g_fail;

#define CHECK(cond, msg) do { \
	if (cond) { g_pass++; printf("PASS  %s\n", msg); } \
	else { g_fail++; printf("FAIL  %s   (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

#if ADAPTED
/* >>> ADAPT <<<  Examples for typical 42-style helpers.  Rename / re-order the
** arguments to match YOUR prototypes, and respect YOUR ownership rules
** (does ft_strjoin free s1?  does it free s2?  what does it return on NULL?). */
static void	test_has_newline(void)
{
	/* CHECK(has_newline("") == 0, "has_newline(\"\") == 0"); */
	/* CHECK(has_newline("abc") == 0, "no newline"); */
	/* CHECK(has_newline("abc\n") != 0, "newline at the end"); */
	/* CHECK(has_newline("\nabc") != 0, "newline at the start"); */
	/* CHECK(has_newline(NULL) == 0, "NULL input does not crash"); */
}

static void	test_strlen(void)
{
	/* CHECK(ft_strlen("") == 0, "strlen empty"); */
	/* CHECK(ft_strlen("abc\n") == 4, "strlen counts the newline"); */
	/* CHECK(ft_strlen(NULL) == 0, "strlen(NULL) does not crash (if you allow it)"); */
}

static void	test_strjoin(void)
{
	/* char *a = strdup("foo");  -- strdup is fine HERE: this is the tester, not your code
	** char *r = ft_strjoin(a, "bar");     // does it free a? adapt the next lines
	** CHECK(r && !strcmp(r, "foobar"), "join two strings");
	** CHECK(r && r[6] == '\0', "result is NUL-terminated");
	** free(r);                                                    */
	/* join with NULL first argument (the very first call has save == NULL):
	** char *r2 = ft_strjoin(NULL, "bar"); CHECK(r2 && !strcmp(r2, "bar"), "NULL + str");  free(r2); */
	/* join with an empty second string / both empty / newline inside */
}

static void	test_strlcat_or_free(void)
{
	/* ft_strlcat: check return value (total length it tried to build), truncation, NUL termination;
	** ft_free: check it frees and returns NULL, and that ft_free(NULL) is safe. */
}
#endif

int	main(void)
{
#if !ADAPTED
	printf("helpers_test.c is a template and has not been adapted yet.\n"
		"Open src/helpers_test.c, fill in the tests for your helpers, set ADAPTED to 1.\n");
	return (2);
#else
	test_has_newline();
	test_strlen();
	test_strjoin();
	test_strlcat_or_free();
	printf("\nhelpers: %d passed, %d failed\n", g_pass, g_fail);
	return (g_fail != 0);
#endif
}
