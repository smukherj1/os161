/*
 * sh - shell
 * Usage: up to you
 */

#ifdef HOST
#include "hostcompat.h"
#endif

#include <unistd.h>

int
main(int argc, char *argv[])
{
#ifdef HOST
	hostcompat_init(argc, argv);
#endif

	/* Write this */
	char  *filename = "/testbin/add";
	char  *args[4];
	pid_t  pid;

	args[0] = "add";
	args[1] = "5";
	args[2] = "12";
	args[3] = NULL;

	pid = fork();
	if (pid == 0) execv(filename, args);

	return 1;
}
