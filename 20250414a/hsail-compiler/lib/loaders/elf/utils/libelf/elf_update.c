/*-
 * Copyright (c) 2006-2011 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#if !defined(WIN32)
#include <sys/mman.h>
#include "queue.h"
#endif
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <gelf.h>
#include <libelf.h>
#include <stdlib.h>
#include <string.h>
#if !defined(WIN32)
#include <unistd.h>
#else
#include <Windows.h>
#ifndef PROT_READ
#define PROT_READ FILE_MAP_READ
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE FILE_MAP_COPY
#endif
#ifndef MAP_FAILED
#define MAP_FAILED NULL
#endif
#endif

#include "_libelf.h"

#ifdef ANDROID
#include "roundup.h"
#else
#include <sys/param.h>
#endif

LIBELF_VCSID("$Id: elf_update.c 1922 2011-09-23 08:04:33Z jkoshy $");

/*
 * Layout strategy:
 *
 * - Case 1: ELF_F_LAYOUT is asserted
 *     In this case the application has full control over where the
 *     section header table, program header table, and section data
 *     will reside.   The library only perform error checks.
 *
 * - Case 2: ELF_F_LAYOUT is not asserted
 *
 *     The library will do the object layout using the following
 *     ordering:
 *     - The executable header is placed first, are required by the
 *     	 ELF specification.
 *     - The program header table is placed immediately following the
 *       executable header.
 *     - Section data, if any, is placed after the program header
 *       table, aligned appropriately.
 *     - The section header table, if needed, is placed last.
 *
 *     There are two sub-cases to be taken care of:
 *
 *     - Case 2a: e->e_cmd == ELF_C_READ or ELF_C_RDWR
 *
 *       In this sub-case, the underlying ELF object may already have
 *       content in it, which the application may have modified.  The
 *       library will retrieve content from the existing object as
 *       needed.
 *
 *     - Case 2b: e->e_cmd == ELF_C_WRITE
 *
 *       The ELF object is being created afresh in this sub-case;
 *       there is no pre-existing content in the underlying ELF
 *       object.
 */

/*
 * The types of extents in an ELF object.
 */
enum elf_extent {
	ELF_EXTENT_EHDR,
	ELF_EXTENT_PHDR,
	ELF_EXTENT_SECTION,
	ELF_EXTENT_SHDR
};

/*
 * A extent descriptor, used when laying out an ELF object.
 */
struct _Elf_Extent {
	ELF_SLIST_ENTRY(_Elf_Extent) ex_next;
	uint64_t	ex_start; /* Start of the region. */
	uint64_t	ex_size;  /* The size of the region. */
	enum elf_extent	ex_type;  /* Type of region. */
	void		*ex_desc; /* Associated descriptor. */
};

ELF_SLIST_HEAD(_Elf_Extent_List, _Elf_Extent);

/*
 * Compute the extents of a section, by looking at the data
 * descriptors associated with it.  The function returns 1
 * if successful, or zero if an error was detected.
 */
