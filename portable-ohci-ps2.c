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
 *  $Id: portable-ohci-ps2.c,v 1.4 2002/01/10 11:04:26 glevand Exp $
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/ps2/irq.h>			// for IRQ_SBUS_USB.

#include "portable-ohci-ps2.h"
#include "portable-ohci.h"

// need to move all the PS2 specific stuff in here...

#define LOCAL_MEM_UNIT   (64 * 1024)
#define MAX_LOCAL_MEM    (1024 * 1024)
#define QUAD_COPY_CHUNK_SIZE 64

struct mb 
{
    char *p;
    int size;
    struct mb *next;
};

static int GrowLocalMem(void);
static void CleanupLocalMem(void);

static void *local_mems[MAX_LOCAL_MEM/LOCAL_MEM_UNIT];
static int nlocal_mems = 0;
static int insufficient_local_mem = 0;
static spinlock_t local_mem_lock = SPIN_LOCK_UNLOCKED;

static struct mb* FreeList = NULL;
static struct mb* AllocList = NULL;

//static int total_size = 0;
//static int peak_size = 0;

static __inline void QuadCopy(void* pDest, const void* pSrc, unsigned int uBytes)
{
	__asm__ __volatile__(
		" 	# QuadCopy				\n\t"
		" 							\n\t" 	
		" 	.set push;				\n\t"
		" 	.set mips3;				\n\t"
		" 	.set noat;				\n\t"
		" 							\n\t" 	
		" 	addu $8, %2, %1;		\n\t" 	// pEnd = pSrc + uBytes
		" 							\n\t" 	
		" 	1:						\n\t"
		" 	lq $9,  0(%1);			\n\t" 	
		" 	lq $10, 16(%1);			\n\t"
		" 	lq $11, 32(%1);			\n\t" 	
		" 	lq $12, 48(%1);			\n\t"
		" 							\n\t" 	
		" 	sq $9,  0(%0);			\n\t" 	
		" 	sq $10, 16(%0);			\n\t"
		" 	sq $11, 32(%0);			\n\t" 
		" 	sq $12, 48(%0);			\n\t"	// *pDest = *pSrc
		" 							\n\t" 	
		" 	addiu %1, 64;			\n\t" 	// pSrc++
		" 	.set noreorder;			\n\t"
		" 	bne %1, $8, 1b;			\n\t"	// if (pSrc != pEnd) continue
		" 	addiu %0, 64;			\n\t" 	// pDest++
		" 	.set reorder;			\n\t"
		" 							\n\t" 	
		" 	.set pop;				\n\t"

		: 												// output 
		: "r"(pDest), "r"(pSrc), "r"(uBytes)			// input 
		: "memory", "$8", "$9", "$10", "$11", "$12"		// clobbers  
	);
}//--------------------------------------------------------------------------------------
static int GrowLocalMem(void)
{
    struct mb *newitem;
    unsigned int flags;

    if (MAX_LOCAL_MEM/LOCAL_MEM_UNIT < nlocal_mems) 
    {
		printk(KERN_WARNING __FILE__ " (%d):" __FUNCTION__ ": failed.\n", __LINE__);
		return (-ENOMEM);
    }

    newitem = (struct mb *)kmalloc(sizeof(struct mb), GFP_KERNEL);

    if (!newitem) 
    {
		printk(KERN_WARNING __FILE__ " (%d):" __FUNCTION__ ": failed.\n", __LINE__);
		return (-ENOMEM);
    }

    local_mems[nlocal_mems] = (void *)ps2sif_allociopheap(LOCAL_MEM_UNIT);
    if (!local_mems[nlocal_mems]) 
    {
		printk(KERN_WARNING __FILE__ " (%d):" __FUNCTION__ ": failed.\n", __LINE__);
		kfree(newitem);
		return (-ENOMEM);
    }
    
    printk(KERN_DEBUG __FILE__ ": GrowLocalMem %dK bytes\n", LOCAL_MEM_UNIT/1024);

    newitem->p = ps2sif_bustovirt((unsigned)local_mems[nlocal_mems]);
    newitem->size = LOCAL_MEM_UNIT;

    spin_lock_irqsave(&local_mem_lock, flags);
    
    newitem->next = FreeList;
    nlocal_mems++;
    FreeList = newitem;
    
    spin_unlock_irqrestore (&local_mem_lock, flags);

    return 0;
}//--------------------------------------------------------------------------------------
static void CleanupLocalMem(void)
{
    while (0 < nlocal_mems) 
    {
		ps2sif_freeiopheap((void *)local_mems[--nlocal_mems]);
    }
}//--------------------------------------------------------------------------------------
int FreeLocalMem(void *p)
{
    struct mb *current, *prev, *current2;
    unsigned int flags;

	if(!p)
	{
		return 0;
	}

    spin_lock_irqsave(&local_mem_lock, flags);
    current = AllocList;
    prev = NULL;
    while (current) {
	if (current->p == p) {
	    current2 = FreeList;
	    while (current2) {
		if (current2->p + current2->size == current->p) {
		    current2->size += current->size;
		    if (prev) prev->next = current->next;
		    else AllocList = current->next;
//total_size -= current->size;
		    spin_unlock_irqrestore (&local_mem_lock, flags);
		    kfree(current);
		    return(1);
		}
		current2 = current2->next;
	    }
	    if (prev) prev->next = current->next;
	    else AllocList = current->next;
	    current->next = FreeList;
	    FreeList = current;
//total_size -= current->size;
	    spin_unlock_irqrestore (&local_mem_lock, flags);
	    return(1);
	}
	prev = current;
	current = current->next;
    }
    spin_unlock_irqrestore (&local_mem_lock, flags);
    return(0);
}//--------------------------------------------------------------------------------------
void *AllocLocalMem(int size)
{
    int size256 = ((size + 255)/256)*256;
    struct mb *current, *prev, *new;
    unsigned int flags;
    int do_retry = 1;

retry:
    if (!in_interrupt() && insufficient_local_mem) 		
    { 													
		insufficient_local_mem = 0; 					
		GrowLocalMem(); 								
    } 													

    spin_lock_irqsave(&local_mem_lock, flags);
    current = FreeList;
    prev = NULL;
    while (current) {
	if (current->size == size256) {
	    if (prev) prev->next = current->next;
	    else FreeList = current->next;
	    current->next = AllocList;
	    AllocList = current;
//total_size += size256;
//if (total_size > peak_size) {
//	peak_size = total_size;
//	printk("new peak_size = %d\n", peak_size);
//}
	    spin_unlock_irqrestore (&local_mem_lock, flags);
	    return(current->p);
	} else if (current->size > size256) {
	    char *p = current->p + (current->size - size256);
	    current->size -= size256;
	    spin_unlock_irqrestore (&local_mem_lock, flags);
	    new = (struct mb *)kmalloc(sizeof(struct mb),
			in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	    if (!new) {
		return NULL;
	    }
	    new->p = p;
	    new->size = size256; 
	    spin_lock_irqsave(&local_mem_lock, flags);
	    new->next = AllocList;
	    AllocList = new;
//total_size += size256;
//if (total_size > peak_size) {
//	peak_size = total_size;
//	printk("new peak_size = %d\n", peak_size);
//}
	    spin_unlock_irqrestore (&local_mem_lock, flags);
	    return(new->p);
	}
	prev = current;
	current = current->next;
    }
    insufficient_local_mem++;
    spin_unlock_irqrestore (&local_mem_lock, flags);

    if (do_retry) {
	do_retry = 0;
	goto retry;
    }

    return(NULL);
}//--------------------------------------------------------------------------------------

int Portable_HcdInit(void)
{
	return hc_found_ohci(NULL, IRQ_SBUS_USB, (void *)0x1f801600);
}//--------------------------------------------------------------------------------------
struct ohci* Portable_AllocateOhci(void)
{
	unsigned long addr;

	if(GrowLocalMem() != 0)
	{
		return NULL;
	}

	addr = (unsigned long)ps2sif_allociopheap(sizeof(struct ohci));
	
	if(!addr) 
	{
		CleanupLocalMem();

		kfree(FreeList);
		FreeList = NULL;

		return NULL;
	}

	return (struct ohci *)ps2sif_bustovirt(addr);
}//--------------------------------------------------------------------------------------
void Portable_FreeOhci(struct ohci* ohci)
{
	struct mb* current;
	struct mb* next;

	if(ohci)
	{
		ps2sif_freeiopheap((void*)ps2sif_virttobus(ohci));
	}

	CleanupLocalMem();

	for(current = FreeList; current; current = next) 
	{
		next = current->next;
		kfree(current);
	}

	FreeList = NULL;

	for(current = AllocList; current; current = next) 
	{
		next = current->next;
		kfree(current);
	}

	AllocList = NULL;
}//--------------------------------------------------------------------------------------
struct ohci_device* Portable_AllocateDevice(void)
{
	const unsigned long addr = (unsigned long)ps2sif_allociopheap(sizeof(struct ohci_device));
	
	return addr ? (struct ohci_device *)ps2sif_bustovirt(addr) : NULL;
}//--------------------------------------------------------------------------------------
void Portable_FreeDevice(struct ohci_device* dev)
{
	if(dev)
	{
		ps2sif_freeiopheap((void*)ps2sif_virttobus(dev));
	}
}//--------------------------------------------------------------------------------------
int Portable_PrepareUrbOnSubmit(urb_t* urb)
{
	Portable_Extra_t*const p = &((urb_priv_t*)(urb->hcpriv))->portable;
	
	if(urb->transfer_buffer_length) 
	{
		if(((unsigned long)(urb->transfer_buffer) & 0x1f) == 0) // 128 bit aligned - this usually hits.
		{
			const unsigned int uBytes = ((urb->transfer_buffer_length + QUAD_COPY_CHUNK_SIZE) & ~(QUAD_COPY_CHUNK_SIZE - 1));

			p->transfer_buffer = AllocLocalMem(uBytes);

			if(!p->transfer_buffer) 
			{
				return -ENOMEM;
			}

			QuadCopy(p->transfer_buffer, urb->transfer_buffer, uBytes);
		}
		else
		{
			p->transfer_buffer = AllocLocalMem(urb->transfer_buffer_length);

			if(!p->transfer_buffer) 
			{
				return -ENOMEM;
			}

			memcpy(p->transfer_buffer, urb->transfer_buffer, urb->transfer_buffer_length);
		}
	}

	if(urb->setup_packet) 
	{
		p->setup_packet = AllocLocalMem(8);
		
		if(!p->setup_packet) 
		{
			return -ENOMEM;
		}

		if(((unsigned long)(urb->setup_packet) & 0x0f) == 0)				// 64 bit aligned.
		{
			*(long long*)(p->setup_packet) = *(const long long*)(urb->setup_packet);
		}
		else
		{
			memcpy(p->setup_packet, urb->setup_packet, 8);
		}
	}

	return 0;
}//--------------------------------------------------------------------------------------
void Portable_PrepareUrbOnReturn(urb_t* urb)
{
	Portable_Extra_t*const p = &((urb_priv_t*)(urb->hcpriv))->portable;
	
	if(usb_pipein(urb->pipe) && urb->actual_length) 
	{
		memcpy(urb->transfer_buffer, p->transfer_buffer, urb->actual_length);
	}
}//--------------------------------------------------------------------------------------
void Portable_FreeUrb(urb_t* urb)
{
	Portable_Extra_t*const p = &((urb_priv_t*)(urb->hcpriv))->portable;
	
	FreeLocalMem(p->transfer_buffer);
	p->transfer_buffer = NULL;

	FreeLocalMem(p->setup_packet);
	p->setup_packet = NULL;
}//--------------------------------------------------------------------------------------

//=====| end of file |===================================================================

