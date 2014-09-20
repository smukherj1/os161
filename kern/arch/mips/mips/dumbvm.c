#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <swap.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <elf.h>
#include <vfs.h>
#include <synch.h>

/*
 * RAM available for kernel and user page allocations and deallocations
 */
static paddr_t free_paddr = 0, last_paddr = 0;
struct lock *core_map_lock = NULL;

/* Core map for page management */
static struct page *pages = NULL;
static int num_pages = 0;

struct page_table* get_ptbl(struct addrspace *as, vaddr_t vpn, int is_executable);

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under dumbvm, always have 96k of user stack */
#define DUMBVM_STACKPAGES    24
#define MIN_COREMAP_PAGES 10

void
vm_bootstrap(void)
{
	paddr_t lo, pages_start_addr, coremap_end_addr;
	int coremapsize_bytes, coremapsize_kbytes;

	core_map_lock = kmalloc(sizeof(struct lock));
	assert(core_map_lock != NULL);
	core_map_lock->lock_held = 0;
	core_map_lock->lock_holder = NULL;
	core_map_lock->name = NULL;

	// Get size of ram. ram_stealmem won't work after this point
	ram_getsize(&lo, &last_paddr);
	pages_start_addr = (last_paddr - PAGE_SIZE) & PAGE_FRAME;
	coremap_end_addr = lo + sizeof(struct page);
	num_pages = 1;

	if(pages_start_addr < coremap_end_addr)
	{
		panic("Don't have space for even 1 page in physical memory\n");
	}

	// Now we need to figure the coremap size and number of pages we can allocate
	// The pages have to be aligned to PAGE_SIZE
	while((pages_start_addr - PAGE_SIZE)>= (coremap_end_addr + sizeof(struct page)))
	{
		pages_start_addr -= PAGE_SIZE;
		coremap_end_addr += sizeof(struct page);
		++num_pages;
	}

	coremapsize_bytes = num_pages * sizeof (struct page);
	if(num_pages < MIN_COREMAP_PAGES)
	{
		panic("Couldn't even allocate %d pages for the coremap!\n", MIN_COREMAP_PAGES);
	}

	pages = (struct page*)PADDR_TO_KVADDR(lo);
	free_paddr = pages_start_addr;
	last_paddr = free_paddr + PAGE_SIZE * num_pages;

	// set core map to zero. Means none of the pages are currently valid.
	bzero(pages, coremapsize_bytes);

	// Print stats about the core map. Might be handy for debugging purposes
	coremapsize_kbytes = coremapsize_bytes / 1024;
	coremapsize_bytes = coremapsize_bytes % 1024;
	kprintf("Virtual Memory bootstrap successful. Have room for %d pages.\n",	num_pages);
	kprintf("Core map size --> %d kbytes %d bytes\n", coremapsize_kbytes, coremapsize_bytes);
	kprintf("Size of each core map entry %d\n", sizeof(struct page));
	kprintf("Paged address range 0x%x to 0x%x\n", free_paddr, last_paddr);

}

static
paddr_t
getppages(unsigned long npages)
{
	int spl;
	paddr_t addr;

	spl = splhigh();

	addr = ram_stealmem(npages);
	
	splx(spl);
	return addr;
}

/*
 * Function to determine if a contiguous 'npages' pages are available
 * in the coremap starting from the 'start' location
 * The actual number of contiguous free pages from 'start' will
 * be store in count.
 */
int can_i_alloc_npages(struct page *start, unsigned long npages, unsigned long *count)
{
	*count = 0;
	vaddr_t start_vaddr = (vaddr_t)start;
	vaddr_t coremap_end_addr = (vaddr_t)(pages + num_pages);

	while(!(start->flags & PFLAG_USED_MASK) && (*count < npages) &&
			(start_vaddr < coremap_end_addr))
	{
		(*count)++;
		start++;
		start_vaddr = (vaddr_t)start;
	}

	if(*count == npages)
		return 1; // contiguous pages available!
	else
		return 0;
}

/*
 * Change the entries in the core map to say these pages now belong to le kernel
 */
void setup_coremap_for_kpages(struct page *start, unsigned long npages)
{
	unsigned long i; // This unsigned long is kinda overkill given that we are dealing with a 512k RAM
	for(i = 0; i < npages; ++i)
	{
		start->as = NULL;
		start->flags = PFLAG_USED_MASK;
		start++;
	}
}

/*
 * Get 'npages' pages for the kernel from the virtual memory system.
 */
static paddr_t getkpagesfromvm(unsigned long npages)
{
	/*
	 * Our kernel requires a contiguous memory of 'npages' pages in memory
	 */
	paddr_t addr = 0;
	int i = 0;
	unsigned long count;

	lock_acquire(core_map_lock);
	while(i < num_pages)
	{
		if(can_i_alloc_npages(pages + i, npages, &count))
		{
			setup_coremap_for_kpages(pages + i, npages);
			addr = free_paddr + i * PAGE_SIZE;
			pages[i].flags |= ((u_int32_t)npages & PFLAG_NUM_CONTG_PAGES);
//			kprintf("Allocated %lu pages from 0x%x to 0x%x\n", npages, addr, addr + npages * PAGE_SIZE);
			break;
		}
		i += count;

		// We couldn't get a large enough free memory chunk. Find the next free chunk
		while(i < num_pages && (pages[i].flags & PFLAG_USED_MASK))
		{
			i++;
		}
	}
	lock_release(core_map_lock);

	return addr;
}


// Called by kernel when finished executing user program. This is to prevent
// any leaks
void reclaim_all_user_pages()
{
	int spl = splhigh();
	int i;
	for(i = 0; i < num_pages; ++i)
	{
		if(pages[i].as != NULL)
		{
			pages[i].flags = 0;
		}
	}
	splx(spl);
}

