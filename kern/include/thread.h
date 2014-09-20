#ifndef _THREAD_H_
#define _THREAD_H_

/*
 * Definition of a thread.
 */

/* Get machine-dependent stuff */
#include <machine/pcb.h>


struct addrspace;

struct thread {
	/**********************************************************/
	/* Private thread members - internal to the thread system */
	/**********************************************************/
	
	struct pcb t_pcb;
	char *t_name;
	const void *t_sleepaddr;
	char *t_stack;
	
	/**********************************************************/
	/* Public thread members - can be used by other code      */
	/**********************************************************/
	
	/*
	 * This is public because it isn't part of the thread system,
	 * and will need to be manipulated by the userprog and/or vm
	 * code.
	 */
	struct addrspace *t_vmspace;

	/*
	 * This is public because it isn't part of the thread system,
	 * and is manipulated by the virtual filesystem (VFS) code.
	 */
	struct vnode *t_cwd;

	/*
	 * *************************************************************
	 * Data members for user processes
	 * *************************************************************
	 */

	/*
	 * This variable is to say if this thread is a user process or not.
	 * 1 when it is a user process
	 * 0 when it is a kernel thread
	 */
	volatile int is_user_process;

	/*
	 * This is the process id (pid) of the thread only if it is a user
	 * process. This variable has no meaning for kernel threads.
	 */
	volatile int pid;

	/*
	 * The thread that created this thread
	 */
	struct thread *parent_thread;

	/*
	 * List of children
	 */
	struct list *children;

	/*
	 * Has exited
	 */
	volatile int *has_exited;

	/*
	 * Exit code
	 */
	volatile int *exit_code;
};

/* Call once during startup to allocate data structures. */
struct thread *thread_bootstrap(void);

/* Call during panic to stop other threads in their tracks */
void thread_panic(void);

/* Call during shutdown to clean up (must be called by initial thread) */
void thread_shutdown(void);

/*
 * Create a thread. This is used both to create the first thread's
 * thread structure and to create subsequent threads.
 */

struct thread *
thread_create(const char *name);

/*
 * Destroy a thread.
 *
 * This function cannot be called in the victim thread's own context.
 * Freeing the stack you're actually using to run would be... inadvisable.
 */
void
thread_destroy(struct thread *thread);

/*
 * Make a new thread, which will start executing at "func".  The
 * "data" arguments (one pointer, one integer) are passed to the
 * function.  The current thread is used as a prototype for creating
 * the new one. If "ret" is non-null, the thread structure for the new
 * thread is handed back. (Note that using said thread structure from
 * the parent thread should be done only with caution, because in
 * general the child thread might exit at any time.) Returns an error
 * code.
 */
int thread_fork(const char *name, 
		void *data1, unsigned long data2, 
		void (*func)(void *, unsigned long),
		struct thread **ret);

/*
 * Same as thread_form above but it assumes memory has already been allocated
 * for the thread structure
 */
int thread_fork_nalloc(const char *name,
		void *data1, unsigned long data2,
		void (*func)(void *, unsigned long),
		struct thread *newguy);

/*
 * Cause the current thread to exit.
 * Interrupts need not be disabled.
 */
void thread_exit(void);

/*
 * Cause the current thread to yield to the next runnable thread, but
 * itself stay runnable.
 * Interrupts need not be disabled.
 */
void thread_yield(void);

/*
 * Cause the current thread to yield to the next runnable thread, and
 * go to sleep until wakeup() is called on the same address. The
 * address is treated as a key and is not interpreted or dereferenced.
 * Interrupts must be disabled.
 */
void thread_sleep(const void *addr);

/*
 * Just a wrapper around thread_sleep that turns off interrupts and calls it
 */

void thread_sleep_wrapper(const void *addr);

/*
 * Cause all threads sleeping on the specified address to wake up.
 * Interrupts must be disabled.
 */
void thread_wakeup(const void *addr);

/*
 * Wake up one thread who is sleeping on "sleep address"
 * ADDR.
 */
void thread_wakeup_one(const void *addr);

/*
 * Just a wrapper around thread_wakeup that turns off interrupts before calling it
 */
void thread_wakeup_wrapper(const void *addr, int wakeup_mode);

/*
 * Return nonzero if there are any threads sleeping on the specified
 * address. Meant only for diagnostic purposes.
 */
int thread_hassleepers(const void *addr);

/*
 * Just a wrapper around thread_hassleepers that turns off interrupts before calling it
 */
int thread_hassleepers_wrapper(const void *addr);


/*
 * Private thread functions.
 */

/* Machine independent entry point for new threads. */
void mi_threadstart(void *data1, unsigned long data2, 
		    void (*func)(void *, unsigned long));

/* Machine dependent context switch. */
void md_switch(struct pcb *old, struct pcb *nu);


#endif /* _THREAD_H_ */
