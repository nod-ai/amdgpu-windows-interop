/*-
 * Copyright (c) 2006,2008 Joseph Koshy
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS `AS IS' AND
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

#include <libelf.h>

#include "_libelf.h"

LIBELF_VCSID("$Id: libelf_fsize.m4 320 2009-03-07 16:37:53Z jkoshy $");

/* WARNING: GENERATED FROM libelf_fsize.m4. */

/*
 * Create an array of file sizes from the elf_type definitions
 */

struct fsize {
	size_t fsz32;
	size_t fsz64;
};

static struct fsize fsize[ELF_T_NUM] = {
#if defined(__GNUC__)
#if	LIBELF_CONFIG_ADDR
    [ELF_T_ADDR] = { .fsz32 = sizeof(Elf32_Addr), .fsz64 = sizeof(Elf64_Addr) },
#endif
#if	LIBELF_CONFIG_BYTE
    [ELF_T_BYTE] = { .fsz32 = 1, .fsz64 = 1 },
#endif
#if	LIBELF_CONFIG_CAP
    [ELF_T_CAP] = { .fsz32 = sizeof(Elf32_Word)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+0 },
#endif
#if	LIBELF_CONFIG_DYN
    [ELF_T_DYN] = { .fsz32 = sizeof(Elf32_Sword)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Sxword)+sizeof(Elf64_Xword)+0 },
#endif
#if	LIBELF_CONFIG_EHDR
    [ELF_T_EHDR] = { .fsz32 = EI_NIDENT+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Word)+sizeof(Elf32_Addr)+sizeof(Elf32_Off)+sizeof(Elf32_Off)+sizeof(Elf32_Word)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+0, .fsz64 = EI_NIDENT+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Word)+sizeof(Elf64_Addr)+sizeof(Elf64_Off)+sizeof(Elf64_Off)+sizeof(Elf64_Word)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+0 },
#endif
#if	LIBELF_CONFIG_GNUHASH
    [ELF_T_GNUHASH] = { .fsz32 = 1, .fsz64 = 1 },
#endif
#if	LIBELF_CONFIG_HALF
    [ELF_T_HALF] = { .fsz32 = sizeof(Elf32_Half), .fsz64 = sizeof(Elf64_Half) },
#endif
#if	LIBELF_CONFIG_LWORD
    [ELF_T_LWORD] = { .fsz32 = sizeof(Elf32_Lword), .fsz64 = sizeof(Elf64_Lword) },
#endif
#if	LIBELF_CONFIG_MOVE
    [ELF_T_MOVE] = { .fsz32 = sizeof(Elf32_Lword)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+0, .fsz64 = sizeof(Elf64_Lword)+sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+0 },
#endif
#if	LIBELF_CONFIG_MOVEP
    [ELF_T_MOVEP] = { .fsz32 = 0, .fsz64 = 0 },
#endif
#if	LIBELF_CONFIG_NOTE
    [ELF_T_NOTE] = { .fsz32 = 1, .fsz64 = 1 },
#endif
#if	LIBELF_CONFIG_OFF
    [ELF_T_OFF] = { .fsz32 = sizeof(Elf32_Off), .fsz64 = sizeof(Elf64_Off) },
#endif
#if	LIBELF_CONFIG_PHDR
    [ELF_T_PHDR] = { .fsz32 = sizeof(Elf32_Word)+sizeof(Elf32_Off)+sizeof(Elf32_Addr)+sizeof(Elf32_Addr)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Word)+sizeof(Elf64_Word)+sizeof(Elf64_Off)+sizeof(Elf64_Addr)+sizeof(Elf64_Addr)+sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+0 },
#endif
#if	LIBELF_CONFIG_REL
    [ELF_T_REL] = { .fsz32 = sizeof(Elf32_Addr)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Addr)+sizeof(Elf64_Xword)+0 },
