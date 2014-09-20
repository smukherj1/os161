#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/*
 * reboot - shut down system and reboot it.
 * Usage: reboot
 *
 * Just calls reboot() with the RB_REBOOT flag.
 */

int
main()
{
	int a = 5, status = 0;
	pid_t pid = fork();

	if(pid == 0)
	{
		a = 6;
		return a;
	}
	waitpid(pid, &status, 0);
	if(a == 5 && status == 6)
	{
		printf("Success!\n");
	}
	else
	{
		printf("Error! a = %d, status = %d\n", a, status);
	}
	return 0;
}