static int
_libelf_compute_section_extents(Elf *e, Elf_Scn *s, off_t rc)
{
	int ec;
	size_t fsz, msz;
	Elf_Data *d;
	Elf32_Shdr *shdr32;
	Elf64_Shdr *shdr64;
	uint32_t sh_type;
	uint64_t d_align;
	unsigned int elftype;
	uint64_t scn_size, scn_alignment;
	uint64_t sh_align, sh_entsize, sh_offset, sh_size;

	ec = e->e_class;

	shdr32 = &s->s_shdr.s_shdr32;
	shdr64 = &s->s_shdr.s_shdr64;
	if (ec == ELFCLASS32) {
		sh_type    = shdr32->sh_type;
		sh_align   = (uint64_t) shdr32->sh_addralign;
		sh_entsize = (uint64_t) shdr32->sh_entsize;
		sh_offset  = (uint64_t) shdr32->sh_offset;
		sh_size    = (uint64_t) shdr32->sh_size;
	} else {
		sh_type    = shdr64->sh_type;
		sh_align   = shdr64->sh_addralign;
		sh_entsize = shdr64->sh_entsize;
		sh_offset  = shdr64->sh_offset;
		sh_size    = shdr64->sh_size;
	}

	assert(sh_type != SHT_NULL && sh_type != SHT_NOBITS);

	elftype = _libelf_xlate_shtype(sh_type);
	if (elftype > ELF_T_LAST) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (0);
	}

	if (sh_align == 0)
		sh_align = _libelf_falign(elftype, ec);

	/*
	 * Compute the section's size and alignment using the data
	 * descriptors associated with the section.
	 */
	if (STAILQ_EMPTY(&s->s_data)) {
		/*
		 * The section's content (if any) has not been read in
		 * yet.  If section is not dirty marked dirty, we can
		 * reuse the values in the 'sh_size' and 'sh_offset'
		 * fields of the section header.
		 */
		if ((s->s_flags & ELF_F_DIRTY) == 0) {
			/*
			 * If the library is doing the layout, then we
			 * compute the new start offset for the
			 * section based on the current offset and the
			 * section's alignment needs.
			 *
			 * If the application is doing the layout, we
			 * can use the value in the 'sh_offset' field
			 * in the section header directly.
			 */
			if (e->e_flags & ELF_F_LAYOUT)
				goto updatedescriptor;
			else
				goto computeoffset;
		}

		/*
		 * Otherwise, we need to bring in the section's data
		 * from the underlying ELF object.
		 */
		if (e->e_cmd != ELF_C_WRITE && elf_getdata(s, NULL) == NULL)
			return (0);
	}

	/*
	 * Loop through the section's data descriptors.
	 */
	scn_size = 0L;
	scn_alignment = 0;
	STAILQ_FOREACH(d, &s->s_data, d_next)  {

		/*
		 * The data buffer's type is known.
		 */
		if (d->d_type >= ELF_T_NUM) {
			LIBELF_SET_ERROR(DATA, 0);
			return (0);
		}

		/*
		 * The data buffer's version is supported.
		 */
		if (d->d_version != e->e_version) {
			LIBELF_SET_ERROR(VERSION, 0);
			return (0);
		}

		/*
		 * The buffer's alignment is non-zero and a power of
		 * two.
		 */
		if ((d_align = d->d_align) == 0 ||
		    (d_align & (d_align - 1))) {
			LIBELF_SET_ERROR(DATA, 0);
			return (0);
		}

		/*
		 * The buffer's size should be a multiple of the
		 * memory size of the underlying type.
		 */
		msz = _libelf_msize(d->d_type, ec, e->e_version);
		if (d->d_size % msz) {
			LIBELF_SET_ERROR(DATA, 0);
			return (0);
		}

		/*
		 * If the application is controlling layout, then the
		 * d_offset field should be compatible with the
		 * buffer's specified alignment.
		 */
		if ((e->e_flags & ELF_F_LAYOUT) &&
		    (d->d_off & (d_align - 1))) {
			LIBELF_SET_ERROR(LAYOUT, 0);
			return (0);
		}

		/*
		 * Compute the section's size.
		 */
		if (e->e_flags & ELF_F_LAYOUT) {
			if ((uint64_t) d->d_off + d->d_size > scn_size)
				scn_size = d->d_off + d->d_size;
		} else {
			scn_size = roundup2(scn_size, d->d_align);
			d->d_off = scn_size;
			fsz = _libelf_fsize(d->d_type, ec, d->d_version,
			    d->d_size / msz);
			scn_size += fsz;
		}

		/*
		 * The section's alignment is the maximum alignment
		 * needed for its data buffers.
		 */
		if (d_align > scn_alignment)
			scn_alignment = d_align;
	}

	/*
	 * If the application is requesting full control over the
	 * layout of the section, check the section's specified size,
	 * offsets and alignment for sanity.
	 */
	if (e->e_flags & ELF_F_LAYOUT) {
		if (scn_alignment > sh_align || sh_offset % sh_align ||
		    sh_size < scn_size) {
			LIBELF_SET_ERROR(LAYOUT, 0);
			return (0);
		}
		goto updatedescriptor;
	}

	/*
	 * Otherwise, compute the values in the section header.
	 *
	 * The section alignment is the maximum alignment for any of
	 * its contained data descriptors.
	 */
	if (scn_alignment > sh_align)
		sh_align = scn_alignment;

	/*
	 * If the section entry size is zero, try and fill in an
	 * appropriate entry size.  Per the elf(5) manual page
	 * sections without fixed-size entries should have their
	 * 'sh_entsize' field set to zero.
	 */
	if (sh_entsize == 0 &&
	    (sh_entsize = _libelf_fsize(elftype, ec, e->e_version,
		(size_t) 1)) == 1)
		sh_entsize = 0;

	sh_size = scn_size;

computeoffset:
	/*
	 * Compute the new offset for the section based on
	 * the section's alignment needs.
	 */
	sh_offset = roundup(rc, sh_align);

	/*
	 * Update the section header.
	 */
	if (ec == ELFCLASS32) {
		shdr32->sh_addralign = (uint32_t) sh_align;
		shdr32->sh_entsize   = (uint32_t) sh_entsize;
		shdr32->sh_offset    = (uint32_t) sh_offset;
		shdr32->sh_size      = (uint32_t) sh_size;
	} else {
		shdr64->sh_addralign = sh_align;
		shdr64->sh_entsize   = sh_entsize;
		shdr64->sh_offset    = sh_offset;
		shdr64->sh_size      = sh_size;
	}

updatedescriptor:
	/*
	 * Update the section descriptor.
	 */
	s->s_size = sh_size;
	s->s_offset = sh_offset;

	return (1);
}

/*
 * Free a list of extent descriptors.
 */

