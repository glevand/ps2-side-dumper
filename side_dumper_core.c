/* SCEI_SYM_OWNER */
/*
 *  SideDumper, a general purpose dumper for USB packets.
 *
 *  Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 *  $Id: side_dumper_core.c,v 1.2 2002/01/10 11:04:26 glevand Exp $
 */

#include <linux/config.h>
#if defined (CONFIG_USB_SIDE_DUMPER)
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/list.h>
#include <linux/ctype.h>		// for isprint().
#include "side_dumper.h"
#include "side_dumper_procfs.h"

typedef struct 
{
	unsigned int requesttype;
	unsigned int request;
	unsigned int value;
	unsigned int index;
	unsigned int length;
} TSetupPacket;

typedef struct 
{
	pid_t pid;
	volatile int oContinue;
	struct semaphore semExitSignal;
} TThreadControl;

typedef struct 
{
	struct list_head link;
	SideDumper_TMiniUrb Mini;
} TWorkItem;

typedef struct 
{
	struct list_head list;
	struct semaphore semListAccess;
	struct semaphore semItemsQueued;
} TWorkQueue;

typedef struct
{
	unsigned int uUrbNumGenerator;
	TThreadControl Thread;
	TWorkQueue Queue;
	SideDumper_TDeviceSettings aDevSettings[16]; // first 15 devices only.
} TInst;

// top level.
static int WorkerThreadEntry(TInst* pInst);
static void QueueUrb(const urb_t* urb);
static void ProcessUrb(const urb_t* urb);

// level 1 dumpers.
static void DumpUrbToPrintk(const SideDumper_TDeviceSettings* pSettings, const SideDumper_TMiniUrb* pMini);
// level 2 dumpers.
static unsigned int DumpUrb(const SideDumper_TDeviceSettings* pSettings, const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut);
// level 3 dumpers.
static unsigned int DumpSPBytes(const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut);
static unsigned int DumpSPSettings(const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut);
static unsigned int DumpTB(const SideDumper_TDeviceSettings* pSettings, const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut);
static unsigned int DumpStdDescriptors(const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut);
// level 4 dumpers.
static unsigned int DumpDD(const char* pszHeader, const struct usb_device_descriptor* pDesc, unsigned int uDescLen, char* pszOut);
static unsigned int DumpCD(const char* pszHeader, const struct usb_config_descriptor* pDesc, unsigned int uDescLen, char* pszOut);
static unsigned int DumpSD(const char* pszHeader, const struct usb_string_descriptor* pDesc, unsigned int uDescLen, unsigned int uIndex, char* pszOut);
static unsigned int DumpID(const char* pszHeader, const struct usb_interface_descriptor* pDesc, unsigned int uDescLen, char* pszOut);
static unsigned int DumpED(const char* pszHeader, const struct usb_endpoint_descriptor* pDesc, unsigned int uDescLen, char* pszOut);
// level 5 dumpers.
static unsigned int DumpRawBuf(const char* pszHeader, const void* pvBuf, unsigned int uBytes, char* pszOut);

// utils.
static __inline const char* PipeTypeStr(unsigned int pipe);
static __inline int TestForStdDescriptor(const SideDumper_TMiniUrb* pMini);
static __inline unsigned int GetStringIndex(const SideDumper_TMiniUrb* pMini);
static __inline unsigned int min(unsigned int a, unsigned int b);
static __inline const char* FindEol(const char* psz);

// mini.
static void InitMini(SideDumper_TMiniUrb* pMini, const urb_t* urb);
static void ProcessMini(const SideDumper_TMiniUrb* pMini);
static void CleanMini(SideDumper_TMiniUrb* pMini);

static TInst g_Inst =
{
	Thread:	
	{
		pid: 0,
	},
	aDevSettings:
	{
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX}, 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 1
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 2 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 3
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 4 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 5
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 6 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 7 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 8 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 9
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 10 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 11
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 12 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 13
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX},// 14 
		{0, 0, DumpUrbToPrintk, UINT_MAX, UINT_MAX} // 15
	},
};

//====| static inlines |=================================================================
static __inline const char* PipeTypeStr(unsigned int pipe)
{
	switch(usb_pipetype(pipe)) 
	{
		case PIPE_ISOCHRONOUS:
			return "iso";
  				
  		case PIPE_INTERRUPT:
			return "intr";
  			
		case PIPE_CONTROL:
			return "ctrl";

		case PIPE_BULK:
			return "bulk";

		default:
			break;
	}

	return "?";
}//--------------------------------------------------------------------------------------
static __inline int TestForStdDescriptor(const SideDumper_TMiniUrb* pMini)
{
	// Test if TB contains any standard discriptors.

	const devrequest*const pDesc = (const devrequest*)(pMini->setup_packet);

	return (usb_pipetype(pMini->pipe) == PIPE_CONTROL
		&& (pDesc->requesttype & 0x60) == USB_TYPE_STANDARD
		&& pDesc->request == USB_REQ_GET_DESCRIPTOR);
}//--------------------------------------------------------------------------------------
static __inline unsigned int GetStringIndex(const SideDumper_TMiniUrb* pMini)
{
	const devrequest*const pDesc = (const devrequest*)(pMini->setup_packet);

	return (unsigned int)(unsigned char)(pDesc->value & 0xff);
}//--------------------------------------------------------------------------------------
static __inline unsigned int min(unsigned int a, unsigned int b)
{
	return (a < b) ? a : b;
}//--------------------------------------------------------------------------------------
static __inline const char* FindEol(const char* psz)
{
	while(*psz != 0 && *psz != '\n')
	{
		psz++;
	}

	return psz;
}//--------------------------------------------------------------------------------------

