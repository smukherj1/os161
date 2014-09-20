#include <swap.h>
#include <types.h>
#include <lib.h>
#include <uio.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <synch.h>
#include <machine/spl.h>

struct SwapMap
{
	// The addrspace who is using this Swap section
	struct addrspace *as;

	// The first 20 bits is the virtual page number of the
	// page swapped to this portion.
	// The bit 0 indicates if this portion is used or not
	// (if set to 1 means in use, if set to 0 means free)
	// The mask for this bit is the SWMAP_FLAG_USED defined
	// below
	u_int32_t flags;
};


#define SWAP_MAP_SIZE 1280
struct SwapMap swap_map[SWAP_MAP_SIZE];

#define SWMAP_FLAG_USED		0x00000001 // Mask for used bit in flag above
#define SWAP_GLOBAL_OFFSET	0	// A global offset if our file has one

struct SwapEntryInfo
{
	struct addrspace *as;        // The address space to which this swapped page belongs to
	vaddr_t vpn;                 // The virtual Page number of this page
	u_int32_t magic_flag;        // This flag will always have the same magic value to make
	                             // sure we have read the SwapEntryInfo correctly
	int used;                    // If this swap section is currently in use
};

struct lock *swap_lock = NULL;

#define SWAP_MAGIC_FLAG 0xabcdabcd

typedef enum
{
	SWAP_MOVE_IN,
	SWAP_COPY_IN,
} swap_option;

/*
 * Below are the functions that implement the new version of swapping using
 * the swap map.
 */

/* Return the next free section in the swap file.
 * Panic if we can't find one
 */
off_t find_free_swap_section(int *free_index)
{
	int i;
	for(i = 0; i < SWAP_MAP_SIZE; ++i)
	{
		if(!(swap_map[i].flags & SWMAP_FLAG_USED))
		{
			*free_index = i;
			return ((i * PAGE_SIZE) + SWAP_GLOBAL_OFFSET);
		}
	}

	panic("Out of swap space!\n");
	return 0;
}

/*
 * Return the position of a particular page given its vpn
 * and addrspace. Panic if we can't find it
 */

off_t find_swapped_page_location(struct addrspace *as, vaddr_t vpn,
		swap_option opt)
{
	int i;
	for(i = 0; i < SWAP_MAP_SIZE; ++i)
	{
		if((swap_map[i].flags & SWMAP_FLAG_USED) && (swap_map[i].as == as)
				&& ((swap_map[i].flags & PAGE_FRAME) == vpn))
		{
			if(opt == SWAP_MOVE_IN)
			{
				swap_map[i].flags = 0;
			}
			return ((i * PAGE_SIZE) + SWAP_GLOBAL_OFFSET);
		}
	}
	switch(opt)
	{
	case SWAP_COPY_IN: kprintf("SWAP_COPY_IN\n"); break;
	case SWAP_MOVE_IN: kprintf("SWAP_MOVE_IN\n");break;
	default: kprintf("Invalid option\n"); break;
	}
	panic("Could not find swapped page for addrspace: 0x%x, vpn: 0x%x\n", (vaddr_t)as, vpn);
	return 0;
}

/*
 * Bring a page in from the disk to physical memory
 */
void swap_in_page(struct addrspace *as, vaddr_t vpn, paddr_t free_page)
{
	struct uio ku;

	struct vnode *v;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);

	lock_acquire(swap_lock);
//	kprintf("SWP: Swap move in as:0x%x, vpn:0x%x\n", as, vpn);
	off_t pos = find_swapped_page_location(as, vpn, SWAP_MOVE_IN);
	mk_kuio(&ku, (void*)PADDR_TO_KVADDR(free_page), PAGE_SIZE, pos, UIO_READ);
	assert(vfs_open(swapfilename, O_RDONLY, &v) == 0);
	assert(VOP_READ(v, &ku) == 0);
	assert(ku.uio_resid == 0);
	vfs_close(v);
	lock_release(swap_lock);

}

void swap_copy_in_page(struct addrspace *as, vaddr_t vpn, paddr_t free_page)
{
	struct uio ku;

	struct vnode *v;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);

	lock_acquire(swap_lock);