static void
_libelf_release_extents(struct _Elf_Extent_List *extents)
{
	struct _Elf_Extent *ex;

	while ((ex = ELF_SLIST_FIRST(extents)) != NULL) {
		ELF_SLIST_REMOVE_HEAD(extents, ex_next);
		free(ex);
	}
}

/*
 * Check if an extent 's' defined by [start..start+size) is free.
 * This routine assumes that the given extent list is sorted in order
 * of ascending extent offsets.
 */

static int
_libelf_extent_is_unused(struct _Elf_Extent_List *extents,
    const uint64_t start, const uint64_t size, struct _Elf_Extent **prevt)
{
	uint64_t tmax, tmin;
	struct _Elf_Extent *t, *pt;
	const uint64_t smax = start + size;

	/* First, look for overlaps with existing extents. */
	pt = NULL;
	ELF_SLIST_FOREACH(t, extents, ex_next) {
		tmin = t->ex_start;
		tmax = tmin + t->ex_size;

		if (tmax <= start) {
			/*
			 * 't' lies entirely before 's': ...| t |...| s |...
			 */
			pt = t;
			continue;
		} else if (smax <= tmin) {
			/*
			 * 's' lies entirely before 't', and after 'pt':
			 *      ...| pt |...| s |...| t |...
			 */
			assert(pt == NULL ||
			    pt->ex_start + pt->ex_size <= start);
			break;
		} else
			/* 's' and 't' overlap. */
			return (0);
	}

	if (prevt)
		*prevt = pt;
	return (1);
}

/*
 * Insert an extent into the list of extents.
 */

static int
_libelf_insert_extent(struct _Elf_Extent_List *extents, int type,
    uint64_t start, uint64_t size, void *desc)
{
	struct _Elf_Extent *ex, *prevt;

	assert(type >= ELF_EXTENT_EHDR && type <= ELF_EXTENT_SHDR);

	prevt = NULL;

	/*
	 * If the requested range overlaps with an existing extent,
	 * signal an error.
	 */
	if (!_libelf_extent_is_unused(extents, start, size, &prevt)) {
		LIBELF_SET_ERROR(LAYOUT, 0);
		return (0);
	}

	/* Allocate and fill in a new extent descriptor. */
	if ((ex = malloc(sizeof(struct _Elf_Extent))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, errno);
		return (0);
	}
	ex->ex_start = start;
	ex->ex_size = size;
	ex->ex_desc = desc;
	ex->ex_type = type;

	/* Insert the region descriptor into the list. */
	if (prevt)
		ELF_SLIST_INSERT_AFTER(prevt, ex, ex_next);
	else
		ELF_SLIST_INSERT_HEAD(extents, ex, ex_next);
	return (1);
}

/*
 * Recompute section layout.
 */

static off_t
_libelf_resync_sections(Elf *e, off_t rc, struct _Elf_Extent_List *extents)
{
	int ec;
	Elf_Scn *s;
	size_t sh_type;

	ec = e->e_class;

	/*
	 * Make a pass through sections, computing the extent of each
	 * section.
	 */
	STAILQ_FOREACH(s, &e->e_u.e_elf.e_scn, s_next) {
		if (ec == ELFCLASS32)
			sh_type = s->s_shdr.s_shdr32.sh_type;
		else
			sh_type = s->s_shdr.s_shdr64.sh_type;

		if (sh_type == SHT_NOBITS || sh_type == SHT_NULL)
			continue;

		if (_libelf_compute_section_extents(e, s, rc) == 0)
			return ((off_t) -1);

		if (s->s_size == 0)
			continue;

		if (!_libelf_insert_extent(extents, ELF_EXTENT_SECTION,
		    s->s_offset, s->s_size, s))
			return ((off_t) -1);

		if ((size_t) rc < s->s_offset + s->s_size)
			rc = s->s_offset + s->s_size;
	}

	return (rc);
}

/*
 * Recompute the layout of the ELF object and update the internal data
 * structures associated with the ELF descriptor.
 *
 * Returns the size in bytes the ELF object would occupy in its file
 * representation.
 *
 * After a successful call to this function, the following structures
 * are updated:
 *
 * - The ELF header is updated.
 * - All extents in the ELF object are sorted in order of ascending
 *   addresses.  Sections have their section header table entries
 *   updated.  An error is signalled if an overlap was detected among
 *   extents.
 * - Data descriptors associated with sections are checked for valid
 *   types, offsets and alignment.
 *
 * After a resync_elf() successfully returns, the ELF descriptor is
 * ready for being handed over to _libelf_write_elf().
 */