//====| static scope |===================================================================
static int WorkerThreadEntry(TInst* pInst)
{
	lock_kernel();

	current->priority /= 4;

	daemonize();
	strcpy(current->comm, "side_dumper");

	unlock_kernel();

	while(1) 
	{
		TWorkItem* pItem;

		down(&(pInst->Queue.semItemsQueued));
		
		if(!pInst->Thread.oContinue)
		{
			break;	// main thread should clean pending items.
		}

		down(&(pInst->Queue.semListAccess));

        if(list_empty(&pInst->Queue.list))
		{
			up(&(pInst->Queue.semListAccess));

			printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": list empty.\n", __LINE__);
		}
		else
		{
			pItem = list_entry(pInst->Queue.list.next, TWorkItem, link);

			list_del(&pItem->link);

			up(&(pInst->Queue.semListAccess));

			ProcessMini(&pItem->Mini);
			CleanMini(&pItem->Mini);

			kfree(pItem);
		}
	}

	printk(KERN_DEBUG __FILE__ " (%d):" __FUNCTION__ ": exit.\n", __LINE__);

	up(&(pInst->Thread.semExitSignal));

	return 0;
}//--------------------------------------------------------------------------------------
static void DumpUrbToPrintk(const SideDumper_TDeviceSettings* pSettings, const SideDumper_TMiniUrb* pMini)
{
	static const unsigned int uStrLen = (16 * 1024);
	unsigned int uBytes;
	static unsigned int uBytesOut = 0;
	char* pChunk;
	char* pNextChunk;
	char*const pszOut = (char*)kmalloc(uStrLen, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);

	if(pszOut == NULL)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": out of memory.\n", __LINE__);
		return;
	}

	pszOut[0] = 0;

	uBytes = DumpUrb(pSettings, KERN_DEBUG "side_dumper|", pMini, pszOut);
	
	if(uBytes >= uStrLen)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": Warning - buffer overrun.\n", __LINE__);
		uBytes = uStrLen - 1;
		pszOut[uStrLen - 1] = 0;
	}

	for(pChunk = pszOut; ; pChunk = pNextChunk) 
	{
		char c;

		pNextChunk = (char*)(FindEol(pChunk));			// break into lines.

		if(*pNextChunk == 0)							// end of data.
		{
			printk(pChunk);
			break;
		}

		pNextChunk++;

		c = *pNextChunk;
		*pNextChunk = 0;
		printk(pChunk);
		*pNextChunk = c;

		if(pSettings->oAsync)
		{
			uBytesOut += pNextChunk - pChunk;

			if(uBytesOut < (15 * 1024))			
			{
				current->policy |= SCHED_YIELD; 		// yield to main thread after every chunk.	
				schedule();
			}
			else
			{
				uBytesOut = 0;
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(HZ / 4);				// delay 250ms every 15k to allow the log to be processed.
			}
		}
	}

	kfree(pszOut);
}//--------------------------------------------------------------------------------------
static unsigned int DumpUrb(const SideDumper_TDeviceSettings* pSettings, const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut)
{
	const char*const pszPin = pszOut;
	const unsigned int uTbLen = usb_pipeout(pMini->pipe) ? pMini->transfer_buffer_length : pMini->actual_length;

	
	pszOut += sprintf(pszOut, "%s urb_%04u: device #%d, %s_%u %c, %u bytes.\n", 
		pszHeader, 
		pMini->uUrbNumber, 
		pMini->devnum,
		PipeTypeStr(pMini->pipe),
		(unsigned int)(usb_pipeendpoint(pMini->pipe)), 
		usb_pipeout(pMini->pipe) ? '>' : '<', 
		uTbLen);
	
	switch(usb_pipetype(pMini->pipe)) 
	{
		case PIPE_ISOCHRONOUS:
			break;
  				
  		case PIPE_INTERRUPT:
  			break;
  			
		case PIPE_CONTROL:
			pszOut += DumpSPBytes(pszHeader, pMini, pszOut);
			pszOut += DumpSPSettings(pszHeader, pMini, pszOut);
			break;

		case PIPE_BULK:
			break;
	}

	pszOut += DumpTB(pSettings, pszHeader, pMini, pszOut);

	pszOut += DumpStdDescriptors(pszHeader, pMini, pszOut);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpSPBytes(const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut)
{
	// Dump the setup packet bytes.

	const char*const pszPin = pszOut;
	const TSetupPacket sp = 
	{
		((const devrequest*)(pMini->setup_packet))->requesttype,
		((const devrequest*)(pMini->setup_packet))->request,
		__le16_to_cpu(((const devrequest*)(pMini->setup_packet))->value),
		__le16_to_cpu(((const devrequest*)(pMini->setup_packet))->index),
		__le16_to_cpu(((const devrequest*)(pMini->setup_packet))->length)
	};

	if(usb_pipetype(pMini->pipe) != PIPE_CONTROL) 
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": wrong pipe type\n", __LINE__);
		return 0;
	}

	pszOut += sprintf(pszOut, "%s  sp = [ %02x %02x %04x %04x %04x ]\n", 
		pszHeader,
		sp.requesttype,
		sp.request,
		sp.value,
		sp.index,
		sp.length);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpSPSettings(const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut)
{
	// Dump the setup packet info.

	const char*const pszPin = pszOut;
	const TSetupPacket sp = 
	{
		((const devrequest*)(pMini->setup_packet))->requesttype,
		((const devrequest*)(pMini->setup_packet))->request,
		__le16_to_cpu(((const devrequest*)(pMini->setup_packet))->value),
		__le16_to_cpu(((const devrequest*)(pMini->setup_packet))->index),
		__le16_to_cpu(((const devrequest*)(pMini->setup_packet))->length)
	};

	if(usb_pipetype(pMini->pipe) != PIPE_CONTROL) 
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": wrong pipe type\n", __LINE__);
		return 0;
	}

	pszOut += sprintf(pszOut, "%s    %s, ", pszHeader, (sp.requesttype & USB_DIR_IN) ? "h<-d" : "h->d");

	switch(sp.requesttype & 0x60)
	{
		case USB_TYPE_STANDARD:
		{
			switch(sp.request)
			{
				case USB_REQ_GET_STATUS:		
					pszOut += sprintf(pszOut, "USB_REQ_GET_STATUS(@%u), ", sp.index);
					break;			
				case USB_REQ_CLEAR_FEATURE:		
					pszOut += sprintf(pszOut, "USB_REQ_CLEAR_FEATURE(%u@%u), ", sp.value, sp.index);
					break;	
				case USB_REQ_SET_FEATURE:		
					pszOut += sprintf(pszOut, "USB_REQ_SET_FEATURE(%u@%u), ", sp.value, sp.index);
					break;		
				case USB_REQ_SET_ADDRESS:		
					pszOut += sprintf(pszOut, "USB_REQ_SET_ADDRESS(@%u), ", sp.value);
					break;		
				case USB_REQ_GET_DESCRIPTOR:	
				case USB_REQ_SET_DESCRIPTOR:
					pszOut += sprintf(pszOut, "USB_REQ_%s_DESCRIPTOR", (sp.request == USB_REQ_GET_DESCRIPTOR ? "GET" : "SET"));
					switch(sp.value >> 8)
					{
						case USB_DT_DEVICE:	
							pszOut += sprintf(pszOut, "(USB_DT_DEVICE), ");
							break;	
						case USB_DT_CONFIG:		
							pszOut += sprintf(pszOut, "(USB_DT_CONFIG), ");
							break;	
						case USB_DT_STRING:		
							pszOut += sprintf(pszOut, "(USB_DT_STRING), ");
							break;	
						case USB_DT_INTERFACE: // possible???	
							pszOut += sprintf(pszOut, "(USB_DT_INTERFACE), ");
							break;	
						case USB_DT_ENDPOINT: // possible???		
							pszOut += sprintf(pszOut, "(USB_DT_ENDPOINT), ");
							break;	
						default:				
							pszOut += sprintf(pszOut, "(USB_DT_?), ");
							break;	
					}
					break;
				case USB_REQ_GET_CONFIGURATION:	
					pszOut += sprintf(pszOut, "USB_REQ_GET_CONFIGURATION, ");
					break;
				case USB_REQ_SET_CONFIGURATION:	
					pszOut += sprintf(pszOut, "USB_REQ_SET_CONFIGURATION(%u), ", sp.value);
					break;
				case USB_REQ_GET_INTERFACE:		
					pszOut += sprintf(pszOut, "USB_REQ_GET_INTERFACE(@%u), ", sp.index);
					break;		
				case USB_REQ_SET_INTERFACE:		
					pszOut += sprintf(pszOut, "USB_REQ_SET_INTERFACE(%u@%u), ", sp.value, sp.index); 
					break;
				case USB_REQ_SYNCH_FRAME:		
					pszOut += sprintf(pszOut, "USB_REQ_SYNCH_FRAME(@%u), ", sp.index);
					break;		
				default: 						
					pszOut += sprintf(pszOut, "USB_REQ_?, ");
					break;	
			}
			break;
		}
		case USB_TYPE_CLASS: 	
			pszOut += sprintf(pszOut, "USB_TYPE_CLASS, ");
			break;
		case USB_TYPE_VENDOR: 	
			pszOut += sprintf(pszOut, "USB_TYPE_VENDOR, ");
			break;
		default:				
			pszOut += sprintf(pszOut, "USB_TYPE_?, ");
			break;
	}

	switch(sp.requesttype & USB_RECIP_MASK)
	{
		case USB_RECIP_DEVICE: 		
			pszOut += sprintf(pszOut, "USB_RECIP_DEVICE\n");
			break;
		case USB_RECIP_INTERFACE: 	
			pszOut += sprintf(pszOut, "USB_RECIP_INTERFACE\n");
			break;
		case USB_RECIP_ENDPOINT: 	
			pszOut += sprintf(pszOut, "USB_RECIP_ENDPOINT\n");
			break;
		case USB_RECIP_OTHER: 		
			pszOut += sprintf(pszOut, "USB_RECIP_OTHER\n");
			break;
		default: 					
			pszOut += sprintf(pszOut, "USB_RECIP_?\n");
			break;
	}

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpTB(const SideDumper_TDeviceSettings* pSettings, const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut)
{
	// Dump the transfer buffer.

	char szTbHeader[64];

	const unsigned int uTbLen = usb_pipeout(pMini->pipe) 
		? min(pSettings->uOutLimit, pMini->transfer_buffer_length) 
		: min(pSettings->uInLimit, pMini->actual_length);

	if(uTbLen == 0)
	{
		return 0;
	}

	strcpy(szTbHeader, pszHeader);
	strcat(szTbHeader, "  tb =");

	return DumpRawBuf(szTbHeader, pMini->transfer_buffer, uTbLen, pszOut);
}//--------------------------------------------------------------------------------------
static unsigned int DumpStdDescriptors(const char* pszHeader, const SideDumper_TMiniUrb* pMini, char* pszOut)
{
	// Dump any descriptors.

	const char*const pszPin = pszOut;
	const struct usb_descriptor_header*const pDesc = (const struct usb_descriptor_header*)(pMini->transfer_buffer);
	const unsigned int uDescLen = usb_pipeout(pMini->pipe) ? pMini->transfer_buffer_length : pMini->actual_length;

	if(!TestForStdDescriptor(pMini))
	{
	 	return 0;
	}
	
	switch(pDesc->bDescriptorType)
	{
		case USB_DT_DEVICE:
			pszOut += DumpDD(pszHeader, (const struct usb_device_descriptor*)(pDesc), uDescLen, pszOut);
			break;	

		case USB_DT_CONFIG:
			pszOut += DumpCD(pszHeader, (const struct usb_config_descriptor*)(pDesc), uDescLen, pszOut);
			break;	

		case USB_DT_STRING:
			pszOut += DumpSD(pszHeader, (const struct usb_string_descriptor*)(pDesc), uDescLen, GetStringIndex(pMini), pszOut);
			break;	

		case USB_DT_INTERFACE:
		case USB_DT_ENDPOINT:
			printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": wrong descriptor type\n", __LINE__);
			break;	
				
		default:
			pszOut += sprintf(pszOut, "%s    unknown descriptor type %xh, %u bytes\n", 
				pszHeader, (unsigned int)pDesc->bDescriptorType, (unsigned int)pDesc->bLength);
			break;
	}

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpDD(const char* pszHeader, const struct usb_device_descriptor* pDesc, unsigned int uDescLen, char* pszOut)
{
	// Dump the device descriptor.

	const char*const pszPin = pszOut;

	pszOut += sprintf(pszOut, "%s    -- device descriptor -----\n", pszHeader);

	if(uDescLen < USB_DT_DEVICE_SIZE)
	{
		pszOut += sprintf(pszOut, "%s    short transfer (%d/%d bytes)\n", pszHeader, uDescLen, (unsigned int)pDesc->bLength);
	}
	else
	{
		pszOut += sprintf(pszOut, "%s    bLength            = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bLength, (unsigned int)pDesc->bLength);
		pszOut += sprintf(pszOut, "%s    bDescriptorType    = %02xh\n", pszHeader, (unsigned int)pDesc->bDescriptorType);
		pszOut += sprintf(pszOut, "%s    bcdUSB             = %04xh\n", pszHeader, (unsigned int)pDesc->bcdUSB);
		pszOut += sprintf(pszOut, "%s    bDeviceClass       = %02xh ", pszHeader, (unsigned int)pDesc->bDeviceClass);
		switch(pDesc->bDeviceClass) 
		{
			case 0: 						pszOut += sprintf(pszOut, "no class\n"); break;						
			case USB_CLASS_AUDIO: 			pszOut += sprintf(pszOut, "USB_CLASS_AUDIO\n"); break;
			case USB_CLASS_COMM: 			pszOut += sprintf(pszOut, "USB_CLASS_COMM\n"); break;
			case USB_CLASS_HID: 			pszOut += sprintf(pszOut, "USB_CLASS_HID\n"); break;
			case USB_CLASS_PRINTER: 		pszOut += sprintf(pszOut, "USB_CLASS_PRINTER\n"); break;
			case USB_CLASS_MASS_STORAGE: 	pszOut += sprintf(pszOut, "USB_CLASS_MASS_STORAGE\n"); break;
			case USB_CLASS_HUB: 			pszOut += sprintf(pszOut, "USB_CLASS_HUB\n"); break;
			case USB_CLASS_VENDOR_SPEC: 	pszOut += sprintf(pszOut, "USB_CLASS_VENDOR_SPEC\n"); break;
			default: 						pszOut += sprintf(pszOut, "???\n"); break;
		}
		pszOut += sprintf(pszOut, "%s    bDeviceSubClass    = %02xh\n", pszHeader, (unsigned int)pDesc->bDeviceSubClass);
		pszOut += sprintf(pszOut, "%s    bDeviceProtocol    = %02xh\n", pszHeader, (unsigned int)pDesc->bDeviceProtocol);
		pszOut += sprintf(pszOut, "%s    bMaxPacketSize0    = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bMaxPacketSize0, (unsigned int)pDesc->bMaxPacketSize0);
		pszOut += sprintf(pszOut, "%s    idVendor           = %04xh\n", pszHeader, (unsigned int)pDesc->idVendor);
		pszOut += sprintf(pszOut, "%s    idProduct          = %04xh\n", pszHeader, (unsigned int)pDesc->idProduct);
		pszOut += sprintf(pszOut, "%s    bcdDevice          = %04xh\n", pszHeader, (unsigned int)pDesc->bcdDevice);
		pszOut += sprintf(pszOut, "%s    iManufacturer      = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->iManufacturer, (unsigned int)pDesc->iManufacturer);
		pszOut += sprintf(pszOut, "%s    iProduct           = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->iProduct, (unsigned int)pDesc->iProduct);
		pszOut += sprintf(pszOut, "%s    iSerialNumber      = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->iSerialNumber, (unsigned int)pDesc->iSerialNumber);
		pszOut += sprintf(pszOut, "%s    bNumConfigurations = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bNumConfigurations, (unsigned int)pDesc->bNumConfigurations);
	}

	pszOut += sprintf(pszOut, "%s    --------------------------\n", pszHeader);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpCD(const char* pszHeader, const struct usb_config_descriptor* pDesc, unsigned int uDescLen, char* pszOut)
{
	// Dump the config descriptor.

	const char*const pszPin = pszOut;

	pszOut += sprintf(pszOut, "%s    -- config descriptor -----\n", pszHeader);

	if(uDescLen < USB_DT_CONFIG_SIZE)
	{
		pszOut += sprintf(pszOut, "%s    short transfer (%d/%d bytes)\n", pszHeader, uDescLen, (unsigned int)pDesc->bLength);
	}
	else
	{
		pszOut += sprintf(pszOut, "%s    bLength             = %02xh\n", pszHeader, (unsigned int)pDesc->bLength);
		pszOut += sprintf(pszOut, "%s    bDescriptorType     = %02xh\n", pszHeader, (unsigned int)pDesc->bDescriptorType);
		pszOut += sprintf(pszOut, "%s    wTotalLength        = %04xh (%d)\n", pszHeader, (unsigned int)pDesc->wTotalLength, (unsigned int)pDesc->wTotalLength);
		pszOut += sprintf(pszOut, "%s    bNumInterfaces      = %02xh\n", pszHeader, (unsigned int)pDesc->bNumInterfaces);
		pszOut += sprintf(pszOut, "%s    bConfigurationValue = %02xh\n", pszHeader, (unsigned int)pDesc->bConfigurationValue);
		pszOut += sprintf(pszOut, "%s    iConfiguration      = %02xh\n", pszHeader, (unsigned int)pDesc->iConfiguration);
		pszOut += sprintf(pszOut, "%s    bmAttributes        = %02xh\n", pszHeader, (unsigned int)pDesc->bmAttributes);
		pszOut += sprintf(pszOut, "%s    MaxPower            = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->MaxPower, (unsigned int)pDesc->MaxPower);

	}

	pszOut += sprintf(pszOut, "%s    --------------------------\n", pszHeader);
		
	if(uDescLen > USB_DT_CONFIG_SIZE)
	{
		const struct usb_descriptor_header* pDH = (const struct usb_descriptor_header*)((unsigned int)pDesc +  pDesc->bLength);
		const struct usb_descriptor_header*const pEnd = (const struct usb_descriptor_header*)((unsigned int)pDesc + min(uDescLen, pDesc->wTotalLength));
	
		for( ; pDH < pEnd; pDH = (const struct usb_descriptor_header*)((unsigned int)pDH + pDH->bLength))
		{
			switch(pDH->bDescriptorType)
			{
				case USB_DT_DEVICE:
				case USB_DT_CONFIG:
				case USB_DT_STRING:
					printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": wrong descriptor type\n", __LINE__);
					break;	

				case USB_DT_INTERFACE:
					pszOut += DumpID(pszHeader, (const struct usb_interface_descriptor*)pDH, (const void*)pEnd - (const void*)pDH, pszOut);
					break;	

				case USB_DT_ENDPOINT:
					pszOut += DumpED(pszHeader, (const struct usb_endpoint_descriptor*)pDH, (const void*)pEnd - (const void*)pDH, pszOut);
					break;	
						
				default:
					pszOut += sprintf(pszOut, "%s    unknown descriptor type %xh, %u bytes\n", 
						pszHeader, (unsigned int)pDesc->bDescriptorType, (unsigned int)pDesc->bLength);
					break;
			}
		}
	}

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpSD(const char* pszHeader, const struct usb_string_descriptor* pDesc, unsigned int uDescLen, unsigned int uIndex, char* pszOut)
{
	// Dump the string descriptors.

	const char*const pszPin = pszOut;

	pszOut += sprintf(pszOut, "%s    -- string descriptor -----\n", pszHeader);

	if(uDescLen < 2 || uDescLen < pDesc->bLength)
	{
		pszOut += sprintf(pszOut, "%s    short transfer (%d/%d bytes)\n", pszHeader, uDescLen, (unsigned int)pDesc->bLength);
	}
	else
	{
		const char* pc = (const char*)(&pDesc->wData);
		const char* pEnd = (const char*)(&pDesc->wData + (min(pDesc->bLength, uDescLen) - 2) / 2);

		pszOut += sprintf(pszOut, "%s    bLength         = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bLength, (unsigned int)pDesc->bLength);
		pszOut += sprintf(pszOut, "%s    bDescriptorType = %02xh\n", pszHeader, (unsigned int)pDesc->bDescriptorType);
		
		if(uIndex == 0)
		{	
			pszOut += sprintf(pszOut, "%s    wData           = [tbd]\n", pszHeader); 	// dump language codes.
		}
		else
		{
			pszOut += sprintf(pszOut, "%s    wData[]         = '", pszHeader);

			for( ; pc < pEnd; pc += 2)
			{
				pszOut += sprintf(pszOut, "%c", isprint(*pc) ? *pc : '.' );  // this just takes the low byte of unicode.
			}
			pszOut += sprintf(pszOut, "'\n");
		}
	}

	pszOut += sprintf(pszOut, "%s    --------------------------\n", pszHeader);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpID(const char* pszHeader, const struct usb_interface_descriptor* pDesc, unsigned int uDescLen, char* pszOut)
{
	// Dump the interface descriptor.

	const char*const pszPin = pszOut;

	pszOut += sprintf(pszOut, "%s    -- interface descriptor --\n", pszHeader);

	if(uDescLen < USB_DT_INTERFACE_SIZE)
	{
		pszOut += sprintf(pszOut, "%s    short transfer (%d/%d bytes)\n", pszHeader, uDescLen, (unsigned int)pDesc->bLength);
	}
	else
	{
		pszOut += sprintf(pszOut, "%s    bLength            = %02xh\n", pszHeader, (unsigned int)pDesc->bLength);
		pszOut += sprintf(pszOut, "%s    bDescriptorType    = %02xh\n", pszHeader, (unsigned int)pDesc->bDescriptorType);
		pszOut += sprintf(pszOut, "%s    bInterfaceNumber   = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bInterfaceNumber, (unsigned int)pDesc->bInterfaceNumber);
		pszOut += sprintf(pszOut, "%s    bAlternateSetting  = %02xh\n", pszHeader, (unsigned int)pDesc->bAlternateSetting);
		pszOut += sprintf(pszOut, "%s    bNumEndpoints      = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bNumEndpoints, (unsigned int)pDesc->bNumEndpoints);
		pszOut += sprintf(pszOut, "%s    bInterfaceClass    = %02xh\n", pszHeader, (unsigned int)pDesc->bInterfaceClass);
		pszOut += sprintf(pszOut, "%s    bInterfaceSubClass = %02xh\n", pszHeader, (unsigned int)pDesc->bInterfaceSubClass);
		pszOut += sprintf(pszOut, "%s    bInterfaceProtocol = %02xh\n", pszHeader, (unsigned int)pDesc->bInterfaceProtocol);
		pszOut += sprintf(pszOut, "%s    iInterface         = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->iInterface, (unsigned int)pDesc->iInterface);
	}

	pszOut += sprintf(pszOut, "%s    --------------------------\n", pszHeader);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpED(const char* pszHeader, const struct usb_endpoint_descriptor* pDesc, unsigned int uDescLen, char* pszOut)
{
	// Dump the endpoint descriptor.

	const char*const pszPin = pszOut;

	pszOut += sprintf(pszOut, "%s    -- endpoint descriptor ---\n", pszHeader);

	if(uDescLen < USB_DT_ENDPOINT_SIZE)
	{
		pszOut += sprintf(pszOut, "%s    short transfer (%d/%d bytes)\n", pszHeader, uDescLen, (unsigned int)pDesc->bLength);
	}
	else
	{
		pszOut += sprintf(pszOut, "%s    bLength          = %02xh\n", pszHeader, (unsigned int)pDesc->bLength);
		pszOut += sprintf(pszOut, "%s    bDescriptorType  = %02xh\n", pszHeader, (unsigned int)pDesc->bDescriptorType);
		pszOut += sprintf(pszOut, "%s    bEndpointAddress = %02xh\n", pszHeader, (unsigned int)pDesc->bEndpointAddress);
		pszOut += sprintf(pszOut, "%s    bmAttributes     = %02xh\n", pszHeader, (unsigned int)pDesc->bmAttributes);
		pszOut += sprintf(pszOut, "%s    wMaxPacketSize   = %04xh (%d)\n", pszHeader, (unsigned int)pDesc->wMaxPacketSize, (unsigned int)pDesc->wMaxPacketSize);
		pszOut += sprintf(pszOut, "%s    bInterval        = %02xh (%d)\n", pszHeader, (unsigned int)pDesc->bInterval, (unsigned int)pDesc->bInterval);
	
		if(pDesc->bLength == USB_DT_ENDPOINT_AUDIO_SIZE)
		{
			pszOut += sprintf(pszOut, "%s    bRefresh         = %04xh\n", pszHeader, (unsigned int)pDesc->bRefresh);
			pszOut += sprintf(pszOut, "%s    bSynchAddress    = %02xh\n", pszHeader, (unsigned int)pDesc->bSynchAddress);
		}
	}

	pszOut += sprintf(pszOut, "%s    --------------------------\n", pszHeader);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static unsigned int DumpRawBuf(const char* pszHeader, const void* pvBuf, unsigned int uBytes, char* pszOut)
{
	const char*const pszPin = pszOut;
	unsigned int i;

	if(uBytes == 0)
	{
		return 0;
	}

	pszOut += sprintf(pszOut, "%s [ ", pszHeader);

	for(i = 0; i < uBytes; i++) 
	{
		if((i % 256) == 0 && i != 0)  // dump 256 bytes per line.
		{
			pszOut += sprintf(pszOut, "]\n%s [ ", pszHeader);
		}
		
		pszOut += sprintf(pszOut, "%02x ", (unsigned int)*((const unsigned char*)pvBuf + i));
	}

	pszOut += sprintf(pszOut, "]\n");

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------

//====| external scope |=================================================================
void SideDumper_InstallDumper(void)
{
	g_Inst.uUrbNumGenerator = 1;

	// setup Queue.

	INIT_LIST_HEAD(&g_Inst.Queue.list);
	init_MUTEX_LOCKED(&g_Inst.Queue.semItemsQueued);
	init_MUTEX(&g_Inst.Queue.semListAccess);

	// setup Thread.

	if(g_Inst.Thread.pid != 0)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": called twice.\n", __LINE__);
		return;
	}

	g_Inst.Thread.oContinue = 1;

	init_MUTEX_LOCKED(&g_Inst.Thread.semExitSignal);

	g_Inst.Thread.pid = kernel_thread((int (*)(void*))(WorkerThreadEntry), &g_Inst, 0);

	if(g_Inst.Thread.pid < 0) 
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": kernel_thread() failed: %u.\n", __LINE__, (unsigned int)(g_Inst.Thread.pid));

		g_Inst.Thread.pid = 0;

		return;
	}

	InstallProcFs();
}//--------------------------------------------------------------------------------------
void SideDumper_RemoveDumper(void)
{
	printk(KERN_ERR __FILE__ ":" __FUNCTION__ ".\n");

	g_Inst.Thread.oContinue = 0;

	up(&g_Inst.Queue.semItemsQueued);

	down(&g_Inst.Thread.semExitSignal);

	//clean up list.

    while(!list_empty(&g_Inst.Queue.list))
	{
		TWorkItem* pItem = list_entry(g_Inst.Queue.list.next, TWorkItem, link);

		list_del(&pItem->link);
		INIT_LIST_HEAD(&pItem->link);	//need this???

		kfree(pItem);
	}

	RemoveProcFs();
}//--------------------------------------------------------------------------------------
static void ProcessUrb(const urb_t* urb)
{
	SideDumper_TMiniUrb m;

	InitMini(&m, urb);
	ProcessMini(&m);
	CleanMini(&m);
}//--------------------------------------------------------------------------------------
static void QueueUrb(const urb_t* urb)
{
	TWorkItem*const pItem = (TWorkItem*)kmalloc(sizeof(TWorkItem), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);

	if(pItem == NULL)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": out of memory.\n", __LINE__);
		return;
	}

	InitMini(&pItem->Mini, urb);

	printk(KERN_DEBUG "side_dumper: *queue urb_%04u %c\n", 
		pItem->Mini.uUrbNumber, 
		usb_pipeout(pItem->Mini.pipe) ? '>' : '<');

	down(&g_Inst.Queue.semListAccess);

	list_add_tail(&pItem->link, &g_Inst.Queue.list);

	up(&g_Inst.Queue.semListAccess);

	up(&g_Inst.Queue.semItemsQueued);
}//--------------------------------------------------------------------------------------

static void InitMini(SideDumper_TMiniUrb* pMini, const urb_t* urb)
{
	const unsigned int uTbBytes = usb_pipeout(urb->pipe) ? urb->transfer_buffer_length : urb->actual_length;

	pMini->uUrbNumber 				= g_Inst.uUrbNumGenerator++;
	pMini->devnum 					= urb->dev->devnum;
	pMini->pipe 					= urb->pipe;			   
	pMini->transfer_buffer_length 	= urb->transfer_buffer_length;			   
	pMini->actual_length 			= urb->actual_length;			   
	
	if(urb->setup_packet)
	{
		memcpy(pMini->setup_packet, urb->setup_packet, 8);
	}

	if(uTbBytes && urb->transfer_buffer)
	{
		pMini->transfer_buffer = kmalloc(uTbBytes, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
		memcpy(pMini->transfer_buffer, urb->transfer_buffer, uTbBytes);
	}
	else
	{
		pMini->transfer_buffer = NULL;
	}
}//--------------------------------------------------------------------------------------
static void ProcessMini(const SideDumper_TMiniUrb* pMini)
{
	if(g_Inst.aDevSettings[pMini->devnum].oEnabled
		&& g_Inst.aDevSettings[pMini->devnum].pfnDumper)
	{
		(*g_Inst.aDevSettings[pMini->devnum].pfnDumper)(&g_Inst.aDevSettings[pMini->devnum], pMini);
	}
			
}//--------------------------------------------------------------------------------------
static void CleanMini(SideDumper_TMiniUrb* pMini)
{
	if(pMini->transfer_buffer)
	{
		kfree(pMini->transfer_buffer);
		pMini->transfer_buffer = NULL;
	}
}//--------------------------------------------------------------------------------------

void SideDumper_OnSubmit(const urb_t* urb)
{
	if(urb->dev 
		&& urb->dev->bus 
		&& urb->dev->devnum < (sizeof(g_Inst.aDevSettings) / sizeof(g_Inst.aDevSettings[0]))
		&& g_Inst.aDevSettings[urb->dev->devnum].oEnabled)
	{
		if(usb_pipeout(urb->pipe))
		{
			if(g_Inst.aDevSettings[urb->dev->devnum].oAsync)
			{
				QueueUrb(urb);
			}
			else
			{
				ProcessUrb(urb);
			}
		}
		else
		{
			// currently silent.
		}
	}
}//--------------------------------------------------------------------------------------
void SideDumper_OnReturn(const urb_t* urb)
{
	if(urb->dev 
		&& urb->dev->bus 
		&& urb->dev->devnum < (sizeof(g_Inst.aDevSettings) / sizeof(g_Inst.aDevSettings[0]))
		&& g_Inst.aDevSettings[urb->dev->devnum].oEnabled)
	{
		if(usb_pipein(urb->pipe))
		{
			if(g_Inst.aDevSettings[urb->dev->devnum].oAsync)
			{
				QueueUrb(urb);
			}
			else
			{
				ProcessUrb(urb);
			}
		}
	}
}//--------------------------------------------------------------------------------------
void SideDumper_Enable(unsigned int uDeviceNumber)
{
	if(uDeviceNumber < (sizeof(g_Inst.aDevSettings) / sizeof(g_Inst.aDevSettings[0])))
	{
		g_Inst.aDevSettings[uDeviceNumber].oEnabled = 1;
		printk(KERN_INFO "side_dumper: USB device #%u enabled.\n", uDeviceNumber);
	}
}//--------------------------------------------------------------------------------------
void SideDumper_Disable(unsigned int uDeviceNumber)
{
	if(uDeviceNumber < (sizeof(g_Inst.aDevSettings) / sizeof(g_Inst.aDevSettings[0])))
	{
		g_Inst.aDevSettings[uDeviceNumber].oEnabled = 0;
		printk(KERN_INFO "side_dumper: USB device #%u disabled.\n", uDeviceNumber);
	}
}//--------------------------------------------------------------------------------------
void SideDumper_SetDeviceSettings(unsigned int uDeviceNumber, const SideDumper_TDeviceSettings* pSettings)
{
	if(pSettings != NULL 
		&& uDeviceNumber < (sizeof(g_Inst.aDevSettings) / sizeof(g_Inst.aDevSettings[0])))
	{
		g_Inst.aDevSettings[uDeviceNumber] = *pSettings;
		printk(KERN_INFO "side_dumper: USB device #%u set.\n", uDeviceNumber);
	}
}//--------------------------------------------------------------------------------------
int SideDumper_GetDeviceSettings(unsigned int uDeviceNumber, SideDumper_TDeviceSettings* pSettings)
{
	if(pSettings == NULL)
	{
		return -EINVAL;
	}

	if(uDeviceNumber < (sizeof(g_Inst.aDevSettings) / sizeof(g_Inst.aDevSettings[0])))
	{
		*pSettings = g_Inst.aDevSettings[uDeviceNumber];

		return 0;
	}

	memset(pSettings, 0, sizeof(SideDumper_TDeviceSettings));

	return -ENODEV;
}//--------------------------------------------------------------------------------------

#endif // CONFIG_USB_SIDE_DUMPER
//=====| end of file |===================================================================