//	kprintf("SWP: Swap copy in as:0x%x, vpn:0x%x\n", as, vpn);
	off_t pos = find_swapped_page_location(as, vpn, SWAP_COPY_IN);
	mk_kuio(&ku, (void*)PADDR_TO_KVADDR(free_page), PAGE_SIZE, pos, UIO_READ);
	assert(vfs_open(swapfilename, O_RDONLY, &v) == 0);
	assert(VOP_READ(v, &ku) == 0);
	assert(ku.uio_resid == 0);
	vfs_close(v);
	lock_release(swap_lock);

}

/*
 * Free the pages on disk corresponding to this address space
 */
void swap_free_pages(struct addrspace *as)
{
	int i;
	lock_acquire(swap_lock);
//	kprintf("SWP: Swap free all as:0x%x\n", as);
	for(i = 0; i < SWAP_MAP_SIZE; ++i)
	{
		if(swap_map[i].as == as)
		{
			swap_map[i].flags = 0;
		}
	}
	lock_release(swap_lock);
}

/*
 * Swap a page from physical memory to disk
 */

void swap_out_page(struct addrspace *as, vaddr_t vpn, paddr_t page_addr)
{
	struct uio ku;
	struct vnode *v;
	int free_index;
	off_t pos;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);

	lock_acquire(swap_lock);
//	kprintf("SWP: Swapping out as:0x%x, vpn:0x%x\n", as, vpn);
	pos = find_free_swap_section(&free_index);
	swap_map[free_index].as = as;
	swap_map[free_index].flags = (vpn & PAGE_FRAME) | SWMAP_FLAG_USED;
	mk_kuio(&ku, (void*)PADDR_TO_KVADDR(page_addr), PAGE_SIZE, pos, UIO_WRITE);
	assert(vfs_open(swapfilename, O_RDWR, &v) == 0);
	assert(VOP_WRITE(v, &ku) == 0);
	assert(ku.uio_resid == 0);
	vfs_close(v);
	lock_release(swap_lock);
}

void reclaim_all_swap_sections()
{
	int spl = splhigh();
	int i;
	for(i = 0; i < SWAP_MAP_SIZE; ++i)
	{
		swap_map[i].flags = 0;
	}
	splx(spl);
}



#if 0
void find_and_load_page_from_disk(struct vnode *v, struct addrspace *as, vaddr_t vpn, paddr_t free_page,
		swap_option opt)
{
	struct uio ku;
	struct SwapEntryInfo sei;
	off_t pos = 0;


	mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
	assert(VOP_READ(v, &ku) == 0);
	assert(sei.magic_flag == SWAP_MAGIC_FLAG);

	while(ku.uio_resid == 0 && (sei.as != as || sei.vpn != vpn || sei.used != 1))
	{
		// Skip the page
		pos += sizeof(sei) + PAGE_SIZE;
		mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
		assert(VOP_READ(v, &ku) == 0);
		assert(sei.magic_flag == SWAP_MAGIC_FLAG);
	}
	if(sei.as == as && sei.vpn == vpn && sei.used == 1)
	{
		if(opt == SWAP_MOVE_IN)
		{
			// Update this section as now being free
			sei.used = 0;
			mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_WRITE);
			assert(VOP_WRITE(v, &ku) == 0);
			assert(ku.uio_resid == 0);
		}
		if(opt == SWAP_MOVE_IN || opt == SWAP_COPY_IN)
		{
			pos += sizeof(sei);
			mk_kuio(&ku, (void*)PADDR_TO_KVADDR(free_page), PAGE_SIZE, pos, UIO_READ);
			assert(VOP_READ(v, &ku) == 0);
			// If this assertion below goes off, it means swap file is corrupted
			assert(ku.uio_resid == 0);
		}
	}
	else if(ku.uio_resid == sizeof(sei) || sei.used == 0)
	{
		panic("I can't find this page on disk!\n");
	}
	else
	{
		panic("Swap file corrupted!\n");
	}

	return;
}

/*
 * Bring a page in from the disk to physical memory
 */