// This function will be called by a user process before going to sleep
// in sys_waitpid. This function will evict its pages if the coremap is
// full
void evict_all_my_pages_if_necessary(struct addrspace *as)
{
	lock_acquire(core_map_lock);

	int i;
	// First scan the coremap to check if it is full. If not we just return
	for(i = 0; i < num_pages; ++i)
	{
		if(!(pages[i].flags & PFLAG_USED_MASK))
		{
			// free page in coremap
			lock_release(core_map_lock);
			return;
		}
	}

	for(i = 0; i < num_pages; ++i)
	{
		// 'as' is non NULL for user pages
		if(pages[i].as == as && (pages[i].flags & PFLAG_USED_MASK))
		{
			// Is this vpn an executable page? In that case let the page table finder
			// know. He will not evict this page table
			int is_executable = (pages[i].vpn >= pages[i].as->as_vbase1) &&
					(pages[i].vpn < (pages[i].as->as_vbase1 + pages[i].as->as_npages1 * PAGE_SIZE));
			// Get this user's pages table for this vpn
			struct page_table *pgtbl = get_ptbl(pages[i].as, pages[i].vpn, is_executable);
			int page_index = (pages[i].vpn & PGTBL_INDEX) >> 12;
			// His page mappping is not invalid
			pgtbl[page_index].pg_tbl_entry &= ~PGTBL_VALID_MASK;
			// Swap out the page
			swap_out_page(pages[i].as, pages[i].vpn, free_paddr + i * PAGE_SIZE);
			// Free coremap entry
			pages[i].flags = 0;
		}
	}
	lock_release(core_map_lock);
}

void evict_all_user_pages()
{
	lock_acquire(core_map_lock);

	int i;
	for(i = 0; i < num_pages; ++i)
	{
		// 'as' is non NULL for user pages
		if(pages[i].as != NULL && (pages[i].flags & PFLAG_USED_MASK))
		{
			// Is this vpn an executable page? In that case let the page table finder
			// know. He will not evict this page table
			int is_executable = (pages[i].vpn >= pages[i].as->as_vbase1) &&
					(pages[i].vpn < (pages[i].as->as_vbase1 + pages[i].as->as_npages1 * PAGE_SIZE));

			// Get this user's pages table for this vpn
			struct page_table *pgtbl = get_ptbl(pages[i].as, pages[i].vpn, is_executable);
			int page_index = (pages[i].vpn & PGTBL_INDEX) >> 12;
			// His page mappping is not invalid
			pgtbl[page_index].pg_tbl_entry &= ~PGTBL_VALID_MASK;
			// Swap out the page
			swap_out_page(pages[i].as, pages[i].vpn, free_paddr + i * PAGE_SIZE);
			// Free coremap entry
			pages[i].flags = 0;
		}
	}

	lock_release(core_map_lock);
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;

	// First attempt to steal the ram directly
	pa = getppages(npages);

	if (pa != 0) {
		return PADDR_TO_KVADDR(pa);
	}

	// ram_stealmem failed. This means our virtual memory system is alive. We will need to request
	// a page from him then.
	pa = getkpagesfromvm(npages);

	if(pa != 0)
	{
		return PADDR_TO_KVADDR(pa);
	}

	// Our vm couldn't allocate pages for us. One reason may be that too much memory is being taken
	// by user pages. Evict all user pages and try again
	evict_all_user_pages();
	// One last attempt
	pa = getkpagesfromvm(npages);

	if(pa != 0)
	{
		return PADDR_TO_KVADDR(pa);
	}


	return 0;
}

void 
free_kpages(vaddr_t addr)
{
	vaddr_t coremap_start = PADDR_TO_KVADDR(free_paddr);
	int page_index = (addr - coremap_start) / PAGE_SIZE;

	// These assertions are to check to make sure we are trying
	// to free the address for a valid page that was actually
	// allocated to the kernel
	assert(page_index >= 0 && page_index < num_pages);
	int spl = splhigh();
	int num_contiguous_pages = pages[page_index].flags & PFLAG_NUM_CONTG_PAGES;
	assert((page_index + num_contiguous_pages) <= num_pages);
	assert(pages[page_index].as == NULL && (pages[page_index].flags & PFLAG_USED_MASK));
//	kprintf("Freed page at address 0x%x\n", addr & 0x7fffffff);
	// Now free the pages

	int i;
	for(i = 0; i < num_contiguous_pages; ++i)
	{
		pages[page_index + i].flags = 0;
	}
	splx(spl);
}

// Macro to take the absolute difference of two unsigned numbers
#define UNSIGNED_DIFF(a,b) (((a) > (b)) ? (a-b) : (b-a))

// -------------------------------------------------------------------------------------------------------------
/*
 * Doesn't do a lot now but will be used during swapping
 */
