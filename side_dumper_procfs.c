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
 *  $Id: side_dumper_procfs.c,v 1.2 2002/01/10 11:04:26 glevand Exp $
 */

#include <linux/config.h>
#if defined (CONFIG_USB_SIDE_DUMPER)
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>		// for get_user().
#include "side_dumper.h"
#include "side_dumper_procfs.h"

typedef struct
{
	struct proc_dir_entry* pRootDir;
	struct proc_dir_entry* pSettingsDir;
} TInst;

static int Read_Enabled(char* page, char** start, off_t off, int count, int* eof, void* data);
static int Write_Enabled(struct file* file, const char* buffer, unsigned long count, void* data);

static int Read_Disabled(char* page, char** start, off_t off, int count, int* eof, void* data);
static int Write_Disabled(struct file* file, const char* buffer, unsigned long count, void* data);

static int Read_DeviceSettings(char* page, char** start, off_t off, int count, int* eof, void* data);
static int Write_DeviceSettings(struct file* file, const char* buffer, unsigned long count, void* data);

// utils.
static int PrintSettings(unsigned int uDev, char* pszOut);
static const char* EatDelimitor(const char* p);
static __inline unsigned int min(unsigned int a, unsigned int b);

static const unsigned int g_uMaxDevNumber = 100;
static const unsigned int g_uDeviceAllNumber = 9999;

static TInst g_Inst =
{
	pRootDir: NULL,
};

//====| static inlines |=================================================================
static __inline unsigned int min(unsigned int a, unsigned int b)
{
	return (a < b) ? a : b;
}//--------------------------------------------------------------------------------------