void swap_in_page(struct addrspace *as, vaddr_t vpn, paddr_t free_page)
{
	struct vnode *v;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);
	int result = vfs_open(swapfilename, O_RDONLY, &v);


	if(result)
	{
		panic("Could not open file '%s' to swap in a page from", swapfilename);
	}
//	kprintf("SWAP: Swapping in 0x%x\n", vpn);
	lock_acquire(swap_lock);
	find_and_load_page_from_disk(v, as, vpn, free_page, SWAP_MOVE_IN);
	lock_release(swap_lock);
	vfs_close(v);

	return;
}

/*
 * Copy a page in from the disk to physical memory
 */
void swap_copy_in_page(struct addrspace *as, vaddr_t vpn, paddr_t free_page)
{
	struct vnode *v;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);
	int result = vfs_open(swapfilename, O_RDONLY, &v);


	if(result)
	{
		panic("Could not open file '%s' to swap in a page from", swapfilename);
	}
//	kprintf("SWAP: Swapping in 0x%x\n", vpn);
	lock_acquire(swap_lock);
	find_and_load_page_from_disk(v, as, vpn, free_page, SWAP_COPY_IN);
	lock_release(swap_lock);
	vfs_close(v);

	return;
}

void find_and_free_pages_on_disk(struct vnode *v, struct addrspace *as)
{
	struct uio ku;
	struct SwapEntryInfo sei;
	off_t pos = 0;


	mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
	assert(VOP_READ(v, &ku) == 0);

	while(ku.uio_resid == 0)
	{
		assert(sei.magic_flag == SWAP_MAGIC_FLAG);
		if(sei.as == as && sei.used == 1)
		{
			// Update this section as now being free
			sei.used = 0;
			mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_WRITE);
			assert(VOP_WRITE(v, &ku) == 0);
			assert(ku.uio_resid == 0);
		}
		// Skip the page
		pos += sizeof(sei) + PAGE_SIZE;
		mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
		assert(VOP_READ(v, &ku) == 0);
	}
	return;
}


/*
 * Free the section
 */
void swap_free_pages(struct addrspace *as)
{
	struct vnode *v;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);
	int result = vfs_open(swapfilename, O_RDONLY, &v);


	if(result)
	{
		panic("Could not open file '%s' to swap in a page from", swapfilename);
	}

	lock_acquire(swap_lock);
	find_and_free_pages_on_disk(v, as);
	lock_release(swap_lock);
	vfs_close(v);

	return;
}

void swap_page_to_empty_disk_section(struct vnode *v, struct addrspace *as, vaddr_t vpn, paddr_t page_addr, off_t pos)
{
	struct uio ku;
	struct SwapEntryInfo sei;

	// Setup the data structures for write
	sei.as = as;
	sei.vpn = vpn;
	sei.magic_flag = SWAP_MAGIC_FLAG;
	sei.used = 1;
	mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_WRITE);
	assert(VOP_WRITE(v, &ku) == 0);
	assert(ku.uio_resid == 0);
	mk_kuio(&ku, (void*)PADDR_TO_KVADDR(page_addr), PAGE_SIZE, pos + sizeof(sei), UIO_WRITE);
	assert(VOP_WRITE(v, &ku) == 0);
	assert(ku.uio_resid == 0);
}


