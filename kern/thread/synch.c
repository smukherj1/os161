/*
 * Synchronization primitives.
 * See synch.h for specifications of the functions.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <curthread.h>
#include <machine/spl.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *namearg, int initial_count)
{
	struct semaphore *sem;

	assert(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}

	sem->name = kstrdup(namearg);
	if (sem->name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	spl = splhigh();
	assert(thread_hassleepers(sem)==0);
	splx(spl);

	/*
	 * Note: while someone could theoretically start sleeping on
	 * the semaphore after the above test but before we free it,
	 * if they're going to do that, they can just as easily wait
	 * a bit and start sleeping on the semaphore after it's been
	 * freed. Consequently, there's not a whole lot of point in 
	 * including the kfrees in the splhigh block, so we don't.
	 */

	kfree(sem->name);
	kfree(sem);
}

void 
P(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	assert(in_interrupt==0);

	spl = splhigh();
	while (sem->count==0) {
		thread_sleep(sem);
	}
	assert(sem->count>0);
	sem->count--;
	splx(spl);
}

void
V(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);
	spl = splhigh();
	sem->count++;
	assert(sem->count>0);
	thread_wakeup(sem);
	splx(spl);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->name = kstrdup(name);
	if (lock->name == NULL) {
		kfree(lock);
		return NULL;
	}
	
	lock->lock_held = 0;
	lock->lock_holder = NULL;
	
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);
	
	kfree(lock->name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	int spl = splhigh();

	assert(!lock_do_i_hold(lock));
	// Spin till we get the lock
	while(lock->lock_held == 1)
	{
		thread_sleep(lock);
	}

	// Lock is available. Acquire it, turn on interrupts and return
	lock->lock_held = 1;
	lock->lock_holder = curthread;
	splx(spl);

}

void
lock_release(struct lock *lock)
{
	// Turn off interrupts
	int spl = splhigh();

	assert(lock != NULL);
	// We should be holding the lock
	assert(lock->lock_held && (lock->lock_holder == curthread));

	// Sanity checks done. Release lock, free memory of holder name and return
	lock->lock_held = 0;
	lock->lock_holder = NULL;
	thread_wakeup_one(lock);

	// Turn interrupts back on
	splx(spl);
}

int
lock_do_i_hold(struct lock *lock)
{
	// Basically the lock_held should be 1 and the name of the lock holder should be the name of current thread
	return (lock->lock_held && (lock->lock_holder == curthread));
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->name = kstrdup(name);
	if (cv->name==NULL) {
		kfree(cv);
		return NULL;
	}
	
	// add stuff here as needed
	
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	// add stuff here as needed
	
	kfree(cv->name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Release the lock
	lock_release(lock);

	// Sleep on cv
	thread_sleep_wrapper(cv);

	// Once awake acquire the lock again
	lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Check if the lock passed by the caller is the same as in the cv struct

	assert(lock->lock_holder == curthread);

	// If there are threads sleeping on the condition wake up one of them
	if(thread_hassleepers_wrapper(cv))
	{
		thread_wakeup_wrapper(cv,1);
	}
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Check if the lock passed by the caller is the same as in the cv struct
	assert(lock->lock_holder == curthread);

	// If there are threads sleeping on the condition wake all one of them
	if(thread_hassleepers_wrapper(cv))
	{
		thread_wakeup_wrapper(cv,0);
	}
}
