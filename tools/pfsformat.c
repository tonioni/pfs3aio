#define __USE_SYSBASE
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <workbench/workbench.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <string.h>
#include <stdlib.h>
#include "versions.h"
#include "../pfs3disk.h"

#define TEMPLATE "DEVICE=DRIVE/K/A,NAME/K/A,NOICONS/S,QUICK/S,DELDIRBLOCKS=DD/K/N,FILENAMESIZE=FNSIZE/K/N,VERIFY/S,RESERVED/K"
#define DEVICE 0
#define NAME 1
#define NOICONS 2
#define QUICK 3
#define DDBL 4
#define FNS 5
#define VERIFY 6
#define RESERVED 7

#define ACTION_SET_DELDIR 2221
#define ACTION_SET_FNSIZE 2222
#define ACTION_PFS_FORMAT 2225
#define ID_PFS2_DISK (0x50465302L)	/* 'PFS\2' */
/* the reserved area is at most 4096 + 255 * 8192 blocks of 4 KiB */
#define MAXRESERVEDKIB (8176 * 1024)

UBYTE *version = "$VER: PFSFormat " PFSFORMAT_REVISION " (" PFSFORMAT_DATE ")";
LONG args[8];

struct Library *IconBase;

BOOL FullFormat(struct DosList *dl, BOOL verify);
BOOL ReadRootBlock(struct DosList *dl, rootblock_t *rb, ULONG *blocksize);
static void AddIcons(UBYTE *drive);
static void PrintParameters(UBYTE *drive, UBYTE *name, ULONG kib);
static void PrintResult(struct DosList *dl, ULONG kib);
static void PrintSize(ULONG n, ULONG per);

/* the device of a device or volume name such as DH1: or Work:, its name
 * with ':' in devicename; NULL for anything else, as a path
 */
static UBYTE devicename[34];

static struct DosList *FindDevice(UBYTE *drive)
{
	struct DosList *start, *dl;
	UBYTE *colon = (UBYTE *)strchr((char *)drive, ':'), *bstr;

	if (!colon || colon[1] || colon == drive || colon - drive > 30)
		return NULL;
	memcpy(devicename, drive, colon - drive);
	devicename[colon - drive] = 0;
	start = LockDosList(LDF_DEVICES | LDF_VOLUMES | LDF_READ);
	dl = FindDosEntry(start, (STRPTR)devicename, LDF_DEVICES | LDF_VOLUMES);
	if (dl && dl->dol_Type == DLT_VOLUME)
	{
		struct MsgPort *port = dl->dol_Task;

		for (dl = NextDosEntry(start, LDF_DEVICES); dl; dl = NextDosEntry(dl, LDF_DEVICES))
			if (port && dl->dol_Task == port)
				break;
	}
	if (dl)
	{
		bstr = (UBYTE *)BADDR(dl->dol_Name);
		memcpy(devicename, bstr + 1, bstr[0] < 31 ? bstr[0] : 31);
		devicename[bstr[0] < 31 ? bstr[0] : 31] = 0;
		strcat((char *)devicename, ":");
	}
	UnLockDosList(LDF_DEVICES | LDF_VOLUMES | LDF_READ);
	return dl;
}

/* a size of 1 to MAXRESERVEDKIB KiB, with m in MB; 0 for anything else */
static ULONG ParseKiB(UBYTE *s)
{
	ULONG n = 0, unit = 1;

	if (*s < '0' || *s > '9')
		return 0;
	for (; *s >= '0' && *s <= '9'; s++)
	{
		n = n * 10 + *s - '0';
		if (n > MAXRESERVEDKIB)
			return 0;
	}
	if (*s == 'k' || *s == 'K')
		s++;
	else if (*s == 'm' || *s == 'M')
	{
		unit = 1024;
		s++;
	}
	if (*s || n > MAXRESERVEDKIB / unit)
		return 0;
	return n * unit;
}

/* TRUE once Ctrl-C was pressed */
static BOOL Break(void)
{
	if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
	{
		PutStr("\nbreak\n");
		return TRUE;
	}
	return FALSE;
}