static off_t
_libelf_resync_elf(Elf *e, struct _Elf_Extent_List *extents)
{
	int ec, eh_class, eh_type;
	unsigned int eh_byteorder, eh_version;
	size_t align, fsz;
	size_t phnum, shnum;
	off_t rc, phoff, shoff;
	void *ehdr, *phdr;
	Elf32_Ehdr *eh32;
	Elf64_Ehdr *eh64;

	rc = 0;

	ec = e->e_class;

	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	/*
	 * Prepare the EHDR.
	 */
	if ((ehdr = _libelf_ehdr(e, ec, 0)) == NULL)
		return ((off_t) -1);

	eh32 = ehdr;
	eh64 = ehdr;

	if (ec == ELFCLASS32) {
		eh_byteorder = eh32->e_ident[EI_DATA];
		eh_class     = eh32->e_ident[EI_CLASS];
		phoff        = (uint64_t) eh32->e_phoff;
		shoff        = (uint64_t) eh32->e_shoff;
		eh_type      = eh32->e_type;
		eh_version   = eh32->e_version;
	} else {
		eh_byteorder = eh64->e_ident[EI_DATA];
		eh_class     = eh64->e_ident[EI_CLASS];
		phoff        = eh64->e_phoff;
		shoff        = eh64->e_shoff;
		eh_type      = eh64->e_type;
		eh_version   = eh64->e_version;
	}

	if (eh_version == EV_NONE)
		eh_version = EV_CURRENT;

	if (eh_version != e->e_version) {	/* always EV_CURRENT */
		LIBELF_SET_ERROR(VERSION, 0);
		return ((off_t) -1);
	}

	if (eh_class != e->e_class) {
		LIBELF_SET_ERROR(CLASS, 0);
		return ((off_t) -1);
	}

	if ((e->e_cmd == ELF_C_READ
        || (e->e_cmd == ELF_C_RDWR && e->e_rawfile))
        && eh_byteorder != e->e_byteorder) {
		LIBELF_SET_ERROR(HEADER, 0);
		return ((off_t) -1);
	}

	shnum = e->e_u.e_elf.e_nscn;
	phnum = e->e_u.e_elf.e_nphdr;

	e->e_byteorder = eh_byteorder;

#define	INITIALIZE_EHDR(E,EC,V)	do {					\
		(E)->e_ident[EI_MAG0] = ELFMAG0;			\
		(E)->e_ident[EI_MAG1] = ELFMAG1;			\
		(E)->e_ident[EI_MAG2] = ELFMAG2;			\
		(E)->e_ident[EI_MAG3] = ELFMAG3;			\
		(E)->e_ident[EI_CLASS] = (EC);				\
		(E)->e_ident[EI_VERSION] = (V);				\
		(E)->e_ehsize = _libelf_fsize(ELF_T_EHDR, (EC), (V),	\
		    (size_t) 1);					\
		(E)->e_phentsize = (phnum == 0) ? 0 : _libelf_fsize(	\
		    ELF_T_PHDR, (EC), (V), (size_t) 1);			\
		(E)->e_shentsize = _libelf_fsize(ELF_T_SHDR, (EC), (V),	\
		    (size_t) 1);					\
	} while (0)

	if (ec == ELFCLASS32)
		INITIALIZE_EHDR(eh32, ec, eh_version);
	else
		INITIALIZE_EHDR(eh64, ec, eh_version);

	(void) elf_flagehdr(e, ELF_C_SET, ELF_F_DIRTY);

	rc += _libelf_fsize(ELF_T_EHDR, ec, eh_version, (size_t) 1);

	if (!_libelf_insert_extent(extents, ELF_EXTENT_EHDR, 0, rc, ehdr))
		return ((off_t) -1);

	/*
	 * Compute the layout the program header table, if one is
	 * present.  The program header table needs to be aligned to a
	 * `natural' boundary.
	 */
	if (phnum) {
		fsz = _libelf_fsize(ELF_T_PHDR, ec, eh_version, phnum);
		align = _libelf_falign(ELF_T_PHDR, ec);

		if (e->e_flags & ELF_F_LAYOUT) {
			/*
			 * Check offsets for sanity.
			 */
			if (rc > phoff) {
				LIBELF_SET_ERROR(LAYOUT, 0);
				return ((off_t) -1);
			}

			if (phoff % align) {
				LIBELF_SET_ERROR(LAYOUT, 0);
				return ((off_t) -1);
			}

		} else
			phoff = roundup(rc, align);

		rc = phoff + fsz;

		phdr = _libelf_getphdr(e, ec);

		if (!_libelf_insert_extent(extents, ELF_EXTENT_PHDR, phoff,
			fsz, phdr))
			return ((off_t) -1);
	} else
		phoff = 0;

	/*
	 * Compute the layout of the sections associated with the
	 * file.
	 */
    /*
     * If we are a read only elf that has not had its
     * headers loaded, or a read/write elf that is not
     * based on a file descripter and had its headers
     * loaded, then lets load the headers.
     * If the loading of the headers fails, return -1.
     */
	if ((e->e_cmd == ELF_C_READ
        || (e->e_cmd == ELF_C_RDWR && e->e_rawfile
        && e->e_fd == -1)) &&
	    (e->e_flags & LIBELF_F_SHDRS_LOADED) == 0 &&
	    _libelf_load_section_headers(e, ehdr) == 0)
		return ((off_t) -1);

	if ((rc = _libelf_resync_sections(e, rc, extents)) < 0)
		return ((off_t) -1);

	/*
	 * Compute the space taken up by the section header table, if
	 * one is needed.
	 *
	 * If ELF_F_LAYOUT has been asserted, the application may have
	 * placed the section header table in between existing
	 * sections, so the net size of the file need not increase due
	 * to the presence of the section header table.
	 *
	 * If the library is responsible for laying out the object,
	 * the section header table is placed after section data.
	 */
	if (shnum) {
		fsz = _libelf_fsize(ELF_T_SHDR, ec, eh_version, shnum);
		align = _libelf_falign(ELF_T_SHDR, ec);

		if (e->e_flags & ELF_F_LAYOUT) {
			if (shoff % align) {
				LIBELF_SET_ERROR(LAYOUT, 0);
				return ((off_t) -1);
			}
		} else
			shoff = roundup(rc, align);

		if (shoff + fsz > (size_t) rc)
			rc = shoff + fsz;

		if (!_libelf_insert_extent(extents, ELF_EXTENT_SHDR, shoff,
		    fsz, NULL))
			return ((off_t) -1);
	} else
		shoff = 0;

	/*
	 * Set the fields of the Executable Header that could potentially use
	 * extended numbering.
	 */
	_libelf_setphnum(e, ehdr, ec, phnum);
	_libelf_setshnum(e, ehdr, ec, shnum);

	/*
	 * Update the `e_phoff' and `e_shoff' fields if the library is
	 * doing the layout.
	 */
	if ((e->e_flags & ELF_F_LAYOUT) == 0) {
		if (ec == ELFCLASS32) {
			eh32->e_phoff = (uint32_t) phoff;
			eh32->e_shoff = (uint32_t) shoff;
		} else {
			eh64->e_phoff = (uint64_t) phoff;
			eh64->e_shoff = (uint64_t) shoff;
		}
	}

	return (rc);
}

