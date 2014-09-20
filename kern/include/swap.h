#include <types.h>

struct addrspace;
struct uio;
struct vnode;

#define SWAP_FILE_NAME "lhd0raw:"


/*
 * Bring a page in from the disk to physical memory
 */
void swap_in_page(struct addrspace *as, vaddr_t vpn, paddr_t free_page);

void swap_copy_in_page(struct addrspace *as, vaddr_t vpn, paddr_t free_page);

/*
 * Free the pages on disk corresponding to this address space
 */
void swap_free_pages(struct addrspace *as);

/*
 * Swap a page from physical memory to disk
 */

void swap_out_page(struct addrspace *as, vaddr_t vpn, paddr_t page_addr);

void reclaim_all_swap_sections();

#if 0 // This function is deprecated
/*
 * Swap the data from the file point to by 'u' to our swap file
 */
void swap_segment_helper(struct vnode *v, struct uio *u, struct addrspace *as, vaddr_t vpn);

#endif


void swap_bootstrap();

void swap_cleanup();