#endif
#if	LIBELF_CONFIG_RELA
    [ELF_T_RELA] = { .fsz32 = sizeof(Elf32_Addr)+sizeof(Elf32_Word)+sizeof(Elf32_Sword)+0, .fsz64 = sizeof(Elf64_Addr)+sizeof(Elf64_Xword)+sizeof(Elf64_Sxword)+0 },
#endif
#if	LIBELF_CONFIG_SHDR
    [ELF_T_SHDR] = { .fsz32 = sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Addr)+sizeof(Elf32_Off)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Word)+sizeof(Elf64_Word)+sizeof(Elf64_Xword)+sizeof(Elf64_Addr)+sizeof(Elf64_Off)+sizeof(Elf64_Xword)+sizeof(Elf64_Word)+sizeof(Elf64_Word)+sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+0 },
#endif
#if	LIBELF_CONFIG_SWORD
    [ELF_T_SWORD] = { .fsz32 = sizeof(Elf32_Sword), .fsz64 = sizeof(Elf64_Sword) },
#endif
#if	LIBELF_CONFIG_SXWORD
    [ELF_T_SXWORD] = { .fsz32 = 0, .fsz64 = sizeof(Elf64_Sxword) },
#endif
#if	LIBELF_CONFIG_SYMINFO
    [ELF_T_SYMINFO] = { .fsz32 = sizeof(Elf32_Half)+sizeof(Elf32_Half)+0, .fsz64 = sizeof(Elf64_Half)+sizeof(Elf64_Half)+0 },
#endif
#if	LIBELF_CONFIG_SYM
    [ELF_T_SYM] = { .fsz32 = sizeof(Elf32_Word)+sizeof(Elf32_Addr)+sizeof(Elf32_Word)+1+1+sizeof(Elf32_Half)+0, .fsz64 = sizeof(Elf64_Word)+1+1+sizeof(Elf64_Half)+sizeof(Elf64_Addr)+sizeof(Elf64_Xword)+0 },
#endif
#if	LIBELF_CONFIG_VDEF
    [ELF_T_VDEF] = { .fsz32 = sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Word)+sizeof(Elf64_Word)+sizeof(Elf64_Word)+0 },
#endif
#if	LIBELF_CONFIG_VNEED
    [ELF_T_VNEED] = { .fsz32 = sizeof(Elf32_Half)+sizeof(Elf32_Half)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+sizeof(Elf32_Word)+0, .fsz64 = sizeof(Elf64_Half)+sizeof(Elf64_Half)+sizeof(Elf64_Word)+sizeof(Elf64_Word)+sizeof(Elf64_Word)+0 },
#endif
#if	LIBELF_CONFIG_WORD
    [ELF_T_WORD] = { .fsz32 = sizeof(Elf32_Word), .fsz64 = sizeof(Elf64_Word) },
#endif
#if	LIBELF_CONFIG_XWORD
    [ELF_T_XWORD] = { .fsz32 = 0, .fsz64 = sizeof(Elf64_Xword) },
#endif
#elif defined(_MSC_VER)
   {4, 8}, {1, 1}, {0, 0}, {8, 16}, {52, 64},
   {2, 2}, {0, 0}, {0, 0}, {0, 0}, {1, 1},
   {4, 8}, {32, 56}, {8, 16}, {12, 24}, {40, 64},
   {4, 4}, {0, 8}, {0, 0}, {16, 24}, {20, 20},
   {16, 16}, {4, 4}, {0, 8}, {1, 1}
#else
#error
#endif
};

size_t
_libelf_fsize(Elf_Type t, int ec, unsigned int v, size_t c)
{
	size_t sz;

	sz = 0;
	if (v != EV_CURRENT)
		LIBELF_SET_ERROR(VERSION, 0);
	else if ((int) t < ELF_T_FIRST || t > ELF_T_LAST)
		LIBELF_SET_ERROR(ARGUMENT, 0);
	else {
		sz = ec == ELFCLASS64 ? fsize[t].fsz64 : fsize[t].fsz32;
		if (sz == 0)
			LIBELF_SET_ERROR(UNIMPL, 0);
	}

	return (sz*c);
}