/*
 * Write out the contents of an ELF section.
 */

static size_t
_libelf_write_scn(Elf *e, char *nf, struct _Elf_Extent *ex)
{
	int ec;
	size_t fsz, msz, nobjects, rc;
	uint32_t sh_type;
	uint64_t sh_off, sh_size;
	int elftype;
	Elf_Scn *s;
	Elf_Data *d, dst;

	assert(ex->ex_type == ELF_EXTENT_SECTION);

	s = ex->ex_desc;
	rc = ex->ex_start;

	if ((ec = e->e_class) == ELFCLASS32) {
		sh_type = s->s_shdr.s_shdr32.sh_type;
		sh_size = (uint64_t) s->s_shdr.s_shdr32.sh_size;
	} else {
		sh_type = s->s_shdr.s_shdr64.sh_type;
		sh_size = s->s_shdr.s_shdr64.sh_size;
	}

	/*
	 * Ignore sections that do not allocate space in the file.
	 */
	if (sh_type == SHT_NOBITS || sh_type == SHT_NULL || sh_size == 0)
		return (rc);

	elftype = _libelf_xlate_shtype(sh_type);
	assert(elftype >= ELF_T_FIRST && elftype <= ELF_T_LAST);

	sh_off = s->s_offset;
	assert(sh_off % _libelf_falign(elftype, ec) == 0);

	/*
	 * If the section has a `rawdata' descriptor, and the section
	 * contents have not been modified, use its contents directly.
	 * The `s_rawoff' member contains the offset into the original
	 * file, while `s_offset' contains its new location in the
	 * destination.
	 */

  /* If we are a read/write elf, we cannot trust the rawdata. */
  if (e->e_cmd != ELF_C_RDWR) {
    if (STAILQ_EMPTY(&s->s_data)) {

      if ((d = elf_rawdata(s, NULL)) == NULL)
        return ((off_t) -1);

      STAILQ_FOREACH(d, &s->s_rawdata, d_next) {
        if ((uint64_t) rc < sh_off + d->d_off)
          (void) memset(nf + rc,
              LIBELF_PRIVATE(fillchar), sh_off +
              d->d_off - rc);
        rc = sh_off + d->d_off;

        assert(d->d_buf != NULL);
        assert(d->d_type == ELF_T_BYTE);
        assert(d->d_version == e->e_version);

        (void) memcpy(nf + rc,
            e->e_rawfile + s->s_rawoff + d->d_off, d->d_size);

        rc += d->d_size;
      }

      return (rc);
    }
  }

  /*
   * Iterate over the set of data descriptors for this section.
   * The prior call to _libelf_resync_elf() would have setup the
	 * descriptors for this step.
	 */

	dst.d_version = e->e_version;

	STAILQ_FOREACH(d, &s->s_data, d_next) {

		msz = _libelf_msize(d->d_type, ec, e->e_version);

		if ((uint64_t) rc < sh_off + d->d_off)
			(void) memset(nf + rc,
			    LIBELF_PRIVATE(fillchar), sh_off + d->d_off - rc);

		rc = sh_off + d->d_off;

		assert(d->d_buf != NULL);
		assert(d->d_version == e->e_version);
		assert(d->d_size % msz == 0);

		nobjects = d->d_size / msz;

		fsz = _libelf_fsize(d->d_type, ec, e->e_version, nobjects);

		dst.d_buf    = nf + rc;
		dst.d_size   = fsz;

		if (_libelf_xlate(&dst, d, e->e_byteorder, ec, ELF_TOFILE) ==
		    NULL)
			return ((off_t) -1);

		rc += fsz;
	}

	return ((off_t) rc);
}

