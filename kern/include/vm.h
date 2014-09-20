#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */

struct addrspace;

struct page
{
	// Page mapping
	struct addrspace *as;
	vaddr_t vpn; // virtual page number

	/*
	 * Note we don't need a physical page frame field because it can be implicitly
	 * calculated from the index of the page entry we are using.
	 */

	// Will have bits for dirty, valid, used
	int32_t flags;
};

/*
 * Structs required for the two level page mapping
 */
struct page_directory
{
	/*
	 * Upper 20 bits - Page Table - Page number where the page table is stored
	 * 0th bit - PTE_P bit - Page Table present?
	 * 1st bit - Page directory loaded (if loaded 1 and present 0 it means this page table was
	 * swapped to disk)
	 */
	int32_t pg_dir_entry;
};

struct page_table
{

	/*
	 * PTE = {20b PFN, 5b0, 1bL, 1bM, 1bR, 1bV, 1bRe, 1bWr, 1bX}
	 * Upper 20 bits - Page Frame Number
	 * M - Modify
	 * R - Reference bit
	 * V - Valid
	 * Re - Readable
	 * Wr - Writable
	 * X - Executable
	 */
	int32_t pg_tbl_entry;
};

/* Masks for Page directory and Page Table */
#define PGDIR_INDEX			0xffc00000
#define PGDIR_PRESENT		0x00000001
#define PGDIR_LOADED		0x00000002

#define PGTBL_INDEX			0x003ff000
#define PGTBL_VALID_MASK	0x00000008

#define PFLAG_USED_MASK 	0x80000000 // Mask in flag field of a page to indicate if page is in use
#define PFLAG_NUM_CONTG_PAGES	0x00000007f // Keeps count of how many contiguous pages from this page was allocated by alloc_kpages

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

/* Maximum User Heap Size */
#define USER_HEAP_MAX		1048576

/*
 * Return code for vm_fault()
 */
#define VM_FAULT_OK		0	/* Aal iz well. We handled le fault */
#define VM_FAULT_USER	1	/* Fatal fault by user. Kill the user process */

/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/*
 * Function called by sys_waitpid when it needs to sleep.
 * This function will evict all its pages if the coremap
 * is full
 */
void evict_all_my_pages_if_necessary(struct addrspace *as);

// Called by kernel when finished executing user program. This is to prevent
// any leaks
void reclaim_all_user_pages();

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

#endif /* _VM_H_ */