paddr_t
make_pg_available(struct addrspace *as, vaddr_t vpn, int *free_index)
{
	// The policy we will use here is we will try to evict a page that is in
	// the same region (code, data or stack) as the faulting vpn and farthest
	// away for it within that region. This is kind of a locality thing.

	// If no other page in that region is loaded, we will try to evict a page
	// in random

	// the core map should already be locked
	assert(lock_do_i_hold(core_map_lock));
	int should_i_swap_out = 1;

	// Figure out which region we belong to
	vaddr_t vbase = 0, vtop = 0;
	vaddr_t executable_vbase, executable_vtop;
	if(as->as_flags1 & PF_X)
	{
		executable_vbase = as->as_vbase1;
		executable_vtop = as->as_vbase1 + (as->as_npages1 * PAGE_SIZE);
	}
	else
	{
		executable_vbase = as->as_vbase2;
		executable_vtop = as->as_vbase2 + (as->as_npages2 * PAGE_SIZE);;

	}
	if(as->as_vbase1 <= vpn && vpn < (as->as_vbase1 + (as->as_npages1 * PAGE_SIZE)))
	{
		vbase = as->as_vbase1;
		vtop = as->as_vbase1 + (as->as_npages1 * PAGE_SIZE);
	}
	else if(as->as_vbase2 <= vpn && vpn < (as->as_vbase2 + (as->as_npages2 * PAGE_SIZE)))
	{
		vbase = as->as_vbase2;
		vtop = as->as_vbase2 + (as->as_npages2 * PAGE_SIZE);
	}
	else if(as->as_stack_vbase <= vpn && vpn < USERSTACK)
	{
		vbase = as->as_stack_vbase;
		vtop = USERSTACK;
	}
	else if(as->as_heap_vstart <= vpn && vpn < as->as_heap_vtop)
	{
		vbase = as->as_heap_vstart;
		vtop = as->as_heap_vtop;
	}
	else
	{
		panic("Segmentation fault?! This should have already been handled in vm_fault()!\n");
	}

	int victim_index;

#if 0

	int max_diff_index = -1;

	// This is a fall back option if there were no pages in the same region
	// as our vpn
	int alternate_index = -1;

	// Scan the coremap and find the page in the same region with largest address difference.
	// Keep looping till we find a suitable victim
	int i;
	while(1)
	{
		vaddr_t addr_diff = 0;
		for(i = 0; i < num_pages; ++i)
		{
			if((pages[i].flags & PFLAG_USED_MASK) == 0)
			{
				// Some page has been freed
				max_diff_index = i;
				should_i_swap_out = 0;
				break;
			}
			if(pages[i].as == as && pages[i].vpn >= vbase && pages[i].vpn < vtop)
			{
				if(addr_diff <= UNSIGNED_DIFF(vpn, pages[i].vpn))
				{
					// We have a larger address diff!
					max_diff_index = i;
					addr_diff = UNSIGNED_DIFF(vpn, pages[i].vpn);
				}
			}
			else if(pages[i].as == as && max_diff_index == -1 && alternate_index == -1)
			{
				// We don't have any options currently. Reserve a non code page to
				// evict for the alternate_index
				if(vpn < executable_vbase || vpn > executable_vtop)
				{
					// Non code page
					alternate_index = i;
				}
			}
		}

		if(max_diff_index == -1 && alternate_index == -1)
		{
			// No suitable candidates found after scanning the entire core map
			// Let other people run and hope they free some pages
			kprintf("Yielding. Need space for 0x%x\n", vpn);
			lock_release(core_map_lock);
			thread_yield();
			lock_acquire(core_map_lock);
		}
		else
		{
			break;
		}
	}

	if(max_diff_index != -1)
	{
		victim_index = max_diff_index;
	}
	else
	{
		victim_index = alternate_index;
	}
#endif

//#if 0
	while(1)
	{
		int i_start = random() % num_pages;
		int i;
		victim_index = -1;
		for(i = i_start; i < num_pages; ++i)
		{
			if(!(pages[i].flags & PFLAG_USED_MASK))
			{
				victim_index = i;
				should_i_swap_out = 0;
				break;
			}
			else if(pages[i].as == as)
			{
				victim_index = i;
				break;
			}
		}
		if(victim_index == -1)
		{
			for(i = i_start; i >= 0; --i)
			{
				if(!(pages[i].flags & PFLAG_USED_MASK))
				{
					victim_index = i;
					should_i_swap_out = 0;
					break;
				}
				else if(pages[i].as == as)
				{
					victim_index = i;
					break;
				}
			}
		}

		if(victim_index == -1)
		{
			lock_release(core_map_lock);
			thread_yield();
			lock_acquire(core_map_lock);
		}
		else
		{
			break;
		}
	}
//#endif

	// We have our victim. Swap him out, remove the valid bit in his page table
	// entry and return this physical address
	vaddr_t victim_vpn = pages[victim_index].vpn;
	int victim_is_executable = 0;
	struct addrspace *victim_as = pages[victim_index].as;
	paddr_t page_addr = free_paddr + victim_index * PAGE_SIZE;

	// Update victim's page table only if this page was in use
	if(pages[victim_index].flags & PFLAG_USED_MASK)
	{
		assert(victim_as != NULL);
		if(victim_as->as_flags1 & PF_X)
		{
			executable_vbase = victim_as->as_vbase1;
			executable_vtop = victim_as->as_vbase1 + victim_as->as_npages1 * PAGE_SIZE;
		}
		else
		{
			assert(victim_as->as_flags2 & PF_X);
			executable_vbase = victim_as->as_vbase2;
			executable_vtop = victim_as->as_vbase2 + victim_as->as_npages2 * PAGE_SIZE;
		}
		if(victim_vpn >= executable_vbase && victim_vpn < executable_vtop)
		{
			victim_is_executable = 1;
			// No need to swap. Victim will be demand loaded
			should_i_swap_out = 0;
		}
		struct page_table *pg_tbl = get_ptbl(victim_as, victim_vpn, victim_is_executable);
		int victim_page_table_index = (victim_vpn & PGTBL_INDEX) >> 12;
		pg_tbl[victim_page_table_index].pg_tbl_entry &= ~PGTBL_VALID_MASK;
		assert(page_addr == (pg_tbl[victim_page_table_index].pg_tbl_entry & PAGE_FRAME));
	}
#if 0
	if(!(page_addr == (pg_tbl[victim_page_table_index].pg_tbl_entry & PAGE_FRAME)))
	{
		kprintf("Address 0x%x\n", &pg_tbl[victim_page_table_index].pg_tbl_entry);
		panic("Victim vpn: 0x%x, page_addr: 0x%x, page_table_entry: 0x%x\n",
				victim_vpn, page_addr, pg_tbl[victim_page_table_index].pg_tbl_entry);
	}
#endif

	// Invalidate victim's TLB entry
	u_int32_t ehi, elo;
	int i;
	for(i = 0; i < NUM_TLB; ++i)
	{
		TLB_Read(&ehi, &elo, i);
		if ((ehi & TLBHI_VPAGE) == victim_vpn)
		{
			TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
		}
	}

	if(should_i_swap_out)
	{
//		kprintf("Swapping out 0x%x for addrspace 0x%x\n", victim_vpn, victim_as);
		swap_out_page(victim_as, victim_vpn, page_addr);
	}

	// Set core map entry to free
	pages[victim_index].flags = 0;
	*free_index = victim_index;

	return page_addr;
}

/*
 * Page alloc and free for user processes
 */
paddr_t
alloc_page(struct addrspace *cur_proc, vaddr_t vpn)
{
	assert(pages != NULL);
	paddr_t page_paddr = 0;

	lock_acquire(core_map_lock);

	/*
	 * Scan the coremap to find a free page,
	 * if no free pages are found we panic (for now).
	 * This will change when we implement swapping
	 */
	int i = 0;
	for(i = 0; i < num_pages; i++)
	{
		if(!(pages[i].flags & PFLAG_USED_MASK))
		{
			// Found a free page
			page_paddr = free_paddr + i * PAGE_SIZE;
			break;
		}
	}

	// If there were no free pages, make one available by swapping
	if(page_paddr == 0)
		page_paddr = make_pg_available(cur_proc, vpn, &i);

	// Swapping should always work
	assert(page_paddr != 0);

	// Upadate the coremap
	assert(cur_proc != NULL); // an address space is mandatory for user page allocation
	pages[i].as = cur_proc;
	pages[i].vpn = vpn;
	pages[i].flags = PFLAG_USED_MASK;
	lock_release(core_map_lock);

	return page_paddr;
}

