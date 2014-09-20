/*
 * pid.h
 *
 *  Created on: 2013-02-26
 *      Author: suvanjan
 */

#ifndef PID_H_
#define PID_H_

#define MAX_USER_PROCESSES 20

/*
 * Allocates memory required for pid management. Panics on error.
 */
void pid_bootstrap();

/*
 * Return a new pid if possible. Returns -1 if all pid slots are full
 */
int get_new_pid();

/*
 * Release this pid. Panics if pid out of range or is currently not
 * allocated.
 */
void release_pid(int pid);

/*
 * Release pid management memory
 */
void pid_shutdown();

#endif /* PID_H_ */
