/* 
 * stoplight.c
 *
 * 31-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: You can use any synchronization primitives available to solve
 * the stoplight problem in this file.
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
#include <curthread.h>
#include <synch.h>
#include <queue.h>


/*
 *
 * Constants
 *
 */

/*
 * Number of cars created.
 */

#define NCARS 20


/*
 *
 * Function Definitions
 *
 */


enum { NORTH, EAST, SOUTH, WEST }; // indices for the directions array below
static const char *directions[] = { "N", "E", "S", "W" };
enum {STRAIGHT, LEFT, RIGHT };

// Mutexes and queues
struct semaphore *NW, *NE, *SW, *SE, *sem_car_count;
struct semaphore *north_queue_sem, *south_queue_sem, *east_queue_sem, *west_queue_sem, *finished_count_sem;
struct semaphore *north_q_exit_sem, *south_q_exit_sem, *east_q_exit_sem, *west_q_exit_sem;
struct queue *north_q, *south_q, *east_q, *west_q;

// Keep track of how many cars have finished
int cars_finished;

static const char *msgs[] = {
        "approaching:",
        "region1:    ",
        "region2:    ",
        "region3:    ",
        "leaving:    "
};

/* use these constants for the first parameter of message */
enum { APPROACHING, REGION1, REGION2, REGION3, LEAVING };

static void
message(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection]);
}

 
/*
 * gostraight()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement passing straight through the
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber, struct semaphore *q_exit_sem)
{
	P(sem_car_count);
	if(cardirection == NORTH)
	{
		P(NW);
		message(REGION1, carnumber, cardirection, SOUTH);
		V(q_exit_sem);
		P(SW);
		message(REGION2, carnumber, cardirection, SOUTH);
		V(NW);
		message(LEAVING, carnumber, cardirection, SOUTH);
		V(SW);
	}
	else if(cardirection == SOUTH)
	{
		P(SE);
		message(REGION1, carnumber, cardirection, NORTH);
		V(q_exit_sem);
		P(NE);
		message(REGION2, carnumber, cardirection, NORTH);
		V(SE);
		message(LEAVING, carnumber, cardirection, NORTH);
		V(NE);
	}
	else if(cardirection == EAST)
	{
		P(NE);
		message(REGION1, carnumber, cardirection, WEST);
		V(q_exit_sem);
		P(NW);
		message(REGION2, carnumber, cardirection, WEST);
		V(NE);
		message(LEAVING, carnumber, cardirection, WEST);
		V(NW);
	}
	else if(cardirection == WEST)
	{
		P(SW);
		message(REGION1, carnumber, cardirection, EAST);
		V(q_exit_sem);
		P(SE);
		message(REGION2, carnumber, cardirection, EAST);
		V(SW);
		message(LEAVING, carnumber, cardirection, EAST);
		V(SE);
	}
	V(sem_car_count);
}


/*
 * turnleft()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a left turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber, struct semaphore *q_exit_sem)
{
	P(sem_car_count);
	if(cardirection == NORTH)
	{
		P(NW);
		message(REGION1, carnumber, cardirection, EAST);
		V(q_exit_sem);
		P(SW);
		message(REGION2, carnumber, cardirection, EAST);
		V(NW);
		P(SE);
		message(REGION3, carnumber, cardirection, EAST);
		V(SW);
		message(LEAVING, carnumber, cardirection, EAST);
		V(SE);
	}
	else if(cardirection == SOUTH)
	{
		P(SE);
		message(REGION1, carnumber, cardirection, WEST);
		V(q_exit_sem);
		P(NE);
		message(REGION2, carnumber, cardirection, WEST);
		V(SE);
		P(NW);
		message(REGION3, carnumber, cardirection, WEST);
		V(NE);
		message(LEAVING, carnumber, cardirection, WEST);
		V(NW);
	}
	else if(cardirection == EAST)
	{
		P(NE);
		message(REGION1, carnumber, cardirection, SOUTH);
		V(q_exit_sem);
		P(NW);
		message(REGION2, carnumber, cardirection, SOUTH);
		V(NE);
		P(SW);
		message(REGION3, carnumber, cardirection, SOUTH);
		V(NW);
		message(LEAVING, carnumber, cardirection, SOUTH);
		V(SW);
	}
	else if(cardirection == WEST)
	{
		P(SW);
		message(REGION1, carnumber, cardirection, NORTH);
		V(q_exit_sem);
		P(SE);
		message(REGION2, carnumber, cardirection, NORTH);
		V(SW);
		P(NE);
		message(REGION3, carnumber, cardirection, NORTH);
		V(SE);
		message(LEAVING, carnumber, cardirection, NORTH);
		V(NE);
	}
	V(sem_car_count);
}


/*
 * turnright()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a right turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber, struct semaphore *q_exit_sem)
{
	P(sem_car_count);
	if(cardirection == NORTH)
	{
		P(NW);
		message(REGION1, carnumber, cardirection, WEST);
                V(q_exit_sem);
		message(LEAVING, carnumber, cardirection, WEST);
		V(NW);
	}
	else if(cardirection == SOUTH)
	{
		P(SE);
		message(REGION1, carnumber, cardirection, EAST);
                V(q_exit_sem);
		message(LEAVING, carnumber, cardirection, EAST);
		V(SE);
	}
	else if(cardirection == EAST)
	{
		P(NE);
		message(REGION1, carnumber, cardirection, NORTH);
		V(q_exit_sem);
                message(LEAVING, carnumber, cardirection, NORTH);
		V(NE);
	}
	else if(cardirection == WEST)
	{
		P(SW);
		message(REGION1, carnumber, cardirection, SOUTH);
                V(q_exit_sem);
		message(LEAVING, carnumber, cardirection, SOUTH);
		V(SW);
	}
	V(sem_car_count);
}


void go_to_dir(int turn_direction, unsigned long cardirection, unsigned long carnumber, struct semaphore *q_exit_sem)
{
	switch(turn_direction)
	{
	case STRAIGHT: gostraight(cardirection, carnumber, q_exit_sem); break;
	case LEFT: turnleft(cardirection, carnumber, q_exit_sem); break;
	case RIGHT: turnright(cardirection, carnumber, q_exit_sem); break;
	default: panic("I don't understand this direction!\n"); break;
	}
}

int get_direction(int turn_direction, unsigned long cardirection)
{
	if(cardirection == NORTH)
	{
		switch(turn_direction)
		{
		case STRAIGHT: return SOUTH;
		case LEFT: return EAST;
		case RIGHT: return WEST;
		default: panic("I don't understand this direction!\n"); return 0;
		}
	}
	else if(cardirection == SOUTH)
	{
		switch(turn_direction)
		{
		case STRAIGHT: return NORTH;
		case LEFT: return WEST;
		case RIGHT: return EAST;
		default: panic("I don't understand this direction!\n"); return 0;
		}
	}
	else if(cardirection == EAST)
	{
		switch(turn_direction)
		{
		case STRAIGHT: return WEST;
		case LEFT: return SOUTH;
		case RIGHT: return NORTH;
		default: panic("I don't understand this direction!\n"); return 0;
		}
	}
	else if(cardirection == WEST)
	{
		switch(turn_direction)
		{
		case STRAIGHT: return EAST;
		case LEFT: return NORTH;
		case RIGHT: return SOUTH;
		default: panic("I don't understand this direction!\n"); return 0;
		}
	}
	panic("Invalid cardirection!");
	return 0;
}


/*
 * approachintersection()
 *
 * Arguments: 
 *      void * unusedpointer: currently unused.
 *      unsigned long carnumber: holds car id number.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Change this function as necessary to implement your solution. These
 *      threads are created by createcars().  Each one must choose a direction
 *      randomly, approach the intersection, choose a turn randomly, and then
 *      complete that turn.  The code to choose a direction randomly is
 *      provided, the rest is left to you to implement.  Making a turn
 *      or going straight should be done by calling one of the functions
 *      above.
 */ 
