/* SCEI_SYM_OWNER */
/*
 *  PlayStation 2 USB controller driver
 *
 *  Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 *  Based on URB OHCI HCD (Host Controller Driver) for USB.
 *  (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *  (C) Copyright 1999 Linus Torvalds  
 *  (C) Copyright 1999 Gregory P. Smith
 *
 *  $Id: portable-ohci-ps2.h,v 1.3 2001/12/17 03:37:47 glevand Exp $
 */

// Only platform dependent preprocessor macros and inline function
// definitions that need to be included in the core translation unit for
// reasons of efficiency should be included here.

#if !defined(H__19F5591A_088B_43F0_B412_C8094604893D__INCLUDED_)
#define H__19F5591A_088B_43F0_B412_C8094604893D__INCLUDED_
#include <linux/config.h>
#if !defined(CONFIG_PS2) || !defined(CONFIG_USB_PS2_OHCI)
# error "PlayStation2 Linux only."
#endif
#include <asm/ps2/sifdefs.h>		// for ps2sif_XXX().


typedef struct 
{
	void *transfer_buffer;
	void *setup_packet;
} Portable_Extra_t;

int FreeLocalMem(void *p);
void* AllocLocalMem(int size);

#undef OHCI_FREE
#define OHCI_FREE(buf) FreeLocalMem(buf)

#undef OHCI_ALLOC
#define OHCI_ALLOC(buf, size) (buf) = AllocLocalMem(size)

#undef virt_to_bus
#define virt_to_bus(addr) ps2sif_virttobus(addr)
#undef bus_to_virt
#define bus_to_virt(addr) ps2sif_bustovirt(addr)

static __inline void sync_and_flush(const void* p)
{                                                       
    volatile int temp;                                     
                                                           
    __asm__ __volatile__(
		" 	# sync_and_flush		\n\t"
		" 							\n\t" 	
		"	.set push       		\n\t"            
		"	.set noreorder  		\n\t"            
		"	.set noat       		\n\t"            
		" 							\n\t" 	
		"	sync.l          		\n\t"            
		" 							\n\t" 	
		"	.set pop        		\n\t"            
		:                  					// output  		              
		:                  					// input                
		: "memory");       					// clobbers                 
                                                           
    temp = *(volatile int*)((unsigned long)(p) & ~3);      
}//--------------------------------------------------------------------------------------

#endif // INCLUDED.
//=====| end of file |===================================================================