void find_and_swap_page_to_disk(struct vnode *v, struct addrspace *as, vaddr_t vpn, paddr_t page_addr)
{
	struct uio ku;
	struct SwapEntryInfo sei;
	off_t pos = 0;

	mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
	assert(VOP_READ(v, &ku) == 0);

	// File empty or the first section is available
	if(ku.uio_resid == sizeof(sei) || sei.used == 0)
	{
		swap_page_to_empty_disk_section(v, as, vpn, page_addr, pos);
		return;
	}
	else if(ku.uio_resid != 0)
	{
		panic("Swap file corrupted!\n");
	}
	else if(sei.as == as && sei.vpn == vpn && sei.used == 1)
	{
		kprintf("vpn: 0x%x, as: 0x%x\n", vpn, (vaddr_t)as);
		panic("This page is already swapped to disk!\n");
	}

	while(ku.uio_resid == 0 && (sei.as != as || sei.vpn != vpn) && sei.used == 1)
	{
		assert(sei.magic_flag == SWAP_MAGIC_FLAG);
		// Skip the swapped page
		pos += sizeof(sei) + PAGE_SIZE;
		mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
		assert(VOP_READ(v, &ku) == 0);
		assert(ku.uio_resid == 0 || ku.uio_resid == sizeof(sei));
	}

	assert(sei.magic_flag == SWAP_MAGIC_FLAG);
	if(sei.used == 0 || ku.uio_resid > 0)
	{
		// This section is free or we have reached the end of the swap file
		swap_page_to_empty_disk_section(v, as, vpn, page_addr, pos);
	}
	else if(sei.as == as && sei.vpn == vpn && sei.used == 1)
	{
		kprintf("vpn: 0x%x, as: 0x%x\n", vpn, (vaddr_t)as);
		panic("This page is already swapped to disk!\n");
	}

	return;
}


/*
 * Swap a page from physical memory to disk
 */

void swap_out_page(struct addrspace *as, vaddr_t vpn, paddr_t page_addr)
{
	struct vnode *v;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);
	int result = vfs_open(swapfilename, O_RDWR, &v);

	if(result)
	{
		panic("Could not open file '%s' to swap out a page to", swapfilename);
	}
//	kprintf("SWAP:Swapping out 0x%x\n", vpn);
	lock_acquire(swap_lock);
	find_and_swap_page_to_disk(v, as, vpn, page_addr);
	lock_release(swap_lock);
	vfs_close(v);
	return;
}

off_t get_next_empty_section(struct vnode *v)
{
	struct uio ku;
	struct SwapEntryInfo sei;
	off_t pos = 0;

	mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
	assert(VOP_READ(v, &ku) == 0);
	if(ku.uio_resid == sizeof(sei) || sei.used == 0)
	{
		return pos;
	}
	else if(ku.uio_resid > 0 || sei.magic_flag != SWAP_MAGIC_FLAG)
	{
		panic("Swap file corrupted!\n");
	}

	while(ku.uio_resid == 0 && sei.used == 1)
	{
		pos += sizeof(sei) + PAGE_SIZE;
		mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_READ);
		assert(VOP_READ(v, &ku) == 0);
		assert(sei.magic_flag == SWAP_MAGIC_FLAG);
	}
	if(ku.uio_resid == sizeof(sei) || sei.used == 0)
	{
		return pos;
	}
	else if(ku.uio_resid > 0)
	{
		panic("Swap file corrupted!\n");
	}

	return pos;
}

