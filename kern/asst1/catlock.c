/*
 * catlock.c
 *
 * 30-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: Please use LOCKS/CV'S to solve the cat syncronization problem in 
 * this file.
 */


/*
 * 
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>


/*
 * 
 * Constants
 *
 */

/*
 * Number of food bowls.
 */

#define NFOODBOWLS 2

/*
 * Number of cats.
 */

#define NCATS 6

/*
 * Number of mice.
 */

#define NMICE 2

/*
 * Global variables
 */
struct lock *cat_mouse_lock, *bowl_lock[NFOODBOWLS], *finished_count_lock;
int cats_currently_eating = 0;
int num_finished = 0;


/*
 * 
 * Function Definitions
 * 
 */

/* who should be "cat" or "mouse" */
static void
lock_eat(const char *who, int num, int bowl, int iteration)
{
        kprintf("%s: %d starts eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
        clocksleep(1);
        kprintf("%s: %d ends eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
}

/*
 * Same parameters as lock_eat function above. This function will be called by either catlock or mouselock
 * when they have decided to eat. This function will randomly pick a bowl and synchronize access to the
 * bowl.
 */
static void try_eat_some_bowl(const char *who, int num, int iteration)
{
	int bowl = (int)(random() % NFOODBOWLS) + 1;

	// So we have a random bowl number. Lets wait for a lock on this bowl
	// eat from it and then return
	lock_acquire(bowl_lock[bowl]);
	lock_eat(who, num, bowl, iteration);
	lock_release(bowl_lock[bowl]);
}

/*
 * catlock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS -
 *      1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
catlock(void * unusedpointer, 
        unsigned long catnumber)
{
    /*
     * Avoid unused variable warnings.
     */
	(void)unusedpointer;

	int i;
	for(i = 0; i < 4;)
	{
		lock_acquire(cat_mouse_lock);
        // I could have used <= 1 below but I'm just making sure its also greater or equal to 0.
        // Could have probably used an assert but whatever.
		if(cats_currently_eating == 0 || cats_currently_eating == 1)
		{
			cats_currently_eating++;
			lock_release(cat_mouse_lock);
			try_eat_some_bowl("cat", catnumber, i);

			// We finished eating. Reduce the cats_currently_eating count.
			lock_acquire(cat_mouse_lock);
			cats_currently_eating--;
			lock_release(cat_mouse_lock);
			// increment our iteration number
			++i;

		}
		else
		{
			// We can't eat now..so lets release the lock
			lock_release(cat_mouse_lock);
		}
		thread_yield();
	}

	// This cat has finished.
	lock_acquire(finished_count_lock);
	++num_finished;
	lock_release(finished_count_lock);
}
	

/*
 * mouselock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long mousenumber: holds the mouse identifier from 0 to 
 *              NMICE - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
mouselock(void * unusedpointer,
          unsigned long mousenumber)
{
        /*
         * Avoid unused variable warnings.
         */
	(void) unusedpointer;
    int i;

    for(i = 0; i < 4;)
    {
    	lock_acquire(cat_mouse_lock);

    	// When 0 cats are eating, it means the bowls are free for us.
    	// When it says '3' or greater cats it means mice are eating. This is because in
    	// this function we set cats_currently_eating to 3 to prevent cats
    	// from coming to the bowls. This is because the cats have a mutual
    	// agreement that there can be a max of 2 cats at the bowls at any given
    	// time
    	if(cats_currently_eating == 0 || cats_currently_eating >= 3)
    	{
    		// If 0 cats then make it three(we are the first mouse) else increase the count(there are other mouse present)
    		cats_currently_eating = (cats_currently_eating == 0) ? 3 : (cats_currently_eating + 1);
    		lock_release(cat_mouse_lock);
    		try_eat_some_bowl("mouse", mousenumber, i);

    		// Finished eating...now decrease count
    		lock_acquire(cat_mouse_lock);
    		cats_currently_eating = (cats_currently_eating == 3) ? 0 : (cats_currently_eating - 1);
    		lock_release(cat_mouse_lock);
    		//increase our iteration number
    		++i;
    	}
    	else
    	{
    		lock_release(cat_mouse_lock);
    	}
    	thread_yield();
    }

    // This mouse has finished.
    lock_acquire(finished_count_lock);
    ++num_finished;
    lock_release(finished_count_lock);
}


/*
 * catmouselock()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catlock() and mouselock() threads.  Change
 *      this code as necessary for your solution.
 */

int
catmouselock(int nargs,
             char ** args)
{
        int index, error;
   
        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;

        // Create the locks
        cat_mouse_lock = lock_create("cat_mouse_lock");
        assert(cat_mouse_lock != NULL);
        finished_count_lock = lock_create("finished count lock");
        assert(finished_count_lock != NULL);
        for(index = 0; index < NFOODBOWLS; ++index)
        {
        	bowl_lock[index] = lock_create("bowl_lock");
        	assert(bowl_lock[index] != NULL);
        }
        cats_currently_eating = 0;
        num_finished = 0;
   
        /*
         * Start NCATS catlock() threads.
         */

        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catlock thread", 
                                    NULL, 
                                    index, 
                                    catlock, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catlock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        /*
         * Start NMICE mouselock() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mouselock thread", 
                                    NULL, 
                                    index, 
                                    mouselock, 
                                    NULL
                                    );
      
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mouselock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        // Wait for everyone to finish
        while(num_finished < (NCATS + NMICE))
        {
        	thread_yield();
        }

        // Destroy locks
        lock_destroy(cat_mouse_lock);
        lock_destroy(finished_count_lock);
        for(index = 0; index < NFOODBOWLS; ++index)
        {
        	lock_destroy(bowl_lock[index]);
        }

        return 0;
}

/*
 * End of catlock.c
 */