/*
 * Write out an ELF Executable Header.
 */

static off_t
_libelf_write_ehdr(Elf *e, char *nf, struct _Elf_Extent *ex)
{
	int ec;
	void *ehdr;
	size_t fsz, msz;
	Elf_Data dst, src;

	assert(ex->ex_type == ELF_EXTENT_EHDR);
	assert(ex->ex_start == 0); /* Ehdr always comes first. */

	ec = e->e_class;

	ehdr = _libelf_ehdr(e, ec, 0);
	assert(ehdr != NULL);

	fsz = _libelf_fsize(ELF_T_EHDR, ec, e->e_version, (size_t) 1);
	msz = _libelf_msize(ELF_T_EHDR, ec, e->e_version);

	(void) memset(&dst, 0, sizeof(dst));
	(void) memset(&src, 0, sizeof(src));

	src.d_buf     = ehdr;
	src.d_size    = msz;
	src.d_type    = ELF_T_EHDR;
	src.d_version = dst.d_version = e->e_version;

	dst.d_buf     = nf;
	dst.d_size    = fsz;

	if (_libelf_xlate(&dst, &src, e->e_byteorder, ec, ELF_TOFILE) ==
	    NULL)
		return ((off_t) -1);

	return ((off_t) fsz);
}

/*
 * Write out an ELF program header table.
 */

static off_t
_libelf_write_phdr(Elf *e, char *nf, struct _Elf_Extent *ex)
{
	int ec;
	void *ehdr;
	Elf32_Ehdr *eh32;
	Elf64_Ehdr *eh64;
	Elf_Data dst, src;
	size_t fsz, phnum;
	uint64_t phoff;

	assert(ex->ex_type == ELF_EXTENT_PHDR);

	ec = e->e_class;
	ehdr = _libelf_ehdr(e, ec, 0);
	phnum = e->e_u.e_elf.e_nphdr;

	assert(phnum > 0);

	if (ec == ELFCLASS32) {
		eh32 = (Elf32_Ehdr *) ehdr;
		phoff = (uint64_t) eh32->e_phoff;
	} else {
		eh64 = (Elf64_Ehdr *) ehdr;
		phoff = eh64->e_phoff;
	}

	assert(phoff > 0);
	assert(ex->ex_start == phoff);
	assert(phoff % _libelf_falign(ELF_T_PHDR, ec) == 0);

	(void) memset(&dst, 0, sizeof(dst));
	(void) memset(&src, 0, sizeof(src));

	fsz = _libelf_fsize(ELF_T_PHDR, ec, e->e_version, phnum);
	assert(fsz > 0);

	src.d_buf = _libelf_getphdr(e, ec);
	src.d_version = dst.d_version = e->e_version;
	src.d_type = ELF_T_PHDR;
	src.d_size = phnum * _libelf_msize(ELF_T_PHDR, ec,
	    e->e_version);

	dst.d_size = fsz;
	dst.d_buf = nf + ex->ex_start;

	if (_libelf_xlate(&dst, &src, e->e_byteorder, ec, ELF_TOFILE) ==
	    NULL)
		return ((off_t) -1);

	return (phoff + fsz);
}

/*
 * Write out an ELF section header table.
 */

static off_t
_libelf_write_shdr(Elf *e, char *nf, struct _Elf_Extent *ex)
{
	int ec;
	void *ehdr;
	Elf_Scn *scn;
	uint64_t shoff;
	Elf32_Ehdr *eh32;
	Elf64_Ehdr *eh64;
	size_t fsz, nscn;
	Elf_Data dst, src;

	assert(ex->ex_type == ELF_EXTENT_SHDR);

	ec = e->e_class;
	ehdr = _libelf_ehdr(e, ec, 0);
	nscn = e->e_u.e_elf.e_nscn;

	if (ec == ELFCLASS32) {
		eh32 = (Elf32_Ehdr *) ehdr;
		shoff = (uint64_t) eh32->e_shoff;
	} else {
		eh64 = (Elf64_Ehdr *) ehdr;
		shoff = eh64->e_shoff;
	}

	assert(nscn > 0);
	assert(shoff % _libelf_falign(ELF_T_SHDR, ec) == 0);
	assert(ex->ex_start == shoff);

	(void) memset(&dst, 0, sizeof(dst));
	(void) memset(&src, 0, sizeof(src));

	src.d_type = ELF_T_SHDR;
	src.d_size = _libelf_msize(ELF_T_SHDR, ec, e->e_version);
	src.d_version = dst.d_version = e->e_version;

	fsz = _libelf_fsize(ELF_T_SHDR, ec, e->e_version, (size_t) 1);

	STAILQ_FOREACH(scn, &e->e_u.e_elf.e_scn, s_next) {
		if (ec == ELFCLASS32)
			src.d_buf = &scn->s_shdr.s_shdr32;
		else
			src.d_buf = &scn->s_shdr.s_shdr64;

		dst.d_size = fsz;
		dst.d_buf = nf + ex->ex_start + scn->s_ndx * fsz;

		if (_libelf_xlate(&dst, &src, e->e_byteorder, ec,
		    ELF_TOFILE) == NULL)
			return ((off_t) -1);
	}

	return (ex->ex_start + nscn * fsz);
}