#if 0
void write_page_in_segments(struct vnode *vin, struct vnode *vout, struct uio *u, struct addrspace *as,
		vaddr_t vpn, int num_bytes, int num_zeroes, off_t pos)
{
	// We will swap in segments because we don't have the luxury of fitting a
	// buffer of our page size in memory. Damned 512k RAM -_-
	char buf[SWAP_SEGMENT_SIZE];
	struct SwapEntryInfo sei;
	struct uio ku;
	int total_bytes = num_bytes + num_zeroes;
	assert(total_bytes == PAGE_SIZE);

	sei.as = as;
	sei.vpn = vpn;
	sei.magic_flag = SWAP_MAGIC_FLAG;
	sei.used = 1;
	mk_kuio(&ku, &sei, sizeof(sei), pos, UIO_WRITE);
	assert(VOP_WRITE(vout, &ku) == 0);
	assert(ku.uio_resid == 0);
	pos += sizeof(sei);

	while(total_bytes > 0)
	{
		if(num_bytes >= SWAP_SEGMENT_SIZE)
		{
			mk_kuio(&ku, buf, SWAP_SEGMENT_SIZE, u->uio_offset, UIO_READ);
			u->uio_offset += SWAP_SEGMENT_SIZE;
			assert(VOP_READ(vin, &ku) == 0);
			assert(ku.uio_resid == 0);
			mk_kuio(&ku, buf, SWAP_SEGMENT_SIZE, pos, UIO_WRITE);
			assert(VOP_WRITE(vout, &ku) == 0);
			assert(ku.uio_resid == 0);
			num_bytes -= SWAP_SEGMENT_SIZE;
			pos += SWAP_SEGMENT_SIZE;
		}
		else if(num_bytes > 0)
		{
			mk_kuio(&ku, buf, num_bytes, u->uio_offset, UIO_READ);
			u->uio_offset += num_bytes;
			assert(VOP_READ(vin, &ku) == 0);
			assert(ku.uio_resid == 0);
			bzero(buf + num_bytes, SWAP_SEGMENT_SIZE - num_bytes);
			mk_kuio(&ku, buf, SWAP_SEGMENT_SIZE, pos, UIO_WRITE);
			assert(VOP_WRITE(vout, &ku) == 0);
			assert(ku.uio_resid == 0);
			num_zeroes -= (SWAP_SEGMENT_SIZE - num_bytes);
			num_bytes = 0;
			pos += SWAP_SEGMENT_SIZE;
		}
		else
		{
			bzero(buf, SWAP_SEGMENT_SIZE);
			mk_kuio(&ku, buf, SWAP_SEGMENT_SIZE, pos, UIO_WRITE);
			assert(VOP_WRITE(vout, &ku) == 0);
			assert(ku.uio_resid == 0);
			num_zeroes -= SWAP_SEGMENT_SIZE;
			pos += SWAP_SEGMENT_SIZE;
		}
		total_bytes = num_bytes + num_zeroes;
	}

}


/*
 * Swap the data from the file point to by 'u' to our swap file
 */
void swap_segment_helper(struct vnode *v, struct uio *u, struct addrspace *as, vaddr_t vpn)
{
	off_t pos = 0;
	int total_bytes = u->uio_resid;
	assert(total_bytes >= 0); // overflow check
	int zeros_to_pad = u->uio_iovec.iov_len - total_bytes;
	int num_pages;
	int rem_bytes;
	int num_zeroes;
	char swapfilename[sizeof(SWAP_FILE_NAME) + 1];
	strcpy(swapfilename, SWAP_FILE_NAME);

	struct vnode *swap_file_v;
	lock_acquire(swap_lock);
	assert(vfs_open(swapfilename, O_RDWR, &swap_file_v) == 0);
	pos = get_next_empty_section(swap_file_v);

	while(total_bytes > 0 || zeros_to_pad > 0)
	{
		num_pages = total_bytes / PAGE_SIZE;
		rem_bytes = total_bytes % PAGE_SIZE;
		num_zeroes = (rem_bytes > 0) ? (PAGE_SIZE - rem_bytes) : 0;
		if(num_pages > 0)
		{
			write_page_in_segments(v, swap_file_v, u, as, vpn, PAGE_SIZE, 0, pos);
			vpn += ~PAGE_FRAME + 1;
			total_bytes -= PAGE_SIZE;
		}
		else if(rem_bytes > 0)
		{
			write_page_in_segments(v, swap_file_v, u, as, vpn, rem_bytes, num_zeroes, pos);
			total_bytes = 0;
			vpn += ~PAGE_FRAME + 1;
			zeros_to_pad -= num_zeroes;
		}
		else if(zeros_to_pad > 0)
		{
			write_page_in_segments(v, swap_file_v, u, as, vpn, 0, PAGE_SIZE, pos);
			vpn += ~PAGE_FRAME + 1;
			zeros_to_pad -= PAGE_SIZE;
		}
		pos = get_next_empty_section(swap_file_v);
	}
	vfs_close(swap_file_v);
	lock_release(swap_lock);

}

#endif

#endif

void swap_bootstrap()
{
	swap_lock = kmalloc(sizeof(struct lock));
	if(swap_lock == NULL)
	{
		panic("Couldn't allocate memory for the swap file lock\n");
	}
	swap_lock->lock_held = 0;
	swap_lock->lock_holder = NULL;
	swap_lock->name = NULL;
	bzero(swap_map, SWAP_MAP_SIZE * sizeof(struct SwapMap));
}

void swap_cleanup()
{
	kfree(swap_lock);
}
