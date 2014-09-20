/*
 * Code to load an ELF-format executable into the current address space.
 *
 * Right now it just copies into userspace and hopes the addresses are
 * mappable to real memory. This works with dumbvm; however, when you
 * write a real VM system, you will need to either (1) add code that 
 * makes the address range used for load valid, or (2) if you implement
 * memory-mapped files, map each segment instead of copying it into RAM.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <uio.h>
#include <elf.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vnode.h>
#include <vfs.h>
#include <swap.h>




/*
 * Load a segment at virtual address VADDR. The segment in memory
 * extends from VADDR up to (but not including) VADDR+MEMSIZE. The
 * segment on disk is located at file offset OFFSET and has length
 * FILESIZE.
 *
 * FILESIZE may be less than MEMSIZE; if so the remaining portion of
 * the in-memory segment should be zero-filled.
 *
 * Note that uiomove will catch it if someone tries to load an
 * executable whose load address is in kernel space. If you should
 * change this code to not use uiomove, be sure to check for this case
 * explicitly.
 */

int load_page_from_executable(char *exec_path, off_t offset, vaddr_t vaddr, paddr_t paddr,
	     size_t memsize, size_t filesize)
{
	struct uio u;

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	struct vnode *v;
	assert(vfs_open(exec_path, O_RDONLY, &v) == 0);

	DEBUG(DB_EXEC, "ELF: Loading %lu bytes to 0x%lx\n",
	      (unsigned long) filesize, (unsigned long) vaddr);
	assert(memsize <= PAGE_SIZE);

	mk_kuio(&u, (void*)PADDR_TO_KVADDR(paddr), filesize, offset, UIO_READ);
	assert(VOP_READ(v, &u) == 0);
	vfs_close(v);

	if (u.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}
	// We have to pad with zeroes if required
	DEBUG(DB_EXEC, "ELF: Padding with %d 0s\n", memsize - filesize);
	bzero((void*)PADDR_TO_KVADDR(paddr + filesize), memsize - filesize);

	return 0;
}



/*
 * Swap a data segment at vaddr to disk which will be loaded later on demand.
 */
static
int
setup_segment(off_t offset, size_t memsize, size_t filesize,
	     int is_executable, struct addrspace *as)
{
	// Don't need to swap code segment. It can be read from the binary
	if(is_executable)
	{
		// If this assertion fails that means we have more than one executable segment!
		// In that case we are screwed. Our VM implementation will have to change completely.
		assert(as->executable_offset == 0);
		as->executable_offset = offset;
		as->executable_filesize = filesize;
		as->executable_memsize = memsize;
		return 0;
	}
	else
	{
		as->data_offset = offset;
		as->data_filesize = filesize;
		as->data_memsize = memsize;
	}
	return 0;
}

/*
 * Load an ELF executable user program into the current address space.
 *
 * Returns the entry point (initial PC) for the program in ENTRYPOINT.
 */
