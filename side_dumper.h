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
 *  $Id: side_dumper.h,v 1.3 2002/01/10 11:04:26 glevand Exp $
 */

#if !defined(H__497B6274_3844_472A_B3DD_3795BFE93EF1__INCLUDED_)
#define H__497B6274_3844_472A_B3DD_3795BFE93EF1__INCLUDED_
#include <linux/config.h>
#include <linux/usb.h>

// SideDumper interface.
typedef struct 
{
	unsigned int uUrbNumber;
	unsigned int devnum;						// urb.dev.devnum
	unsigned int pipe;							// urp.pipe
	int transfer_buffer_length;					// urp.transfer_buffer_length
	unsigned int actual_length;					// urp.actual_length
	unsigned char setup_packet[8];				// urb.setup_packet
	void* transfer_buffer;						// urb.transfer_buffer
} SideDumper_TMiniUrb;

typedef struct SideDumper_tag_TDeviceSettings
{
	int oEnabled;																					// Master enable.
	int oAsync;																						// Use worker thread.
	void(* pfnDumper)(const struct SideDumper_tag_TDeviceSettings*, const SideDumper_TMiniUrb*);	// Defaults to printk().			
	unsigned int uOutLimit;																			// Output pipe max trace len.
	unsigned int uInLimit;																			// Input pipe max trace len.
} SideDumper_TDeviceSettings;

#if defined(CONFIG_USB_SIDE_DUMPER)
void SideDumper_InstallDumper(void); 
void SideDumper_RemoveDumper(void); 
void SideDumper_Enable(unsigned int uDeviceNumber); 
void SideDumper_Disable(unsigned int uDeviceNumber); 
void SideDumper_SetDeviceSettings(unsigned int uDeviceNumber, const SideDumper_TDeviceSettings* pSettings); 
int SideDumper_GetDeviceSettings(unsigned int uDeviceNumber, SideDumper_TDeviceSettings* pSettings); 
void SideDumper_OnSubmit(const urb_t* urb); 
void SideDumper_OnReturn(const urb_t* urb);
#else
static __inline void SideDumper_InstallDumper(void) {(void)(0);}
static __inline void SideDumper_RemoveDumper(void) {(void)(0);}
static __inline void SideDumper_Enable(unsigned int uDeviceNumber) {(void)(0);}
static __inline void SideDumper_Disable(unsigned int uDeviceNumber) {(void)(0);}
static __inline void SideDumper_SetDeviceSettings(unsigned int uDeviceNumber, const SideDumper_TDeviceSettings* pSettings) {(void)(0);}
static __inline void SideDumper_GetDeviceSettings(unsigned int uDeviceNumber, SideDumper_TDeviceSettings* pSettings) {(void)(0);}
static __inline void SideDumper_OnSubmit(const urb_t* urb) {(void)(0);}
static __inline void SideDumper_OnReturn(const urb_t* urb) {(void)(0);}
#endif

#endif // INCLUDED.
//=====| end of file |===================================================================