int main(void)
{
	struct RDArgs *result;
	struct MsgPort *msgport;
	struct DosList *dl;
	UBYTE *drive, *name, *bname = NULL;
	ULONG kib = 0, len;
	LONG rc = RETURN_FAIL, c;
	BOOL inhibited = FALSE, formatted, pfsformat;

	SysBase = *((struct ExecBase **)4);
	if (SysBase->LibNode.lib_Version < 37)
		exit(RETURN_FAIL);

	if (!(result = ReadArgs(TEMPLATE, args, NULL)))
	{
		PrintFault(IoErr(), "PFSFormat");
		exit(RETURN_FAIL);
	}
	drive = (UBYTE *)args[DEVICE];
	name = (UBYTE *)args[NAME];
	if (args[RESERVED])
		kib = ParseKiB((UBYTE *)args[RESERVED]);
	len = strlen((char *)name);

	do
	{
		/* a device or volume such as DH1: or Work:, never a path */
		if (!(dl = FindDevice(drive)))
		{
			PutStr("Could not find device\n");
			break;
		}
		drive = devicename;
		if (!len || len > 30 || strchr((char *)name, ':') || strchr((char *)name, '/'))
		{
			PrintFault(ERROR_INVALID_COMPONENT_NAME, "PFSFormat");
			break;
		}
		if (args[RESERVED] && !kib)
		{
			Printf("RESERVED is 1 to %ld KiB, or with m as MiB like 1m to %ldm\n", MAXRESERVEDKIB, MAXRESERVEDKIB / 1024);
			break;
		}
		if (!(msgport = DeviceProc((STRPTR)drive)))
		{
			PutStr("Could not find device\n");
			break;
		}
		/* the volume name as a BSTR */
		if (!(bname = AllocVec(len + 2, MEMF_PUBLIC)))
		{
			PrintFault(ERROR_NO_FREE_STORE, "PFSFormat");
			break;
		}
		bname[0] = len;
		memcpy(bname + 1, name, len + 1);

		if (!(inhibited = Inhibit((STRPTR)drive, DOSTRUE)))
		{
			PutStr("Could not inhibit device\n");
			break;
		}

		PrintParameters(drive, name, kib);
		PutStr("Press enter to continue, or CTRL-C to break\n");
		while ((c = FGetC(Input())) != '\n' && c != -1)
			;
		if (c == -1 || Break())
			break;

		/* a handler with ACTION_PFS_FORMAT refuses a format without a name
		 * at once, any other answer means it does not have it */
		pfsformat = !DoPkt(msgport, ACTION_PFS_FORMAT, 0, 0, 0, 0, 0) &&
			IoErr() == ERROR_INVALID_COMPONENT_NAME;
		if (kib && !pfsformat)
		{
			PutStr("RESERVED is not supported by this device\n");
			break;
		}
		if (!args[QUICK] && !FullFormat(dl, args[VERIFY]))
			break;

		PutStr("Initializing disk ...\n");
		/* ACTION_PFS_FORMAT formats without the requester of Format() */
		if (pfsformat)
			formatted = DoPkt(msgport, ACTION_PFS_FORMAT, MKBADDR(bname), 0, kib, 0, 0);
		else
			formatted = Format((STRPTR)drive, (STRPTR)name, 0);
		if (!formatted)
		{
			PrintFault(IoErr(), "PFSFormat");
			break;
		}
		PrintResult(dl, kib);

		Inhibit((STRPTR)drive, DOSFALSE);
		inhibited = FALSE;

		if (!args[NOICONS])
			AddIcons(drive);

		if (args[DDBL] && !DoPkt(msgport, ACTION_SET_DELDIR, ID_PFS2_DISK, *(LONG *)args[DDBL], 0, 0, 0))
		{
			PutStr("Creating deldir blocks failed\n");
			break;
		}
		if (args[FNS] && !DoPkt(msgport, ACTION_SET_FNSIZE, ID_PFS2_DISK, *(LONG *)args[FNS], 0, 0, 0))
		{
			PutStr("Setting filenamesize failed\n");
			break;
		}
		rc = RETURN_OK;
	} while (0);

	if (inhibited)
		Inhibit((STRPTR)drive, DOSFALSE);
	if (bname)
		FreeVec(bname);
	FreeArgs(result);
	return rc;
}