void
free_page(paddr_t page)
{
	// Make sure its page aligned
	assert((page % PAGE_SIZE) == 0);
	int page_index = (page - free_paddr) / PAGE_SIZE;

	lock_acquire(core_map_lock);
	assert(page_index >= 0 && page_index < num_pages);
	assert(pages != NULL);

	// Now free this page
	pages[page_index].as = NULL;
	pages[page_index].flags = 0;

	lock_release(core_map_lock);
}

/*
 * Below are the set of functions that allow of swapping of page tables in and out
 * of memory.
 */

/*
 * This function goes through the page tables already in memory and finds
 * a victim page table to evict. We always maintain atleast one executable
 * page table in memory.
 */
int get_victim_slot(struct addrspace *as, int is_executable, vaddr_t vpgdir)
{
	int least_count = 0x7fffffff; // initialize to large number
	int least_count_index = 0;
	int i;

	if(is_executable)
	{
		// This is the case where the page table we want to put in
		// memory is for executable page tables
		for(i = 0; i < NUM_PTABLES_IN_MEM; ++i)
		{
			if((as->page_table_flags[i] & PINMEM_FLAG_EXECUTABLE_MASK) &&
					((as->page_table_flags[i] & PINMEM_FLAG_COUNTER_MASK) < least_count))
			{
				least_count = as->page_table_flags[i] & PINMEM_FLAG_COUNTER_MASK;
				least_count_index = i;
			}
		}
		// If there were no executable page tables loaded. The '0'th one will be evicted
	}
	else
	{
		// This is for data segments. We will evict executable page tables
		// only if there are more than one currently loaded (don't think this
		// is ever the case)

		// We will also try to keep one page table for the stack and one
		// for the data together if possible
		int executable_count = 0;
		int second_exec_ptable_index = -1;
		least_count_index = -1;
		for(i = 0; i < NUM_PTABLES_IN_MEM; ++i)
		{
			if(!(as->page_table_flags[i] & PINMEM_FLAG_EXECUTABLE_MASK) &&
								((as->page_table_flags[i] & PINMEM_FLAG_COUNTER_MASK) < least_count))
			{
				least_count = as->page_table_flags[i] & PINMEM_FLAG_COUNTER_MASK;
				least_count_index = i;
			}

			if(as->page_table_flags[i] & PINMEM_FLAG_EXECUTABLE_MASK)
			{
				++executable_count;
				if(executable_count > 1)
				{
					// There are more than 1 executable page tables loaded
					// This page table is a candidate for eviction
					second_exec_ptable_index = i;
				}
			}
			else if((as->page_table_flags[i] & PGDIR_INDEX) == vpgdir)
			{
				// This is asking for a page table in the same directory
				// This probably means they are both stack or data
				least_count_index = i;
				break;
			}
		}

		if(least_count_index == -1)
		{
			// Two many executable page tables were loaded.
			// Evict one of them
			assert(second_exec_ptable_index != -1);
			least_count_index = second_exec_ptable_index;
		}
	}
	return least_count_index;
}

/*
 * This searches the page tables already in memory. If a slot is free then the new page table
 * can be loaded there. Otherwise a slot is freed by swapping a page table out.
 */
int find_slot_for_pg_table(struct addrspace *as, int is_executable, vaddr_t vpgdir)
{
	int i;
	// First search if a slot in our page tables in mem array is free
	for(i = 0; i < NUM_PTABLES_IN_MEM; ++i)
	{
		if(as->page_table_flags[i] == 0)
		{
			return i;
		}
	}

	// Slot is not free. Find a victim to evict
	int victim_index = get_victim_slot(as, is_executable, vpgdir);

	// We have the page table with the least count. Swap him out and return
	// this index
	// The virtual address being passed below is the page directory index converted into kernel virtual address.
	// It has no meaning and its purpose is to serve as a unique key for this swapped page table.
	vaddr_t vpn = as->page_table_flags[victim_index] & PGDIR_INDEX;

	// The swap function needs the actual physical address of the page
	paddr_t page_addr = ((paddr_t)(as->ptables_in_mem[victim_index])) & 0x7fffffff;
	//kprintf("PGDIR:Swapping out page table in directory 0x%x\n", vpn);

	swap_out_page(as, PADDR_TO_KVADDR(vpn), page_addr);
	as->page_table_flags[victim_index] = 0;

	// Calculate the page directory index
	vpn = vpn >> 22;
	// Set this page directory entry to not PGDIR_PRESENT;
	as->pg_dir[vpn].pg_dir_entry &= ~PGDIR_PRESENT;

	return victim_index;
}

/*
 * Given the virtual page number, we return the pointer to the
 * page table. The page table should have been loaded already.
 *
 * I pass the index too because its already calculated before
 */
struct page_table* get_page_table(struct addrspace *as, vaddr_t vpn, int pgdir_index, int is_executable)
{
	if(as->pg_dir[pgdir_index].pg_dir_entry & PGDIR_PRESENT)
	{
		// The page table corresponding to this directory is present in memory
		// Search in our loaded page tables and return the pointer
		int i;
		for(i = 0; i < NUM_PTABLES_IN_MEM; ++i)
		{
			if((as->page_table_flags[i] & PGDIR_INDEX) == (vpn & PGDIR_INDEX))
			{
				// Increment the counter. The counter can overflow and wrap around, but I don't
				// think we will ever actually reach that so I don't care
				as->page_table_flags[i] = (as->page_table_flags[i] & ~PINMEM_FLAG_COUNTER_MASK)
						| ((as->page_table_flags[i] + 1) & PINMEM_FLAG_COUNTER_MASK);
				return as->ptables_in_mem[i];
			}
		}
		panic("PGDIR_PRESENT bit was set but page_table was not found!\n");
	}

