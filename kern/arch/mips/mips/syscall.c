#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <kern/unistd.h>
#include <addrspace.h>
#include <syscall.h>
#include <thread.h>
#include <curthread.h>
#include <pid.h>
#include <list.h>
#include <kern/limits.h>
#include <vfs.h>
#include <vm.h>
#include <machine/vm.h>

/*
 * Child Process Info. This structure contains the only fields a parent needs to
 * know about its child
 */
struct childprocinfo
{
	int has_exited;
	int exit_code;
	struct thread *child_process_ptr;
};


/*
 * System call handler.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. In addition, the system call number is
 * passed in the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, like an ordinary function call, and the a3 register is
 * also set to 0 to indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/lib/libc/syscalls.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * Since none of the OS/161 system calls have more than 4 arguments,
 * there should be no need to fetch additional arguments from the
 * user-level stack.
 *
 * Watch out: if you make system calls that have 64-bit quantities as
 * arguments, they will get passed in pairs of registers, and not
 * necessarily in the way you expect. We recommend you don't do it.
 * (In fact, we recommend you don't use 64-bit quantities at all. See
 * arch/mips/include/types.h.)
 */

void
mips_syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err = 0;

	assert(curspl==0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	switch (callno) {

		case SYS__exit:
			sys_exit(tf);
			break;
		case SYS_getpid:
			err = 0;
			retval = curthread->pid;
			break;
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	    /* Add stuff here */
	    case SYS_fork:
	    	err = md_forkentry(tf, &retval);
	    	break;

	    case SYS_execv:
	    	err = sys_execv(tf);
	    	break;

	    // Minimalistic write. Only works for write to stdout
	    case SYS_write:
	    	err = sys_write(tf);
	    	break;
	    case SYS_read:
	    	err = sys_read(tf, &retval);
	    	break;
	    case SYS_waitpid:
	    	err = sys_waitpid((void*)curthread, tf->tf_a0, (int*)tf->tf_a1);
	    	break;

	    // System call to get heap space for malloc
	    case SYS_sbrk:
	    	err = sys_sbrk(tf->tf_a0, &retval);
	    	break;
 
	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	assert(curspl==0);
}

int sys_waitpid(void *ptr, int pid, int *status)
{
	struct thread *parent_proc = (struct thread*)ptr;
	if(status == NULL)
	{
		return EFAULT;
	}

	if(parent_proc->children == NULL)
	{
		return EINVAL;
	}

	int spl = splhigh();
	struct childprocinfo *child_process_info;
	if(list_get(parent_proc->children, pid, (void**)&child_process_info))
	{
		splx(spl);
		return EINVAL;
	}
	if(child_process_info->has_exited)
	{
		*status = child_process_info->exit_code;
		splx(spl);
		return 0;
	}

	if(curthread->t_vmspace != NULL)
	{
		evict_all_my_pages_if_necessary(curthread->t_vmspace);
	}

	if(child_process_info->has_exited)
	{
		*status = child_process_info->exit_code;
		splx(spl);
		return 0;
	}
	// Sleep on this child till he exits...sounds wrong :/
	thread_sleep(child_process_info->child_process_ptr);
	splx(spl);

	assert(child_process_info->has_exited);
	*status = child_process_info->exit_code;

	return 0;
}

int sys_write(struct trapframe *tf)
{
	int fd = tf->tf_a0;
	char *buf = (char*)tf->tf_a1;
	int nbytes = tf->tf_a2;

	if(fd != STDOUT_FILENO && fd != STDERR_FILENO)
	{
		kprintf("Error: Can't handle writes other than to stdout!\n");
		return 1;
	}

	// atomic write of nbytes of data
	int spl = splhigh();

	while(*buf != '\0' && nbytes >0)
	{
		kprintf("%c", *buf);
		++buf;
		--nbytes;
	}
	splx(spl);
	return 0;
}

int sys_read(struct trapframe *tf, int *retval)
{
	int fd = tf->tf_a0;
	char *buf = (char*)tf->tf_a1;
	int nbytes = tf->tf_a2;

	if(fd != STDIN_FILENO)
	{
		kprintf("Error: Can't handle reads other than from stdin!\n");
		return 1;
	}

	while(nbytes >0)
	{
		*buf = (char)getch();
		++buf;
		--nbytes;
	}

	*retval = tf->tf_a2 - nbytes;
	return 0;
}

void cleanup_children()
{
	if(curthread->children == NULL)
		return;
	struct childprocinfo *cpi;
	struct list_item *li = curthread->children->head, *next;
	int status;

	while(li != NULL)
	{
		next = li->next;
		sys_waitpid((void*)curthread, li->key, &status);
		list_remove(curthread->children, li->key, (void**)&cpi);
		kfree(cpi);
		li = next;
	}
	list_destroy(&curthread->children, kfree);
}

int sys_exit(struct trapframe *tf)
{
	if(curthread->exit_code != NULL)
		*(curthread->exit_code) = tf->tf_a0;
	if(curthread->has_exited != NULL)
		// Our status shouldn't say we have exited already
		assert(*(curthread->has_exited) == 0);

	cleanup_children();
	// All our children should have been cleaned up.
	assert(curthread->children == NULL);
	thread_exit();
	return 0;
}

void child_fork(void *child_tf, unsigned long addr_space)
{
	// This is the trapframe of parent. We will use the same
	// It must be allocated on the stack. Why? (troll question)
	struct trapframe my_tf;
	// This is the parent address space. We will use the same
	struct addrspace *my_addrspace = (struct addrspace*)addr_space;

	// Make sure our parent allocated a pid for us
	assert(curthread->pid != -1);
	assert(curthread->is_user_process == 1);
	// Copy the passed in trap frame into our stack
	memcpy(&my_tf, child_tf, sizeof(struct trapframe));
	// Free the kernel heap for the trap frame
	kfree((struct trapframe*)child_tf);

	// Now lets copy le address space and activate it
	assert(curthread->t_vmspace == NULL);
	curthread->t_vmspace = my_addrspace;
	as_activate(curthread->t_vmspace);

	// Return value for child is 0
	my_tf.tf_v0 = 0;
	// Signal no error
	my_tf.tf_a3 = 0;
	// Increment PC to point to next instruction after syscall
	my_tf.tf_epc += 4;

	mips_usermode(&my_tf);
}

int
md_forkentry(struct trapframe *tf, int *retval)
{
	// This will be our new process...which is just a thread...herp derp
	struct thread *new_thread = NULL;
	struct childprocinfo *cpi = NULL;

	*retval = get_new_pid();

	if(*retval == -1)
	{
		// No more pids available
		return EAGAIN;
	}

	// Create a copy of the trap frame (current state) of the parent
	// which we will pass to to child
	struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
	if(child_tf == NULL)
	{
		return ENOMEM;
	}
	memcpy(child_tf, tf, sizeof(struct trapframe));

	// Copy the address space of the parent which we will pass to the child
	struct addrspace *child_addrspace;
	if(as_copy(curthread->t_vmspace, &child_addrspace))
	{
		kfree(child_tf);
		return ENOMEM;
	}

	new_thread = thread_create(curthread->t_name);
	if(new_thread == NULL)
	{
		as_destroy(child_addrspace);
		kfree(child_tf);
		return ENOMEM;
	}

	int spl = splhigh();
	// Need to add this child to the list
	if(curthread->children == NULL)
	{
		curthread->children = list_create();
		if(curthread->children == NULL)
		{
			as_destroy(child_addrspace);
			kfree(child_tf);
			thread_destroy(new_thread);
			splx(spl);
			return ENOMEM;
		}
	}

	// Now create the info about this child that the parent will need later
	cpi = kmalloc(sizeof(struct childprocinfo));
	if(cpi == NULL)
	{
		as_destroy(child_addrspace);
		kfree(child_tf);
		thread_destroy(new_thread);
		splx(spl);
		return ENOMEM;
	}
	cpi->has_exited = 0;
	cpi->exit_code = -1;
	cpi->child_process_ptr = new_thread;
	new_thread->has_exited = &(cpi->has_exited);
	new_thread->exit_code = &(cpi->exit_code);
	if(list_insert(curthread->children, *retval, cpi))
	{
		as_destroy(child_addrspace);
		kfree(child_tf);
		kfree(cpi);
		thread_destroy(new_thread);
		splx(spl);
		return ENOMEM;
	}
	if(thread_fork_nalloc(curthread->t_name, child_tf, (unsigned long)child_addrspace,
			child_fork, new_thread))
	{
		DEBUG(DB_SYSCALL, "thread_fork failed.\n");
		kfree(child_tf);
		as_destroy(child_addrspace);
		// No need to free thread as it is already taken care of by thread_fork_nalloc
		kfree(cpi);
		splx(spl);
		return ENOMEM;
	}
	new_thread->pid = *retval;
	new_thread->is_user_process = 1;
	splx(spl);

	return 0;
}

/*
 * ENODEV 	The device prefix of program did not exist.
 * ENOTDIR 	A non-final component of program was not a directory.
 * ENOENT 	program did not exist.
 * EISDIR 	program is a directory.
 * ENOEXEC 	program is not in a recognizable executable file format, was for the wrong platform, or contained invalid fields.
 * ENOMEM 	Insufficient virtual memory is available.
 * E2BIG 	The total size of the argument strings is too large.
 * EIO 		A hard I/O error occurred.
 * EFAULT 	One of the args is an invalid pointer.
 */
int sys_execv(struct trapframe *tf)
{
	char *u_prog_name = (char *)tf->tf_a0;
	char **u_prog_args = (char **)tf->tf_a1;

	if(tf->tf_a0 ==(int)NULL || tf->tf_a1 == (int)NULL)
	{
		return EFAULT;
	}

	char ptr[NAME_MAX], name_copy_2[NAME_MAX];
	int error;
	size_t actual;
	error = copyinstr((const_userptr_t)tf->tf_a0,ptr,NAME_MAX,&actual);
	strcpy(name_copy_2, ptr);
	if(error)
	{
		return error;
	}

	if(strlen(u_prog_name) == 0)
	{
		return EINVAL;
	}

	error = copyinstr((const_userptr_t)tf->tf_a1,ptr,NAME_MAX,&actual);
	if(error)
	{
		return error;
	}

	if(strlen(u_prog_name) == 0)
	{
		return EINVAL;
	}

	int argc = 0;
	int bufLen = 0;

	int i;
	for(i = 0; u_prog_args[i] != NULL; i++, argc++);
	// Check for NULL pointer errors

    /*
     * Increase the size to hold the pointers (4 bytes) to the arguments which include filename + args.
     * It is terminated by a Null pointer which is why we have (argc + 1) instead of just argc
     */
    bufLen = (argc + 1)* 4;

    int *argvLen = (int *)kmalloc(argc * sizeof(int));
    if(argvLen == NULL)
	{
    	return ENOMEM;
	}

    /* Calculating the kernel buffer length that is required */
	for(i = 0; i < argc; i++)
	{
		error = copyinstr((const_userptr_t)u_prog_args[i],ptr,NAME_MAX,&actual);
		if(error)
		{
			return error;
		}
		argvLen[i] = strlen(u_prog_args[i]);
		bufLen += argvLen[i] + (4 - (argvLen[i]%4));
	}

    char *kbuf = (char *)kmalloc(bufLen * sizeof(char));
    if(kbuf == NULL)
    {
    	kfree(argvLen);
    	return ENOMEM;
    }

   /*
    * Copying the arguments (argv) with padding in the kernel buffer
    * Once we setup the stack for the new address space, we can
    * add the pointers to these arguments in the buffer
    */
    char *argv;
	argv = kbuf + ((argc + 1)*4);

	for(i = 0; i < argc; i++)
	{
		error = copyin((userptr_t)u_prog_args[i],argv,argvLen[i]);

		if(error)
		{
			kfree(argvLen);
			kfree(kbuf);
			return error;
		}

		argv = argv + argvLen[i];

		int j;
		for(j = 0; j < (4 - (argvLen[i]%4)); j++, argv++)
		{
			*argv = 0;
		}
	}

	/* Similar to runprogram */
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(u_prog_name, O_RDONLY, &v);
	if (result) {
		kfree(argvLen);
		kfree(kbuf);
		return result;
	}

	/* Save the old addr space - in case of errors
	 * we might have to run the old program */
	struct addrspace *old_addr_space = curthread->t_vmspace;

	// Integration with fork - we will be in a new thread (NULL vmspace)

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		// Might have to reassign old address space
		curthread->t_vmspace = old_addr_space;
		vfs_close(v);
		kfree(argvLen);
		kfree(kbuf);
		return ENOMEM;
	}

	as_destroy(old_addr_space);


	assert(strlen(name_copy_2) < MAX_EXEC_PATH_SIZE);
	strcpy(curthread->t_vmspace->exec_path, name_copy_2);
	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		kfree(argvLen);
		kfree(kbuf);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		kfree(argvLen);
		kfree(kbuf);
		return result;
	}


	int sp = stackptr - bufLen;
    argv = (char *)(sp + ((argc + 1)*4));
    memcpy(kbuf,&argv,sizeof(argv));

    for(i = 1; i < argc; i++)
    {
        argv = (char *)argv + argvLen[i-1] + (4 - (argvLen[i-1]%4));
        memcpy(kbuf+(i*4),&argv,sizeof(argv));
    }

    char *a;
    a = NULL;
    memcpy(kbuf+(argc*4),&a,sizeof(a));

   /* int spl = splhigh();
	for(i = bufLen - 1; i >= 0; i--)
	{
		kprintf("%x\n",kbuf[i]);
	}
	kprintf("%x %x\n",kbuf,stackptr);
	splx(spl);*/

	stackptr = stackptr - bufLen;
	error = copyout(kbuf,(userptr_t)stackptr,bufLen);
    kfree(argvLen);
    kfree(kbuf);
    if(error != 0)
	{
		return error;
	}

	/* Warp to user mode. */
	md_usermode(argc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
		    stackptr, entrypoint);

	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

int
sys_sbrk (int amount, int *retval)
{
	assert(curthread->t_vmspace != NULL);

	vaddr_t old_heap_vtop = curthread->t_vmspace->as_heap_vtop;

	if(amount == 0)
	{
		*retval = old_heap_vtop;
		return 0;
	}

	if((old_heap_vtop + amount) < curthread->t_vmspace->as_heap_vstart)
	{
		*retval = -1;
		return EINVAL;
	}

	if((old_heap_vtop + amount) >= curthread->t_vmspace->as_stack_vbase)
	{
		*retval = -1;
		return ENOMEM;
	}

	if(old_heap_vtop + amount - curthread->t_vmspace->as_heap_vstart > USER_HEAP_MAX)
	{
		*retval = -1;
		return ENOMEM;
	}

	curthread->t_vmspace->as_heap_vtop += amount;
	*retval = old_heap_vtop;

	return 0;
}