//====| static scope |===================================================================
static const char* EatDelimitor(const char* p)
{
	while(*p ==  ' ' || *p ==  '\t' || *p ==  ',' || *p ==  ':' || *p ==  '\n')
	{
		p++;
	}

	return p;
}//--------------------------------------------------------------------------------------
static int Read_Enabled(char* page, char** start, off_t off, int count, int* eof, void* data)
{
	unsigned int uDev;
	unsigned int uBytes = 0;

	*eof = 1;

	uBytes += sprintf(page + uBytes, "SideDumper: enabled devices = [ ");

	for(uDev = 1; uDev < g_uMaxDevNumber; uDev++)
	{
		SideDumper_TDeviceSettings s;

		if(SideDumper_GetDeviceSettings(uDev, &s) != 0)
		{
			break;	// done.
		}

		if(s.oEnabled)
		{
			uBytes += sprintf(page + uBytes, "%02u ", uDev);
		}
	}

	uBytes += sprintf(page + uBytes, "]\n");

	return uBytes;
}//--------------------------------------------------------------------------------------
static int Write_Enabled(struct file* file, const char* buffer, unsigned long count, void* data)
{
	char a[256];
	const char* pCur = a;
	const char*const pEnd = a + min(count, sizeof(a) - 1);

	copy_from_user(a, buffer, pEnd - a);	// need this???

	while(1)
	{
		unsigned int uDev;
		const char* pNext;

		pCur = EatDelimitor(pCur);

		uDev = simple_strtoul(pCur, (char**)(&pNext), 10);

		if(pCur == pNext || pCur >= pEnd)
		{
			break;	// done.
		}

		pCur = pNext;

		SideDumper_Enable(uDev); 
	}

	return count;
}//--------------------------------------------------------------------------------------
static int Read_Disabled(char* page, char** start, off_t off, int count, int* eof, void* data)
{
	unsigned int uDev;
	unsigned int uBytes = 0;

	*eof = 1;

	uBytes += sprintf(page + uBytes, "SideDumper: disabled devices = [ ");

	for(uDev = 1; uDev < g_uMaxDevNumber; uDev++)
	{
		SideDumper_TDeviceSettings s;

		if(SideDumper_GetDeviceSettings(uDev, &s) != 0)
		{
			break;	// done.
		}

		if(!s.oEnabled)
		{
			uBytes += sprintf(page + uBytes, "%02u ", uDev);
		}
	}

	uBytes += sprintf(page + uBytes, "]\n");

	return uBytes;
}//--------------------------------------------------------------------------------------
static int Write_Disabled(struct file* file, const char* buffer, unsigned long count, void* data)
{
	char a[256];
	const char* pCur = a;
	const char*const pEnd = a + min(count, sizeof(a) - 1);

	copy_from_user(a, buffer, pEnd - a);	// need this???

	while(1)
	{
		unsigned int uDev;
		const char* pNext;

		pCur = EatDelimitor(pCur);

		uDev = simple_strtoul(pCur, (char**)(&pNext), 10);

		if(pCur == pNext || pCur >= pEnd)
		{
			break;	// done.
		}

		pCur = pNext;

		SideDumper_Disable(uDev); 
	}

	return count;
}//--------------------------------------------------------------------------------------
static int PrintSettings(unsigned int uDev, char* pszOut)
{
	const char*const pszPin = pszOut;
	SideDumper_TDeviceSettings s;

	if(SideDumper_GetDeviceSettings(uDev, &s) != 0)
	{
		return 0;
	}

	pszOut += sprintf(pszOut, "SideDumper: device_%02u settings = [ Enabled:%u Async:%u OutLimit:%u InLimit:%u ]\n",
		uDev,
		s.oEnabled,
		s.oAsync,
		s.uOutLimit,
		s.uInLimit);

	return (pszOut - pszPin);
}//--------------------------------------------------------------------------------------
static int Read_DeviceSettings(char* page, char** start, off_t off, int count, int* eof, void* data)
{
	const char*const pszPin = page;
	unsigned int uDev = (unsigned int)(data);

	*eof = 1;

	if(uDev == g_uDeviceAllNumber)
	{
		for(uDev = 1; uDev < g_uMaxDevNumber; uDev++)
		{
			unsigned int u = PrintSettings(uDev, page);

			if(u == 0)
			{
				break;	// done.
			}

			page += u;
		}
	}
	else
	{
		page += PrintSettings(uDev, page);
	}

	return (page - pszPin);
}//--------------------------------------------------------------------------------------
static int Write_DeviceSettings(struct file* file, const char* buffer, unsigned long count, void* data)
{
	char a[256];
	const char* pCur = a;
	const char*const pEnd = a + min(count, sizeof(a) - 1);
	unsigned int uDev = (unsigned int)(data);

	SideDumper_TDeviceSettings s;

	if(SideDumper_GetDeviceSettings(uDev, &s) != 0)
	{
		return -EINVAL;
	}

	copy_from_user(a, buffer, pEnd - a);	// need this???

	while(1)
	{
		static const char szEnabled[]  = "Enabled:";
		static const char szAsync[]    = "Async:";
		static const char szOutLimit[] = "OutLimit:";
		static const char szInLimit[]  = "InLimit:";
		const char* pNext;

		pCur = EatDelimitor(pCur);

		if(pCur >= pEnd)
		{
			break;	// done.
		}

		if(memcmp(pCur, szEnabled, sizeof(szEnabled) - 1) == 0)
		{
			pCur += sizeof(szEnabled) - 1;
			s.oEnabled = simple_strtoul(pCur, (char**)(&pNext), 10);
		}
		else if(memcmp(pCur, szAsync, sizeof(szAsync) - 1) == 0)
		{
			pCur += sizeof(szAsync) - 1;
			s.oAsync = simple_strtoul(pCur, (char**)(&pNext), 10);
		}
		else if(memcmp(pCur, szOutLimit, sizeof(szOutLimit) - 1) == 0)
		{
			pCur += sizeof(szOutLimit) - 1;
			s.uOutLimit = simple_strtoul(pCur, (char**)(&pNext), 0);
		}
		else if(memcmp(pCur, szInLimit, sizeof(szInLimit) - 1) == 0)
		{
			pCur += sizeof(szInLimit) - 1;
			s.uInLimit = simple_strtoul(pCur, (char**)(&pNext), 0);
		}
		else
		{
			printk(KERN_WARNING "SideDumper: unknown setting '%s'\n", pCur);
			break;	// done.
		}

		if(pCur == pNext)
		{
			break;	// done.
		}

		pCur = pNext;

		SideDumper_SetDeviceSettings(uDev, &s); 
	}

	return count;
}//--------------------------------------------------------------------------------------