	// Page table is not present in memory. Find memory slot for it and swap in from disk
	int empty_slot = find_slot_for_pg_table(as, is_executable, vpn & PGDIR_INDEX);
	// The swap function needs the actual physical address of the page
	paddr_t page_addr = ((paddr_t)(as->ptables_in_mem[empty_slot])) & 0x7fffffff;
	//kprintf("PGDIR:Swapping in page table in directory 0x%x\n", vpn & PGDIR_INDEX);
	swap_in_page(as, PADDR_TO_KVADDR(vpn & PGDIR_INDEX), page_addr);
	as->pg_dir[pgdir_index].pg_dir_entry |= PGDIR_PRESENT;
	as->page_table_flags[empty_slot] = (PGDIR_INDEX & vpn) |
			(is_executable ? PINMEM_FLAG_EXECUTABLE_MASK : 0);
	return as->ptables_in_mem[empty_slot];
}


/*
 * This function may have a similar name to the one above but it is a higher level
 * abstraction over it. This function when given a virtual page directory number
 * (sorry for calling it vpn) will
 * first check if the page table was previously loaded. In that case it will call
 * the similar named 'get_page_table' function to get that page table and return
 * it.
 * If the page table hasn't been loaded already, it will call 'find_slot_for_pg_table'
 * and return that.
 */
struct page_table* get_ptbl(struct addrspace *as, vaddr_t vpn, int is_executable)
{
	int pgdir_index = vpn >> 22;

	if(as == NULL)
	{
		panic("Null addrspace pointer!\n");
	}

	if(as->pg_dir[pgdir_index].pg_dir_entry & PGDIR_LOADED)
	{
		return get_page_table(as, vpn, pgdir_index, is_executable);
	}
	else
	{
		int empty_slot = find_slot_for_pg_table(as, is_executable, vpn & PGDIR_INDEX);
		// This page table is now loaded
		as->pg_dir[pgdir_index].pg_dir_entry |= PGDIR_LOADED | PGDIR_PRESENT;
		as->page_table_flags[empty_slot] = (PGDIR_INDEX & vpn) |
				(is_executable ? PINMEM_FLAG_EXECUTABLE_MASK : 0);
		// zero the page table memory slot
		bzero(as->ptables_in_mem[empty_slot], sizeof(as->ptables_in_mem[empty_slot]));
		return as->ptables_in_mem[empty_slot];
	}
}

/*
 * This function implements on demand loading. It will look at the
 * faulting address and determine if loading of that segment is
 * required and load it if necessary.
 *
 * We need the pointer to the page table entry because for data segments
 * we will update the flags to say it is now loaded. In future data segments
 * will be swapped to the swap file and will not be loaded from the executable
 */
void load_segment_if_required(struct addrspace *as, vaddr_t faultaddress, paddr_t page_paddr,
		int32_t *pg_tbl_entry)
{
	// First find out which region is the executable region
	// in our address space
	vaddr_t exec_vbase, exec_vtop;
	vaddr_t data_vbase, data_vtop;

	if(as->as_flags1 & PF_X)
	{
		exec_vbase = as->as_vbase1;
		exec_vtop = exec_vbase + as->as_npages1 * PAGE_SIZE;

		data_vbase = as->as_vbase2;
		data_vtop = data_vbase + as->as_npages2 * PAGE_SIZE;
	}
	else
	{
		// Otherwise 2 MUST be the executable region
		assert(as->as_flags2 & PF_X);
		exec_vbase = as->as_vbase2;
		exec_vtop = exec_vbase + as->as_npages2 * PAGE_SIZE;

		data_vbase = as->as_vbase1;
		data_vtop = data_vbase + as->as_npages1 * PAGE_SIZE;
	}

	if(faultaddress >= exec_vbase && faultaddress < exec_vtop)
	{
		// This is the executable. Load the corresponding page
		vaddr_t vpn = faultaddress & PAGE_FRAME;
		size_t filesize, memsize;
		size_t num_pages_ahead = (vpn - exec_vbase) / PAGE_SIZE;
		assert((num_pages_ahead * PAGE_SIZE) <= as->executable_filesize);
		assert((num_pages_ahead * PAGE_SIZE) <= as->executable_memsize);
		filesize = as->executable_filesize - num_pages_ahead * PAGE_SIZE;
		memsize = as->executable_memsize - num_pages_ahead * PAGE_SIZE;
		off_t pos = as->executable_offset + num_pages_ahead * PAGE_SIZE;

		if(filesize > PAGE_SIZE)
		{
			filesize = PAGE_SIZE;
		}
		if(memsize > PAGE_SIZE)
		{
			memsize = PAGE_SIZE;
		}

		load_page_from_executable(as->exec_path, pos, vpn, page_paddr, memsize, filesize);
		DEBUG(DB_EXEC, "Loaded an executable page at vaddr:0x%x, paddr:0x%x on demand\n", vpn, page_paddr);
	}
	else if(faultaddress >= data_vbase && faultaddress < data_vtop && !((*pg_tbl_entry) & PF_L))
	{
		// This a data page. Load the corresponding page from executable
		vaddr_t vpn = faultaddress & PAGE_FRAME;
		size_t filesize, memsize;
		size_t num_pages_ahead = (vpn - data_vbase) / PAGE_SIZE;
		assert((num_pages_ahead * PAGE_SIZE) <= as->data_memsize);
		if(num_pages_ahead * PAGE_SIZE  > as->data_filesize)
		{
			filesize = 0;
		}
		else
		{
			filesize = as->data_filesize - num_pages_ahead * PAGE_SIZE;
		}
		memsize = as->data_memsize - num_pages_ahead * PAGE_SIZE;
		off_t pos = as->data_offset + num_pages_ahead * PAGE_SIZE;

		if(filesize > PAGE_SIZE)
		{
			filesize = PAGE_SIZE;
		}
		if(memsize > PAGE_SIZE)
		{
			memsize = PAGE_SIZE;
		}

		load_page_from_executable(as->exec_path, pos, vpn, page_paddr, memsize, filesize);
		(*pg_tbl_entry) |= PF_L; // data page now loaded
		DEBUG(DB_EXEC, "Loaded a data page at vaddr:0x%x, paddr:0x%x on demand\n", vpn, page_paddr);
	}
	else if(faultaddress >= as->as_heap_vstart && faultaddress < as->as_heap_vtop && !((*pg_tbl_entry) & PF_L))
	{
		// This heap segment is vaid.
		(*pg_tbl_entry) |= PF_L;
	}
	else if(faultaddress >= as->as_stack_vbase && faultaddress < USERSTACK && !((*pg_tbl_entry) & PF_L))
	{
		// This stack segment is now loaded.
		(*pg_tbl_entry) |= PF_L;
	}
	else
	{
		// So this is not an executable. It is either a data, heap or stack which was loaded before
		// and has been swapped to disk. Swap it back in
//		kprintf("Swapping in 0x%x for addrspace 0x%x\n", faultaddress, as);
		assert(lock_do_i_hold(core_map_lock));
		swap_in_page(as, faultaddress, page_paddr);
	}

	return;

}

