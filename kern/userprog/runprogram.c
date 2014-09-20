/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, char **args, int argc)
{
	if(progname == NULL || args == NULL)
	{
		return EFAULT;
	}

	// Old run program*/

	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return result;
	}

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return result;
	}

	// Modified to include arguments

	int kargc = argc;
	size_t bufLen = 0;
	int i;

	bufLen = (kargc + 1)*4;

	int *argvLen = (int *)kmalloc(kargc * sizeof(int));
	if(argvLen == NULL)
	{
		vfs_close(v);
		return ENOMEM;
	}

	// Calculating the kernel buffer length that is required
	for(i = 0; i < kargc; i++)
	{
		argvLen[i] = strlen(args[i]);
		bufLen += argvLen[i] + (4 - (argvLen[i]%4));
	}

	// Stack pointer address has to be 8-byte aligned
	if((bufLen%8) != 0)
		bufLen += (8 - (bufLen%8));

	char *kbuf = (char *)kmalloc(bufLen * sizeof(char));
	if(kbuf == NULL)
	{
		kfree(argvLen);
		vfs_close(v);
		return ENOMEM;
	}

	// Copying the arguments (argv) with padding in the kernel buffer

	char *argv;
	argv = kbuf + ((kargc + 1)*4);
	int error;

	for(i = 0; i < kargc; i++)
	{
		memcpy(argv,args[i],argvLen[i]);

		argv = argv + argvLen[i];

		int j;
		for(j = 0; j < (4 - (argvLen[i]%4)); j++, argv++)
		{
			*argv = 0;
		}
	}

	stackptr = stackptr - (vaddr_t)bufLen;
	argv = (char *)(stackptr + ((kargc + 1)*4));
	memcpy(kbuf,&argv,sizeof(argv));

	for(i = 1; i < kargc; i++)
	{
		argv = (char *)argv + argvLen[i-1] + (4 - (argvLen[i-1]%4));
		memcpy(kbuf+(i*4),&argv,sizeof(argv));
	}

	char *a;
	a = NULL;
	memcpy(kbuf+(kargc*4),&a,sizeof(a));

	error = copyout(kbuf,(userptr_t)stackptr,bufLen);
	kfree(argvLen);
	kfree(kbuf);
	if(error != 0)
	{
		vfs_close(v);
		return error;
	}

	vfs_close(v);
	assert(strlen(progname) < MAX_EXEC_PATH_SIZE);
	strcpy(curthread->t_vmspace->exec_path, progname);
	/* Warp to user mode. */
	md_usermode(kargc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