//====| external scope |=================================================================
int InstallProcFs(void)
{
	struct proc_dir_entry* pTemp;
	unsigned int uDev;

	// root.

	if(g_Inst.pRootDir != NULL || g_Inst.pSettingsDir != NULL)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": called twice.\n", __LINE__);
	}

	g_Inst.pRootDir = proc_mkdir("side_dumper", 0);

	if(g_Inst.pRootDir == NULL)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": proc_mkdir() failed.\n", __LINE__);
		return -EBUSY;
	}

	// enabled.

	pTemp = create_proc_entry("enabled", S_IFREG | S_IRUGO | S_IWUSR, g_Inst.pRootDir);

	if(pTemp == NULL)
	{
		printk(KERN_ERR __FILE__ " (%d):" __FUNCTION__ ": create_proc_entry() failed.\n", __LINE__);
		return -EBUSY;
	}

	pTemp->read_proc = Read_Enabled;
	pTemp->write_proc = Write_Enabled;

	// disabled.

	pTemp = create_proc_entry("disabled", S_IFREG | S_IRUGO | S_IWUSR, g_Inst.pRootDir);

	if(pTemp == NULL)
	{
		return -EBUSY;
	}

	pTemp->read_proc = Read_Disabled;
	pTemp->write_proc = Write_Disabled;

	// device_settings.

	g_Inst.pSettingsDir = proc_mkdir("device_settings", g_Inst.pRootDir);

	if(g_Inst.pSettingsDir == NULL)
	{
		return -EBUSY;
	}

	// all.

	pTemp = create_proc_entry("all", S_IFREG | S_IRUGO | S_IWUSR, g_Inst.pSettingsDir);

	if(pTemp != NULL)
	{
		pTemp->read_proc = Read_DeviceSettings;
		pTemp->data = (void*)(g_uDeviceAllNumber);
	}

	// device_xx.

	for(uDev = 1; uDev < g_uMaxDevNumber; uDev++)
	{
		SideDumper_TDeviceSettings s;
		char sz[64];

		if(SideDumper_GetDeviceSettings(uDev, &s) != 0)
		{
			break;	// done.
		}

		sprintf(sz, "device_%02u", uDev);

		pTemp = create_proc_entry(sz, S_IFREG | S_IRUGO | S_IWUSR, g_Inst.pSettingsDir);

		if(pTemp != NULL)
		{
			pTemp->read_proc = Read_DeviceSettings;
			pTemp->write_proc = Write_DeviceSettings;
			pTemp->data = (void*)(uDev);
		}
	}

	return 0;
}//--------------------------------------------------------------------------------------
void RemoveProcFs(void)
{
	unsigned int uDev;

	// device_settings.

	if(g_Inst.pSettingsDir != NULL)
	{
		for(uDev = 1; uDev < g_uMaxDevNumber; uDev++)
		{
			SideDumper_TDeviceSettings s;
			char sz[64];

			if(SideDumper_GetDeviceSettings(uDev, &s) != 0)
			{
				break;	// done.
			}

			sprintf(sz, "device_%02u", uDev);

			remove_proc_entry(sz, g_Inst.pSettingsDir);
		}

		remove_proc_entry("all", g_Inst.pSettingsDir);
		remove_proc_entry(g_Inst.pSettingsDir->name, 0);

		g_Inst.pSettingsDir = NULL;
	}

	// root.

	if(g_Inst.pRootDir != NULL)
	{
		remove_proc_entry("enabled", g_Inst.pRootDir);
		remove_proc_entry("disabled", g_Inst.pRootDir);
		remove_proc_entry(g_Inst.pRootDir->name, 0);

		g_Inst.pRootDir = NULL;
	}

}//--------------------------------------------------------------------------------------

#endif // CONFIG_USB_SIDE_DUMPER
//=====| end of file |===================================================================