/*
 * Function to find the correct pte given vpn using a two level paging
 * Returns the pointer to the page table entry
 * (Note not the pointer to the page in the pte)
 * We allocate new pages only when alloc_new_pages = 1
 */
struct page_table*
find_pte(struct addrspace *cur_as, vaddr_t vpn, int32_t flags)
{
	assert(cur_as != NULL);
	vaddr_t executable_vbase, executable_vtop;
	if(cur_as->as_flags1 & PF_X)
	{
		executable_vbase = cur_as->as_vbase1;
		executable_vtop = cur_as->as_vbase1 + (cur_as->as_npages1 * PAGE_SIZE);
	}
	else
	{
		executable_vbase = cur_as->as_vbase2;
		executable_vtop = cur_as->as_vbase2 + (cur_as->as_npages2 * PAGE_SIZE);;
	}
	int is_executable = (vpn >= executable_vbase && vpn < executable_vtop);

	lock_acquire(core_map_lock);
	struct page_table *pg_tbl = get_ptbl(cur_as, vpn, is_executable);
	lock_release(core_map_lock);
	int pgtbl_index = (vpn & PGTBL_INDEX) >> 12;

	if(pg_tbl[pgtbl_index].pg_tbl_entry & PGTBL_VALID_MASK)
	{
		return (&pg_tbl[pgtbl_index]);
	}
	else
	{
		/*
		 * Allocate a new page, read the page from disk and store the paddr
		 * in the corresponding pg_tbl_entry. We update the coremap
		 * in alloc_page, so dont have to do it here.
		 */
		paddr_t page_paddr = alloc_page(cur_as, vpn);

		// Lock to prevent synchronization issues of our page table with the
		// code in make_pg_available
		lock_acquire(core_map_lock);
		// If required, demand load the page
		load_segment_if_required(cur_as, vpn, page_paddr, &(pg_tbl[pgtbl_index].pg_tbl_entry));

		// First clear the physical address currently stored
		pg_tbl[pgtbl_index].pg_tbl_entry &= ~PAGE_FRAME;
		pg_tbl[pgtbl_index].pg_tbl_entry |= (page_paddr & PAGE_FRAME) | flags | PGTBL_VALID_MASK;
		lock_release(core_map_lock);
		return (&pg_tbl[pgtbl_index]);
	}
}

int
find_tlb_index()
{
	panic("I am not implemented yet! %s in %s:%d\n", __PRETTY_FUNCTION__, __FILE__, __LINE__);
	return 0;
}