static void PrintParameters(UBYTE *drive, UBYTE *name, ULONG kib)
{
	LONG dd = args[DDBL] ? *(LONG *)args[DDBL] : 2;
	LONG fns = args[FNS] ? *(LONG *)args[FNS] : 32;

	Printf("Device            %s\n", drive);
	Printf("Name              %s\n", name);
	Printf("Format            %s\n", args[QUICK] ? "quick" : args[VERIFY] ? "full, verified" : "full");
	if (kib)
	{
		PutStr("Reserved area     ");
		PrintSize(kib, 1024);
		PutStr("\n");
	}
	else
		PutStr("Reserved area     default\n");
	if (dd)
		Printf("Delete directory  %ld blocks\n", dd);
	else
		PutStr("Delete directory  off\n");
	Printf("File names        up to %ld characters\n", fns - 1);
	Printf("Icons             %s\n\n", args[NOICONS] ? "no" : "yes");
}

/* a size in units of 1/per MB: in KiB below 1 MB, else in MB with one
 * decimal unless whole */
static void PrintSize(ULONG n, ULONG per)
{
	if (n < per)
		Printf("%lu KiB", n * 1024 / per);
	else if (n % per == 0)
		Printf("%lu MB", n / per);
	else
		Printf("%lu.%lu MB", n / per, (n % per) * 10 / per);
}

/* the reserved and the data area the format made, from the rootblock */
static void PrintResult(struct DosList *dl, ULONG asked)
{
	rootblock_t rb;
	ULONG bs, area, kib, data, rbs;

	if (!ReadRootBlock(dl, &rb, &bs) || (rb.disktype & 0xffffff00) != (ID_PFS2_DISK & 0xffffff00) ||
		!rb.reserved_blksize || rb.lastreserved < rb.firstreserved)
		return;
	area = rb.lastreserved - rb.firstreserved + 1;
	kib = area * (bs / 512) / 2;
	/* nothing of the data area is in use right after the format */
	data = rb.blocksfree;
	rbs = rb.reserved_blksize / 1024;
	PutStr("Reserved area     ");
	PrintSize(kib, 1024);
	Printf(" in %lu blocks of %lu KiB\n", kib / rbs, rbs);
	PutStr("Data area         ");
	PrintSize(data, 1048576 / bs);
	Printf(" in %lu blocks of %lu bytes\n", data, bs);
	/* the handler rounds the size up to 32 reserved blocks, and takes the
	 * minimum for a smaller one */
	if (asked && kib / rbs > (((asked + rbs - 1) / rbs + 31) & ~31))
		Printf("RESERVED %lu KiB is below the minimum of this partition\n", asked);
}

/* the Trashcan and the disk icon */
static void AddIcons(UBYTE *drive)
{
	BPTR root, oldcd, dir;
	struct DiskObject *dobj;

	if (!(IconBase = OpenLibrary("icon.library", 37)))
	{
		PutStr("Could not open icon.library\n");
		return;
	}
	if (!(root = Lock((STRPTR)drive, ACCESS_READ)))
	{
		PrintFault(IoErr(), "PFSFormat");
		CloseLibrary(IconBase);
		return;
	}
	oldcd = CurrentDir(root);
	if ((dobj = GetDefDiskObject(WBGARBAGE)))
	{
		if ((dir = CreateDir("Trashcan")))
			UnLock(dir);
		if (!dir || !PutDiskObject("Trashcan", dobj))
			PutStr("Could not create Trashcan.info\n");
		FreeDiskObject(dobj);
	}
	if (!(dobj = GetDefDiskObject(WBDISK)) || !PutDiskObject("Disk", dobj))
		PutStr("Could not create Disk.info\n");
	if (dobj)
		FreeDiskObject(dobj);
	CurrentDir(oldcd);
	UnLock(root);
	CloseLibrary(IconBase);
}
