/*
************************************************************************************************************************
*
*  Copyright (C) 2020 Advanced Micro Devices, Inc.  All rights reserved.
*
***********************************************************************************************************************/

#if !defined(_LP64)
#define	ELFTC_CLASS	ELFCLASS32
#else
#define	ELFTC_CLASS	ELFCLASS64
#endif
#define	ELFTC_ARCH	EM_386
#define	ELFTC_BYTEORDER	ELFDATA2LSB