const char* vm_fault_type_str(int faulttype)
{
	switch(faulttype)
	{
	case VM_FAULT_READONLY: return "VM_FAULT_READONLY";
	case VM_FAULT_READ: return "VM_FAULT_READ";
	case VM_FAULT_WRITE: return "VM_FAULT_WRITE";
	default: return "DA_FAWK?";
	}
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	int32_t flags;
	int i;
	u_int32_t ehi, elo, elo_fault;
	struct addrspace *as;
	struct page_table *pte;
	int spl;

	spl = splhigh();

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "vm_fault faultaddress: 0x%x, faulttype: %s, curthread: 0x%x, as: 0x%x\n",
			faultaddress, vm_fault_type_str(faulttype), (vaddr_t)curthread, (vaddr_t)curthread->t_vmspace);

	as = curthread->t_vmspace;
	if (as == NULL) {
		/*
		 * No address space set up. This is probably a kernel
		 * fault early in boot. Return EFAULT so as to panic
		 * instead of getting into an infinite faulting loop.
		 */
		splx(spl);
		return EFAULT;
	}

	/*
	 * Setup the flags by checking where the faultaddress lies.
	 * Can then use the flags1 or flags2 in addrspace. Will have to change
	 * this when we add heap. Using constant pages for stack.
	 */
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = as->as_stack_vbase;
	stacktop = USERSTACK;

	// We will allow a max growth of 'DUMBVM_STACKPAGES' for the stack
	vaddr_t max_stack_growth_base = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		flags = as->as_flags1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		flags = as->as_flags2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		flags = PF_R | PF_W;
	}
	else if(faultaddress >= (stackbase - PAGE_SIZE) && faultaddress >= as->as_heap_vtop &&
			faultaddress >= max_stack_growth_base)
	{
		// User wants to grow the stack and it does not collide with the heap.
		// Currently allow stack to grow only 1 page at a time. Might have to
		// change this if any of the test programs require a larger stack
		// growth
		// Note: It is '>=' the heap top because the heap addresses are
		// actually '<' heap_top
		as->as_stack_vbase -= PAGE_SIZE;
		flags = PF_R | PF_W;
	}
	else if(faultaddress >= as->as_heap_vstart && faultaddress < as->as_heap_vtop)
	{
		// User heap
		flags = PF_R | PF_W;
	}
	else {
		splx(spl);
		// Segmentation Fault
		return VM_FAULT_USER;
	}

	switch (faulttype) {
	    case VM_FAULT_READONLY:
	    	/*
	    	 * Call the find_pte function. It returns the pte, check
	    	 * whether the page is writable using that. If not, then
	    	 * kill the process.
	    	 */
	    	pte = find_pte(as, faultaddress, flags);
	    	if(pte->pg_tbl_entry & PF_W)
	    	{
	    		elo_fault = (pte->pg_tbl_entry & PAGE_FRAME) | TLBLO_DIRTY | TLBLO_VALID;
	    	}
	    	else
	    	{
	    		// Writing to read only segment fault
	    		return VM_FAULT_USER;
	    	}
	    	break;

	    case VM_FAULT_READ:
	    	/* Call the find_pte function store paddr by reading the upper 20 bits pg_tbl_entry */
	    	pte = find_pte(as, faultaddress, flags);
	    	elo_fault = (pte->pg_tbl_entry & PAGE_FRAME) | TLBLO_VALID;
	    	// If this page is writeable, we don't want to make it read only
	    	if(pte->pg_tbl_entry & PF_W)
	    	{
	    		elo_fault |= TLBLO_DIRTY;
	    	}
	    	break;
	    case VM_FAULT_WRITE:
	    	pte = find_pte(as, faultaddress, flags);
	    	elo_fault = (pte->pg_tbl_entry & PAGE_FRAME) | TLBLO_DIRTY | TLBLO_VALID;
		break;
	    default:
		splx(spl);
		panic("VM Fault called with invalid value for faulttype!\n");
		return EINVAL;
	}

	/*
	 * Populate the TLB entry using TLB_Random
	 */
	/* Check if this entry is already present in the TLB */
	i = TLB_Probe(faultaddress, 0);

	if(i < 0)
	{
		/* Check if there are any free entries in the TLB */
		for (i=0; i<NUM_TLB; i++)
		{
			TLB_Read(&ehi, &elo, i);
			// If our faultaddress is already in the TLB, lets use that spot
			if (elo & TLBLO_VALID) {
				continue;
			}
			ehi = faultaddress;
			DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, elo_fault & PAGE_FRAME);
			TLB_Write(ehi, elo_fault, i);
			splx(spl);
			return VM_FAULT_OK;
		}

		/* If not then we are randomly going to choose one */
		ehi = faultaddress;
		TLB_Random(ehi, elo_fault);
	}
	else
	{
		ehi = faultaddress;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, elo_fault & PAGE_FRAME);
		TLB_Write(ehi, elo_fault, i);
	}

	splx(spl);
	return VM_FAULT_OK;



	#if 0
	/* Assert that the address space has been set up properly. */
	assert(as->as_vbase1 != 0);
	assert(as->as_pbase1 != 0);
	assert(as->as_npages1 != 0);
	assert(as->as_vbase2 != 0);
	assert(as->as_pbase2 != 0);
	assert(as->as_npages2 != 0);
	assert(as->as_stackpbase != 0);
	assert((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	assert((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	assert((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	assert((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	assert((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		splx(spl);
		return EFAULT;
	}

	/* make sure it's page-aligned */
	assert((paddr & PAGE_FRAME)==paddr);

	for (i=0; i<NUM_TLB; i++) {
		TLB_Read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		TLB_Write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	as->as_flags1 = 0;
	as->as_flags2 = 0;
	as->pg_dir = 0;
	as->exec_path[0] = '\0';
	as->executable_offset = 0;
	as->executable_memsize = 0;
	as->executable_filesize = 0;
	as->data_offset = 0;
	as->data_filesize = 0;
	as->data_memsize = 0;

	/*
	 * Need to create a new page for the page directory
	 * Creating the pg_dir page in kernel memory - bcoz
	 * there's only one per process
	 */

	as->pg_dir = kmalloc(1024 * sizeof(struct page_directory));
	if(as->pg_dir == NULL)
	{
		kfree(as);
		return NULL;
	}
	bzero(as->pg_dir, 1024 * sizeof(struct page_directory));
	bzero(as->page_table_flags, sizeof(as->page_table_flags));
	int i;
	assert(sizeof(as->ptables_in_mem[0]) == PAGE_SIZE);
	for(i = 0; i < NUM_PTABLES_IN_MEM; ++i)
	{
		bzero(as->ptables_in_mem[i], 1024);
	}
	return as;
}

// Copy an individual page table and allocate new pages
void copy_individal_page_table(struct addrspace *as_old, struct addrspace *as_new,
		struct page_table *ptbl_old, struct page_table *ptbl_new, vaddr_t vpgdir)
{
	// Go through each entry in the page table, copy the info
	// and do necessary page allocations
	int i;
	vaddr_t vpn;
	paddr_t page_addr;
	for(i = 0; i < 1024; ++i)
	{
		vpn = vpgdir | ((vaddr_t)i << 12);
		// This page is loaded and is valid
		if((ptbl_old[i].pg_tbl_entry & PF_L) && (ptbl_old[i].pg_tbl_entry & PGTBL_VALID_MASK))
		{
			page_addr = alloc_page(as_new, vpn);
			// When we allocated a page for us, the old_process' page might have gotten swapped out
			lock_acquire(core_map_lock);
			if(ptbl_old[i].pg_tbl_entry & PGTBL_VALID_MASK)
			{
				// His page is still there. Copy directly
				memcpy((void*)PADDR_TO_KVADDR(page_addr),
						(void*)PADDR_TO_KVADDR(ptbl_old[i].pg_tbl_entry & PAGE_FRAME), PAGE_SIZE);
			}
			else
			{
				// His page was swapped out. Copy in the data from the swap file
				// but don't remove his data from the swap file
				swap_copy_in_page(as_old, vpn, page_addr);
			}
			ptbl_new[i].pg_tbl_entry = page_addr | (ptbl_old[i].pg_tbl_entry & ~PAGE_FRAME) | PGTBL_VALID_MASK;
			lock_release(core_map_lock);
		}
		else if(ptbl_old[i].pg_tbl_entry & PF_L)
		{
			// His page was swapped out. Copy in the data from the swap file
			// but don't remove his data from the swap file
			page_addr = alloc_page(as_new, vpn);
			lock_acquire(core_map_lock);
			if(ptbl_old[i].pg_tbl_entry & PGTBL_VALID_MASK)
			{
				// His page is still there. Copy directly
				memcpy((void*)PADDR_TO_KVADDR(page_addr),
						(void*)PADDR_TO_KVADDR(ptbl_old[i].pg_tbl_entry & PAGE_FRAME), PAGE_SIZE);
			}
			else
			{
				// His page was swapped out. Copy in the data from the swap file
				// but don't remove his data from the swap file
				swap_copy_in_page(as_old, vpn, page_addr);
			}
			ptbl_new[i].pg_tbl_entry = page_addr | (ptbl_old[i].pg_tbl_entry & ~PAGE_FRAME)  | PGTBL_VALID_MASK;
			lock_release(core_map_lock);
		}
	}
}

// Copy the page tables. This includes copying stuff in the page directories and all page tables
void copy_all_page_tables(struct addrspace *as_old, struct addrspace *as_new)
{
	// First find out which region is the executable region
	// in our address space
	vaddr_t exec_vbase, exec_vtop;
	if(as_new->as_flags1 & PF_X)
	{
		exec_vbase = as_new->as_vbase1;
		exec_vtop = exec_vbase + as_new->as_npages1 * PAGE_SIZE;
	}
	else
	{
		// Otherwise 2 MUST be the executable region
		assert(as_new->as_flags2 & PF_X);
		exec_vbase = as_new->as_vbase2;
		exec_vtop = exec_vbase + as_new->as_npages2 * PAGE_SIZE;
	}

	int i;
	struct page_directory *pgdir_old = as_old->pg_dir;
	struct page_directory *pgdir_new = as_new->pg_dir;
	// Go through each directory, copy the directory entries and page tables
	// if required
	for(i = 0; i < 1024; ++i)
	{
		if(pgdir_old[i].pg_dir_entry & PGDIR_LOADED)
		{
			// This directory is loaded. Get the page
			// table and copy it
			lock_acquire(core_map_lock);
			struct page_table *ptbl_old = get_page_table(as_old, (vaddr_t)i << 22, i, 0);
			int empty_slot = find_slot_for_pg_table(as_new, 0, (vaddr_t)i << 22);
			lock_release(core_map_lock);
			copy_individal_page_table(as_old, as_new, ptbl_old,
					as_new->ptables_in_mem[empty_slot], (vaddr_t)i << 22);

			as_new->page_table_flags[empty_slot] = ((vaddr_t)i << 22);
			if(((i << 22) & PGDIR_INDEX) == (exec_vbase & PGDIR_INDEX))
			{
				as_new->page_table_flags[empty_slot] |= PINMEM_FLAG_EXECUTABLE_MASK;
			}
			pgdir_new[i].pg_dir_entry = PGDIR_PRESENT | PGDIR_LOADED;
		}
	}
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
	new->as_flags1 = old->as_flags1;
	new->as_flags2 = old->as_flags2;
	new->as_heap_vstart = old->as_heap_vstart;
	new->as_heap_vtop = old->as_heap_vtop;
	new->as_stack_vbase = old->as_stack_vbase;
	new->data_filesize = old->data_filesize;
	new->data_memsize = old->data_memsize;
	new->data_offset = old->data_offset;
	new->executable_filesize = old->executable_filesize;
	new->executable_memsize = old->executable_memsize;
	new->executable_offset = old->executable_offset;
	strcpy(new->exec_path, old->exec_path);
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}


	/*
	 * We now need to walk through the page table and allocate
	 * and copy pages for the new addrspace. We also need to
	 * update the entries of the new addrspace page table.
	 */
	/*
	 * Doesn't do copy on write yet. Unfortunately
	 * forktest won't pass without that I think. This
	 * is because it spawns too many processes which
	 * have shared code pages.
	 */
	int spl = splhigh();
	copy_all_page_tables(old, new);
	splx(spl);


	*ret = new;
	return 0;
}


#if 0
void free_page_tables_in_dir(struct page_table *ptbl, int *allocated_pages)
{
	int i;
	for(i = 0; i < 1024; ++i)
	{
		paddr_t page = ptbl[i].pg_tbl_entry & PAGE_FRAME;
		if(page != 0 && (ptbl[i].pg_tbl_entry & PGTBL_VALID_MASK))
		{
			free_page(page);
			(*allocated_pages)--;
		}
		else if(page != 0 && (ptbl[i].pg_tbl_entry & PF_L))
		{
			// Swapped page.
			(*allocated_pages)--;
		}
	}
}
#endif

void
as_destroy(struct addrspace *as)
{
	/*
	 * Walk through the page table. Invalidate the entries.
	 * Free the pages. Update the coremap.
	 */

	// Walk through the core map and free all
	// pages with this address space
	int i;
	lock_acquire(core_map_lock);
	for(i = 0; i < num_pages; ++i)
	{
		if(pages[i].as == as)
		{
			pages[i].as = NULL;
			pages[i].flags = 0;
		}
	}
	// Free all our swapped pages
	swap_free_pages(as);
	lock_release(core_map_lock);
	kfree(as->pg_dir);
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	int i, spl;

	(void)as;

	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		/* Setting the flags for the segment which can then be used by vm_fault*/
		as->as_flags1 = readable | writeable | executable;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		/* Setting the flags for the segment which can then be used by vm_fault*/
		as->as_flags2 = readable | writeable | executable;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Wont load pages in the start. vm_fault should be able to
	 * load pages as they are required.
	 */
	(void)as;

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	DEBUG(DB_EXEC, "Region 1 0x%x to 0x%x\n", as->as_vbase1, as->as_vbase1 + as->as_npages1 * PAGE_SIZE);
	DEBUG(DB_EXEC, "Region 2 0x%x to 0x%x\n", as->as_vbase2, as->as_vbase2 + as->as_npages2 * PAGE_SIZE);

	// Define the heap and stack regions
	vaddr_t region1_top, region2_top;
	region1_top = as->as_vbase1 + as->as_npages1 * PAGE_SIZE;
	region2_top = as->as_vbase2 + as->as_npages2 * PAGE_SIZE;
	as->as_heap_vstart = region1_top > region2_top ? region1_top : region2_top;
	as->as_heap_vtop = as->as_heap_vstart;

	DEBUG(DB_EXEC, "No user heap allocated yet: 0x%x to 0x%x\n", as->as_heap_vstart, as->as_heap_vtop);
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	as->as_stack_vbase = USERSTACK - PAGE_SIZE;
	DEBUG(DB_EXEC, "Starting off with only one page for user stack: 0x%x to 0x%x\n", USERSTACK, as->as_stack_vbase);

	if(as->as_stack_vbase < as->as_heap_vtop)
	{
		// No memory for even one page of stack
		return ENOMEM;
	}

	return 0;
}


