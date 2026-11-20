/* Compile-only check: the header must have include guards, must be usable on
** its own, and must declare get_next_line(). */
#include "get_next_line.h"
#include "get_next_line.h"

int	main(void)
{
	char	*(*f)(int);

	f = get_next_line;
	(void)f;
	return (0);
}
