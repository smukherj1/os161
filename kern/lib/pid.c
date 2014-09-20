/*
 * pid.c
 *
 *  Created on: 2013-02-26
 *      Author: suvanjan
 */

#include <types.h>
#include <lib.h>
#include <machine/spl.h>
#include <pid.h>

int *pid_array = NULL;
int last_pid_index = 0;

void pid_bootstrap()
{
	pid_array = kmalloc(sizeof(int) * MAX_USER_PROCESSES);

	if(pid_array == NULL)
	{
		panic("Could not allocate memory for pid management!\n");
	}

	// Make each pid as zero (not allocated)
	bzero(pid_array, sizeof(int) * MAX_USER_PROCESSES);
}

int get_new_pid()
{
	int spl = splhigh();
	int i_try_pid = last_pid_index;
	do
	{
		if(pid_array[i_try_pid] == 0)
		{
			pid_array[i_try_pid] = 1;
			last_pid_index = i_try_pid;

			// We return +1 because 0 is not an accepted pid
			// and we want to use all our pid slots
			splx(spl);
			return i_try_pid + 1;
		}

		// The % MAX_USER_PROCESSES will wrap it around
		i_try_pid = (i_try_pid + 1) % MAX_USER_PROCESSES;
	}
	// if we reached the last_pid_index again it means we have completely wrapped
	// around and have no more pid left!
	while(i_try_pid != last_pid_index);

	splx(spl);
	return -1;
}

void release_pid(int pid)
{
	int spl = splhigh();
	// Sanity checks. Making these hard asserts because if these assertions
	// go off it means there is some serious bug in some other part of the
	// kernel and we want to debug that
	assert(pid > 0 && pid <= MAX_USER_PROCESSES);
	assert(pid_array[pid - 1] == 1);

	// Release this pid by marking its spot as 0.
	// Note: Its pid - 1 because when we allocate we return the index + 1
	// as the pid.
	pid_array[pid - 1] = 0;
	splx(spl);
}

void pid_shutdown()
{
	kfree(pid_array);
}