/*
 * Update the elf file image.
 *
 * The original file could have been mapped in with an ELF_C_RDWR
 * command and the application could have added new content or
 * re-arranged its sections before calling elf_update().  Consequently
 * its not safe to work `in place' on the original file.  So we
 * malloc() the required space for the updated ELF object and build
 * the object there and write it out to the underlying file at the
 * end.  Note that the application may have opened the underlying file
 * in ELF_C_RDWR and only retrieved/modified a few sections.  We take
 * care to avoid translating file sections unnecessarily.
 *
 * Gaps in the coverage of the file by the file's sections will be
 * filled with the fill character set by elf_fill(3).
 */

static off_t
_libelf_update_elf(Elf *e, off_t newsize, struct _Elf_Extent_List *extents)
{
	off_t nrc, rc;
	char *newfile;
	struct _Elf_Extent *ex;

	assert(e->e_kind == ELF_K_ELF);
    // There are two types of ELF_C_RDWR files, one that is based in
    // memory and has a raw file and one that is based on a file
    // descriptor and does not have a raw_file. Both are equally
    // valid, so we don't special case here.
	assert(e->e_cmd == ELF_C_RDWR || (e->e_cmd == ELF_C_WRITE && e->e_fd != -1));

	if ((newfile = e->e_mem.alloc((size_t) newsize)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, errno);
		return ((off_t) -1);
	}

	nrc = rc = 0;
	ELF_SLIST_FOREACH(ex, extents, ex_next) {

		/* Fill inter-extent gaps. */
		if (ex->ex_start > (size_t) rc)
			(void) memset(newfile + rc, LIBELF_PRIVATE(fillchar),
			    ex->ex_start - rc);

		switch (ex->ex_type) {
		case ELF_EXTENT_EHDR:
			if ((nrc = _libelf_write_ehdr(e, newfile, ex)) < 0)
				goto error;
			break;

		case ELF_EXTENT_PHDR:
			if ((nrc = _libelf_write_phdr(e, newfile, ex)) < 0)
				goto error;
			break;

		case ELF_EXTENT_SECTION:
			if ((nrc = _libelf_write_scn(e, newfile, ex)) < 0)
				goto error;
			break;

		case ELF_EXTENT_SHDR:
			if ((nrc = _libelf_write_shdr(e, newfile, ex)) < 0)
				goto error;
			break;

		default:
			assert(0);
			break;
		}

		assert(ex->ex_start + ex->ex_size == (size_t) nrc);
		assert(rc < nrc);

		rc = nrc;
	}

	assert(rc == newsize);

	/*
	 * For regular files, throw away existing file content and
	 * unmap any existing mappings.
	 */
	if ((e->e_flags & LIBELF_F_SPECIAL_FILE) == 0 && e->e_fd != -1) {
#if !defined(WIN32)
#define FTRUNC(A, B) ftruncate(A, (off_t)B)
#else
#define FTRUNC(A, B) _chsize(A, B)
#endif
		if (FTRUNC(e->e_fd, 0) < 0 ||
		    lseek(e->e_fd, (off_t) 0, SEEK_SET)) {
			LIBELF_SET_ERROR(IO, errno);
			goto error;
		}
		if (e->e_flags & LIBELF_F_RAWFILE_MMAP) {
			assert(e->e_rawfile != NULL);
			assert(e->e_cmd == ELF_C_RDWR);
			if (munmap(e->e_rawfile, e->e_rawsize) < 0) {
				LIBELF_SET_ERROR(IO, errno);
				goto error;
			}
		}
	}

	/*
	 * Write out the new contents.
	 */
	if (e->e_fd != -1 && write(e->e_fd, newfile, (size_t) newsize) != newsize) {
		LIBELF_SET_ERROR(IO, errno);
		goto error;
	}

	/*
	 * For files opened in ELF_C_RDWR mode, set up the new 'raw'
	 * contents.
	 */
  if (e->e_cmd == ELF_C_RDWR) {
    if (e->e_rawfile) {
      if (e->e_flags & LIBELF_F_RAWFILE_MMAP && e->e_fd >= 0) {
        if ((e->e_rawfile = mmap(NULL, (size_t) newsize,
                PROT_READ, MAP_PRIVATE, e->e_fd, (off_t) 0)) ==
            MAP_FAILED) {
          LIBELF_SET_ERROR(IO, errno);
          goto error;
        }
      } else if (e->e_flags & LIBELF_F_RAWFILE_MALLOC) {
        e->e_mem.dealloc(e->e_rawfile);
        e->e_rawfile = newfile;
        newfile = NULL;
      }
    } else {
      e->e_rawfile = newfile;
      newfile = NULL;
      e->e_flags |= LIBELF_F_RAWFILE_MALLOC;
    }
    /* Record the new size of the file. */
    e->e_rawsize = newsize;
  } else {
    /* File opened in ELF_C_WRITE mode. */
    assert(e->e_rawfile == NULL);
  }

  /* Free the temporary buffer. */
  if (newfile)
    e->e_mem.dealloc(newfile);

  return (rc);

error:
  e->e_mem.dealloc(newfile);

  return ((off_t) -1);
}

