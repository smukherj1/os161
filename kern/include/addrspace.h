#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <vm.h>
#include "opt-dumbvm.h"

struct vnode;

/* 
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */

#define NUM_PTABLES_IN_MEM 3
#define MAX_EXEC_PATH_SIZE 30
struct addrspace {

	vaddr_t as_vbase1;
	size_t as_npages1;
	int32_t as_flags1;

	vaddr_t as_vbase2;
	size_t as_npages2;
	int32_t as_flags2;

	off_t executable_offset;
	size_t executable_memsize;
	size_t executable_filesize;

	off_t data_offset;
	size_t data_memsize;
	size_t data_filesize;

	// Since stack grows downwards (lower stack address means larger stack)
	// the base is actually the limit of the stack. This base must always be
	// greater than 'as_heap_vtop'. If they collide, it means we have overflow
	// in whichever region requested to grow
	vaddr_t as_stack_vbase;

	vaddr_t as_heap_vstart;
	vaddr_t as_heap_vtop;

	/* Path of our executable program */
	char exec_path[MAX_EXEC_PATH_SIZE];

	/* Page table directory for each process*/
	struct page_directory *pg_dir;

	/* Page tables we will actually keep in memory */
	struct page_table ptables_in_mem[NUM_PTABLES_IN_MEM][1024];

	/*
	 * Flags containing info about the page tables
	 * we keep in memory. Is zero when slot if free.
	 *
	 * Upper 10 bits is the virtual page
	 * directory number for this page table.
	 * The next bit is indicates whether this page table
	 * is for an executable directory.
	 * The next 21 bits
	 * is a counter that says how many times there was a fault
	 * requiring this page. The one with the least count is
	 * a candidate for eviction.
	 */
	int32_t page_table_flags[NUM_PTABLES_IN_MEM];
};

/*
 * Masks for the page_table_flags above
 */
#define PINMEM_FLAG_EXECUTABLE_MASK 0x00200000
#define PINMEM_FLAG_COUNTER_MASK	0x001fffff

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make 
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make the specified address space the one currently
 *                "seen" by the processor. Argument might be NULL, 
 *		  meaning "no particular address space".
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(struct addrspace *);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as, 
				   vaddr_t vaddr, size_t sz,
				   int readable, 
				   int writeable,
				   int executable);
int		  as_prepare_load(struct addrspace *as);
int		  as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);

/*
 * Load a page from the executable file. This is for on demand loading.
 */
int load_page_from_executable(char *exec_path, off_t offset, vaddr_t vaddr, paddr_t paddr,
	     size_t memsize, size_t filesize);


#endif /* _ADDRSPACE_H_ */