static
void
approachintersection(void * unusedpointer,
                     unsigned long carnumber)
{
	int cardirection, turn_direction;
	cardirection = random() % 4;
	turn_direction = random() % 3;
	(void) unusedpointer; //suppress warning
	struct semaphore *local_q_exit_sem = NULL;
	
	// Join the queue for that direction
	if(cardirection == NORTH)
	{
		P(north_queue_sem);
		q_addtail(north_q, curthread);
                message(APPROACHING, carnumber, cardirection, get_direction(turn_direction, cardirection));
		V(north_queue_sem);
		local_q_exit_sem = north_q_exit_sem;
	}
	else if(cardirection == SOUTH)
	{
		P(south_queue_sem);
                message(APPROACHING, carnumber, cardirection, get_direction(turn_direction, cardirection));
		q_addtail(south_q, curthread);
		V(south_queue_sem);
		local_q_exit_sem = south_q_exit_sem;
	}
	else if(cardirection == EAST)
	{
		P(east_queue_sem);
		q_addtail(east_q, curthread);
                message(APPROACHING, carnumber, cardirection, get_direction(turn_direction, cardirection));
		V(east_queue_sem);
		local_q_exit_sem = east_q_exit_sem;
	}
	else if(cardirection == WEST)
	{
		P(west_queue_sem);
		q_addtail(west_q, curthread);
                message(APPROACHING, carnumber, cardirection, get_direction(turn_direction, cardirection));
		local_q_exit_sem = west_q_exit_sem;
	        V(west_queue_sem);
	}
	else
	{
	        panic("I don't understand this direction. File %s:%d in function %s", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	}

        // We joined the queue...now go to sleep
	thread_sleep_wrapper(curthread);
	
	// We have been woken up. Its our turn to contend to enter the intersection	
	go_to_dir(turn_direction, cardirection, carnumber, local_q_exit_sem);

	P(finished_count_sem);
	cars_finished++;
	V(finished_count_sem);
}


/* createcars()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up the approachintersection() threads.  You are
 *      free to modiy this code as necessary for your solution.
 */

int
createcars(int nargs,
           char ** args)
{
        int index, error;
        struct thread *car_thread;
        cars_finished = 0;

        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;

        assert((NW = sem_create("NW", 1)) != NULL);
        assert((NE = sem_create("NE", 1)) != NULL);
        assert((SW = sem_create("SW", 1)) != NULL);
        assert((SE = sem_create("SE", 1)) != NULL);
        
        // The maximum number of cars allowed in the intersection at any
        // point in time is 3. This is because 4 cars can deadlock.
        assert((sem_car_count = sem_create("car_count", 3)) != NULL);
        
        
        assert((north_queue_sem = sem_create("north_queue_sem", 1)) != NULL);
        assert((south_queue_sem = sem_create("south_queue_sem", 1)) != NULL);
        assert((east_queue_sem = sem_create("east_queue_sem", 1)) != NULL);
        assert((west_queue_sem = sem_create("west_queue_sem", 1)) != NULL);
	assert((north_q_exit_sem = sem_create("north_q_exit_sem", 1)) != NULL);
	assert((south_q_exit_sem = sem_create("south_q_exit_sem", 1)) != NULL);
	assert((east_q_exit_sem = sem_create("east_q_exit_sem", 1)) != NULL);
	assert((west_q_exit_sem = sem_create("west_q_exit_sem", 1)) != NULL);
        assert((finished_count_sem = sem_create("finished count sem", 1)) != NULL);
        assert((north_q = q_create(5)) != NULL);
        assert((south_q = q_create(5)) != NULL);
        assert((east_q = q_create(5)) != NULL);
        assert((west_q = q_create(5)) != NULL);


        /*
         * Start NCARS approachintersection() threads.
         */

        for (index = 0; index < NCARS; index++) {

                error = thread_fork("approachintersection thread",
                                    NULL,
                                    index,
                                    approachintersection,
                                    NULL
                                    );

                /*
                 * panic() on error.
                 */

                if (error) {
                        
                        panic("approachintersection: thread_fork failed: %s\n",
                              strerror(error)
                              );
                }
        }

        // This while loop is basically to see if there are cars waiting on any of these queues
        // at each direction and wake up their threads. First it makes sure the queue is not empty
        // then it checks if the head car in the queue is sleeping yet. This is necessary because
        // the cars release the semaphore on the queue BEFORE going to sleep. So if there is a 
        // context switch just after it is added to the queue, it will be in the queue but not
        // sleeping. So if that is the case, we will not extract that car and continue to the
        // next queue. This loop yields after checking all the queues to give the cars a chance.
        // This loop continues till all NCARS have finished. We don't synchronize access to
        // cars_finished here because its ONLY BEING INCREMENTED and we care for the final value
        // only. The respective cars however synchronize their access to it.
        while(cars_finished != NCARS)
        {
        	P(north_queue_sem);
        	if(!q_empty(north_q) && thread_hassleepers_wrapper(q_getguy(north_q, q_getstart(north_q))))
        	{
        		car_thread = q_remhead(north_q);
        		V(north_queue_sem);
			// Make sure last car in the queue has entered next region. This is to prevent overtaking.
			P(north_q_exit_sem);
        		thread_wakeup_wrapper(car_thread, 1);
        	}
        	else
        	{
        		V(north_queue_sem);
        	}
        	P(south_queue_sem);
        	if(!q_empty(south_q) && thread_hassleepers_wrapper(q_getguy(south_q, q_getstart(south_q))))
        	{
        	     car_thread = q_remhead(south_q);
        	     V(south_queue_sem);
		     P(south_q_exit_sem);
        	     thread_wakeup_wrapper(car_thread, 1);
        	}
        	else
        	{
        		V(south_queue_sem);
        	}
        	P(east_queue_sem);
        	if(!q_empty(east_q) && thread_hassleepers_wrapper(q_getguy(east_q, q_getstart(east_q))))
        	{
        	     car_thread = q_remhead(east_q);
        	     V(east_queue_sem);
		     P(east_q_exit_sem);
        	     thread_wakeup_wrapper(car_thread, 1);
        	}
        	else
        	{
        		V(east_queue_sem);
        	}
        	P(west_queue_sem);
        	if(!q_empty(west_q) && thread_hassleepers_wrapper(q_getguy(west_q, q_getstart(west_q))))
        	{
        	     car_thread = q_remhead(west_q);
        	     V(west_queue_sem);
		     P(west_q_exit_sem);
        	     thread_wakeup_wrapper(car_thread, 1);
        	}
        	else
        	{
        		V(west_queue_sem);
        	}
        	thread_yield();
        }

        sem_destroy(NW);
        sem_destroy(NE);
        sem_destroy(SW);
        sem_destroy(SE);
        sem_destroy(sem_car_count);
        sem_destroy(north_queue_sem);
        sem_destroy(south_queue_sem);
        sem_destroy(east_queue_sem);
        sem_destroy(west_queue_sem);
	sem_destroy(north_q_exit_sem);
	sem_destroy(south_q_exit_sem);
	sem_destroy(east_q_exit_sem);
	sem_destroy(west_q_exit_sem);
        sem_destroy(finished_count_sem);
        q_destroy(north_q);
        q_destroy(south_q);
        q_destroy(east_q);
        q_destroy(west_q);

        return 0;
}