/*
 * Write out the file image.
 *
 * The original file could have been mapped in with an ELF_C_RDWR
 * command and the application could have added new content or
 * re-arranged its sections before calling elf_update().  Consequently
 * its not safe to work `in place' on the original file.  So we
 * malloc() the required space for the updated ELF object and build
 * the object there and write it out to the underlying file at the
 * end.  Note that the application may have opened the underlying file
 * in ELF_C_RDWR and only retrieved/modified a few sections.  We take
 * care to avoid translating file sections unnecessarily.
 *
 * Gaps in the coverage of the file by the file's sections will be
 * filled with the fill character set by elf_fill(3).
 */

static off_t
_libelf_write_elf(Elf *e, off_t newsize, struct _Elf_Extent_List *extents)
{
	off_t rc;
	Elf_Scn *scn, *tscn;

  rc = _libelf_update_elf(e, newsize, extents);
  if (rc == (off_t)-1)
    return (rc);

	/*
	 * Reset flags, remove existing section descriptors and
	 * {E,P}HDR pointers so that a subsequent elf_get{e,p}hdr()
	 * and elf_getscn() will function correctly.
	 */

	e->e_flags &= ~ELF_F_DIRTY;

	STAILQ_FOREACH_SAFE(scn, &e->e_u.e_elf.e_scn, s_next, tscn)
		_libelf_release_scn(scn);

	if (e->e_class == ELFCLASS32) {
		e->e_mem.dealloc(e->e_u.e_elf.e_ehdr.e_ehdr32);
		if (e->e_u.e_elf.e_phdr.e_phdr32)
			e->e_mem.dealloc(e->e_u.e_elf.e_phdr.e_phdr32);

		e->e_u.e_elf.e_ehdr.e_ehdr32 = NULL;
		e->e_u.e_elf.e_phdr.e_phdr32 = NULL;
	} else {
		e->e_mem.dealloc(e->e_u.e_elf.e_ehdr.e_ehdr64);
		if (e->e_u.e_elf.e_phdr.e_phdr64)
			e->e_mem.dealloc(e->e_u.e_elf.e_phdr.e_phdr64);

		e->e_u.e_elf.e_ehdr.e_ehdr64 = NULL;
		e->e_u.e_elf.e_phdr.e_phdr64 = NULL;
	}

	return (rc);
}

/*
 * Update an ELF object.
 */

off_t
elf_update(Elf *e, Elf_Cmd c)
{
	int ec;
	off_t rc;
	struct _Elf_Extent_List extents;

	rc = (off_t) -1;

	if (e == NULL || e->e_kind != ELF_K_ELF ||
	    (c != ELF_C_NULL && c != ELF_C_WRITE
       && c != ELF_C_RDWR)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (rc);
	}

	if ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64) {
		LIBELF_SET_ERROR(CLASS, 0);
		return (rc);
	}

	if (e->e_version == EV_NONE)
		e->e_version = EV_CURRENT;

	if (c == ELF_C_WRITE && e->e_cmd == ELF_C_READ) {
		LIBELF_SET_ERROR(MODE, 0);
		return (rc);
	}

	ELF_SLIST_INIT(&extents);

	if ((rc = _libelf_resync_elf(e, &extents)) < 0)
		goto done;

	if (c == ELF_C_NULL)
		goto done;

	if (c == ELF_C_WRITE && e->e_fd == -1) {
		rc = (off_t) -1;
		LIBELF_SET_ERROR(SEQUENCE, 0);
		goto done;
	}

  if (c == ELF_C_RDWR) {
    rc = _libelf_update_elf(e, rc, &extents);
  } else {
    rc = _libelf_write_elf(e, rc, &extents);
  }

done:
	_libelf_release_extents(&extents);
	return (rc);
}

