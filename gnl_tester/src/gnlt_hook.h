/* ************************************************************************** */
/*  gnlt_hook.h  -  force-included (cc -include) ONLY when compiling YOUR     */
/*  get_next_line*.c.  Your source files are NOT modified: the preprocessor   */
/*  just renames malloc/free/read to the tester's instrumented versions.      */
/*  That gives the tester: leak accounting, allocation-size/overflow checks,  */
/*  malloc() / read() fault injection and short-read simulation.              */
/* ************************************************************************** */
#ifndef GNLT_HOOK_H
# define GNLT_HOOK_H

# include <stddef.h>
# include <stdlib.h>
# include <unistd.h>
# include <stdio.h>
# include <string.h>
# include <fcntl.h>
# include <sys/types.h>

void	*gnlt_malloc(size_t n);
void	gnlt_free(void *p);
ssize_t	gnlt_read(int fd, void *buf, size_t n);

# define malloc gnlt_malloc
# define free gnlt_free
# define read gnlt_read

#endif