int
load_elf(struct vnode *v, vaddr_t *entrypoint)
{
	Elf_Ehdr eh;   /* Executable header */
	Elf_Phdr ph;   /* "Program header" = segment header */
	int result, i;
	struct uio ku;

	/*
	 * Read the executable header from offset 0 in the file.
	 */

	mk_kuio(&ku, &eh, sizeof(eh), 0, UIO_READ);
	result = VOP_READ(v, &ku);
	if (result) {
		return result;
	}

	if (ku.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on header - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * Check to make sure it's a 32-bit ELF-version-1 executable
	 * for our processor type. If it's not, we can't run it.
	 *
	 * Ignore EI_OSABI and EI_ABIVERSION - properly, we should
	 * define our own, but that would require tinkering with the
	 * linker to have it emit our magic numbers instead of the
	 * default ones. (If the linker even supports these fields,
	 * which were not in the original elf spec.)
	 */

	if (eh.e_ident[EI_MAG0] != ELFMAG0 ||
	    eh.e_ident[EI_MAG1] != ELFMAG1 ||
	    eh.e_ident[EI_MAG2] != ELFMAG2 ||
	    eh.e_ident[EI_MAG3] != ELFMAG3 ||
	    eh.e_ident[EI_CLASS] != ELFCLASS32 ||
	    eh.e_ident[EI_DATA] != ELFDATA2MSB ||
	    eh.e_ident[EI_VERSION] != EV_CURRENT ||
	    eh.e_version != EV_CURRENT ||
	    eh.e_type!=ET_EXEC ||
	    eh.e_machine!=EM_MACHINE) {
		return ENOEXEC;
	}

	/*
	 * Go through the list of segments and set up the address space.
	 *
	 * Ordinarily there will be one code segment, one read-only
	 * data segment, and one data/bss segment, but there might
	 * conceivably be more. You don't need to support such files
	 * if it's unduly awkward to do so.
	 *
	 * Note that the expression eh.e_phoff + i*eh.e_phentsize is 
	 * mandated by the ELF standard - we use sizeof(ph) to load,
	 * because that's the structure we know, but the file on disk
	 * might have a larger structure, so we must use e_phentsize
	 * to find where the phdr starts.
	 */

	for (i=0; i<eh.e_phnum; i++) {
		off_t offset = eh.e_phoff + i*eh.e_phentsize;
		mk_kuio(&ku, &ph, sizeof(ph), offset, UIO_READ);

		result = VOP_READ(v, &ku);
		if (result) {
			return result;
		}

		if (ku.uio_resid != 0) {
			/* short read; problem with executable? */
			kprintf("ELF: short read on phdr - file truncated?\n");
			return ENOEXEC;
		}

		switch (ph.p_type) {
		    case PT_NULL: /* skip */ continue;
		    case PT_PHDR: /* skip */ continue;
		    case PT_MIPS_REGINFO: /* skip */ continue;
		    case PT_LOAD: break;
		    default:
			kprintf("loadelf: unknown segment type %d\n", 
				ph.p_type);
			return ENOEXEC;
		}

		result = as_define_region(curthread->t_vmspace,
					  ph.p_vaddr, ph.p_memsz,
					  ph.p_flags & PF_R,
					  ph.p_flags & PF_W,
					  ph.p_flags & PF_X);
		if (result) {
			return result;
		}
	}

	result = as_prepare_load(curthread->t_vmspace);
	if (result) {
		return result;
	}

	/*
	 * Now swap all data segments to disk. We will
	 * load the executable segments later on demand.
	 */

	for (i=0; i<eh.e_phnum; i++) {
		off_t offset = eh.e_phoff + i*eh.e_phentsize;
		mk_kuio(&ku, &ph, sizeof(ph), offset, UIO_READ);

		result = VOP_READ(v, &ku);
		if (result) {
			return result;
		}

		if (ku.uio_resid != 0) {
			/* short read; problem with executable? */
			kprintf("ELF: short read on phdr - file truncated?\n");
			return ENOEXEC;
		}

		switch (ph.p_type) {
		    case PT_NULL: /* skip */ continue;
		    case PT_PHDR: /* skip */ continue;
		    case PT_MIPS_REGINFO: /* skip */ continue;
		    case PT_LOAD: break;
		    default:
			kprintf("loadelf: unknown segment type %d\n", 
				ph.p_type);
			return ENOEXEC;
		}

		result = setup_segment(ph.p_offset, ph.p_memsz, ph.p_filesz,
				      ph.p_flags & PF_X, curthread->t_vmspace);
		if (result) {
			return result;
		}
	}


	result = as_complete_load(curthread->t_vmspace);
	if (result) {
		return result;
	}

	*entrypoint = eh.e_entry;
	DEBUG(DB_EXEC, "Program entry point 0x%x\n", *entrypoint);

	return 0;
}
