
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/filehandler.h>

#include "pfs3.h"
#include "doctor.h"

enum mode mode = repair;	/* check, repair, unformat or search */

struct Window *CheckRepairWnd;
struct stats stats;

struct
{
	char name[200];
} targetdevice;
static BOOL verbose = FALSE;
static BOOL noninteractive = FALSE;
FILE *logfh = NULL;
static BOOL directscsi = FALSE;
static BOOL inhibited = FALSE;
static BOOL progressmark;

static const char *accessmodes[] = { "", "Standard", "Direct SCSI", "TD64", "NSD" };

void dummyMsg(char *message)
{
}

static void clearProgress(void)
{
	if (progressmark) {
		printf("\n");
		progressmark = 0;
	}
}

void guiMsg(const char *format, ...)
{
	clearProgress();
	va_list parms;
	va_start (parms, format);
	vprintf (format, parms);
	va_end (parms);
}

void guiUpdateStats(void)
{
}

void guiStatus(int level, char *message, long maxval)
{
	clearProgress();
	if (!message)
		return;
	printf("%s %ld...\n", message, maxval);
}

void guiProgress(int level, long progress)
{
	printf(".");
	fflush(stdout);
	progressmark = 1;
}

int guiAskUser(char *message, char *okstr, char *cancelstr)
{
	clearProgress();
	char ch = 0;
	for (;;) {
		printf("%s\n", message);
		if (cancelstr) {
			printf("1=<%s> 0=<%s>\n", okstr, cancelstr);
			if (noninteractive)
				return 1;
			scanf("%c", &ch);
			if (ch == '1')
				return 1;
			if (ch == '0')
				return 0;
		} else {
			printf("Press RETURN to continue\n");
			if (noninteractive)
				return 1;
			scanf("%c", &ch);
			return 1;
		}
	}
}

#define OV_Flags LDF_DEVICES|LDF_VOLUMES
static BOOL OpenVolume(void)
{
	struct DosList *dl;
	ULONG cylsectors, b;
	BOOL t;
	int i;
	UBYTE *detectbuf;

	/* get device doslist */
	dl = LockDosList (OV_Flags|LDF_READ);
	dl = FindDosEntry(dl, targetdevice.name, OV_Flags);

	if (dl && dl->dol_Type == DLT_VOLUME)
	{
		struct DosList *dl2;
		struct DosInfo *di;
		
		di = (struct DosInfo *)BADDR(((struct RootNode *)DOSBase->dl_Root)->rn_Info);
		for (dl2 = (struct DosList *)BADDR(di->di_DevInfo);
			 dl2;
			 dl2 = (struct DosList *)BADDR(dl2->dol_Next))
		{
			if (dl2->dol_Type == DLT_DEVICE && dl->dol_Task == dl2->dol_Task)
			{
				unsigned char *dname;

				dl = dl2;
				dname = (char *)BADDR(dl->dol_Name);
				strncpy(targetdevice.name, dname+1, *dname);
				targetdevice.name[*dname] = 0;
				break;
			}
		}
	}

	if (!dl || dl->dol_Type == DLT_VOLUME)
	{
		UnLockDosList(OV_Flags|LDF_READ);
		guiMsg("DEVICE "); guiMsg(targetdevice.name);
		guiMsg(" not found\nEXITING ...\n\n");
		return FALSE;
	}

	UnLockDosList(OV_Flags|LDF_READ);

	/* inhibit device */
	targetdevice.name[strlen(targetdevice.name)] = ':';
	targetdevice.name[strlen(targetdevice.name)] = 0;
	if (!(inhibited = Inhibit(targetdevice.name, DOSTRUE)))
	{
		guiMsg("Device could not be inhibited.\nEXITING ...\n\n");
		return FALSE;
	}

	/* init volume structure */
	memset(&volume, 0, sizeof(volume));
	volume.fssm = (struct FileSysStartupMsg *)BADDR(dl->dol_misc.dol_handler.dol_Startup);
	volume.dosenvec = (struct DosEnvec *)BADDR(volume.fssm->fssm_Environ);
	strcpy(volume.devicename, targetdevice.name);
	cylsectors = volume.dosenvec->de_Surfaces * volume.dosenvec->de_BlocksPerTrack;
	volume.firstblock = volume.dosenvec->de_LowCyl * cylsectors;
	volume.lastblock = (volume.dosenvec->de_HighCyl + 1) * cylsectors - 1;
	b = volume.dosenvec->de_SectorPerBlock;
	volume.blocklogshift = 0;
	while (b > 1) {
		volume.blocklogshift++;
		b >>= 1;		
	}
	b = volume.blocksize = (volume.dosenvec->de_SizeBlock << 2) << volume.blocklogshift;
	for (i=-1; b; i++)
		b >>= 1;
	volume.blockshift = i;
	volume.rescluster = 0;
	volume.disksizenative = volume.lastblock - volume.firstblock + 1;
	volume.firstblocknative = volume.firstblock;
	volume.lastblocknative = volume.lastblock;
	volume.firstblock >>= volume.blocklogshift;
	volume.lastblock >>= volume.blocklogshift;
	volume.disksize = volume.lastblock - volume.firstblock + 1;
	volume.lastreserved = volume.disksize - 256;	/* temp value, calculated later */

	volume.status = guiStatus;
	volume.showmsg = guiMsg;
	volume.askuser = guiAskUser;
	volume.progress = guiProgress;
	volume.updatestats = guiUpdateStats;
	volume.getblock = vol_GetBlock;
	volume.writeblock = vol_WriteBlock;
	BCPLtoCString(volume.execdevice, (UBYTE *)BADDR(volume.fssm->fssm_Device));
	volume.execunit = volume.fssm->fssm_Unit;

	if (verbose) {
		UBYTE name[FNSIZE];
		BCPLtoCString(name, (UBYTE *)BADDR(volume.fssm->fssm_Device));
		volume.showmsg("Device: %s:%lu\n", name, volume.fssm->fssm_Unit);
		volume.showmsg("Firstblock: %lu\n", volume.firstblock);
		volume.showmsg("Lastblock : %lu\n", volume.lastblock);
		volume.showmsg("Blocksize : %lu\n", volume.blocksize);
	}

	/* open device */
	if (!OpenDiskDevice(volume.fssm, &volume.port, &volume.request, &t))
	{
		guiMsg("Device could not be opened.\nEXITING ...\n\n");
		return FALSE;
	}

	InitCache(64, 32, volume.blocksize);		/* make this configurable ? */

	detectbuf = AllocVec(MAXRESBLOCKSIZE, MEMF_PUBLIC);
	if (!detectbuf) {
		printf("Could not allocated %ld byte buffer.\n", volume.blocksize);
		return FALSE;
	}
	if (!DetectAccessmode(detectbuf, directscsi)) {
		printf("PFSDoctor failed to access this disk\n"
				"above the 4G boundary after attempting\n"
				"TD64, NSD and Direct SCSI\n");
		FreeVec(detectbuf);
		return FALSE;
	}
	FreeVec(detectbuf);
	printf("Autodetected disk access mode: %s\n", accessmodes[volume.accessmode]);

	if (volume.accessmode == ACCESS_DS)
	{
		volume.getrawblocks = dev_GetBlocksDS;
		volume.writerawblocks = dev_WriteBlocksDS;
	}
	else
	{
		if (volume.accessmode == ACCESS_TD64)
			volume.td64mode = TRUE;
		else if (volume.accessmode == ACCESS_NSD)
			volume.nsdmode = TRUE;
		volume.getrawblocks = dev_GetBlocks;
		volume.writerawblocks = dev_WriteBlocks;
	}

	if (mode == check)
		volume.writerawblocks = dev_WriteBlocksDummy;

	return TRUE;
}

static void CloseVolume(void)
{
	FreeCache();

	if (inhibited)
		Inhibit(targetdevice.name, FALSE);

	if (volume.request)
	{
		if (!(CheckIO((struct IORequest *)volume.request)))
			AbortIO((struct IORequest *)volume.request);
		
		WaitIO((struct IORequest *)volume.request);
		CloseDevice((struct IORequest *)volume.request);
	}

	if (volume.request)
		DeleteIORequest(volume.request);

	if (volume.port)
		DeleteMsgPort(volume.port);

	volume.request = NULL;
	volume.port = NULL;
}

#define TEMPLATE "DEVICE/A,INFO/S,CHECK/S,REPAIR/S,SEARCH/S,UNFORMAT/S,VERBOSE/S,NONINTERACTIVE/S,LOGFILE/K,DIRECTSCSI/S"

#define ARGS_DEVICE 0
#define ARGS_INFO 1
#define ARGS_CHECK 2
#define ARGS_REPAIR 3
#define ARGS_SEARCH 4
#define ARGS_UNFORMAT 5
#define ARGS_VERBOSE 6
#define ARGS_NONINTER 7
#define ARGS_LOGFILE 8
#define ARGS_DIRECTSCSI 9
#define ARGS_SIZE 10


#if 0

static const ULONG schijf[][2] =
{ 
	{20480,20},
	{51200,30},
	{512000,40},
	{1048567,50},
	{10000000,70},
	{0xffffffff,80}
};

#define MAXNUMRESERVED (4096 + 255*1024*8)

static ULONG CalcNumReserved (ULONG total, ULONG resblocksize, ULONG sectorsize)
{
  ULONG temp, taken, i;

  	// temp is the number of reserved blocks if the whole disk is
  	// taken. taken is the actual number of reserved blocks we take.
	temp = total;
	temp /= (resblocksize/512);
	temp *= (sectorsize/512);
	taken = 0;

	for (i=0; temp > schijf[i][0]; i++)
	{
		taken += schijf[i][0]/schijf[i][1];
		temp -= schijf[i][0];
	}
	taken += temp/schijf[i][1];
	taken += 10;
	taken = min(MAXNUMRESERVED, taken);
	taken = (taken + 31) & ~0x1f;		/* multiple of 32 */

	return taken;
}
#endif

int main(int argc, char *argv[])
{
	struct RDArgs *rdarg;
	LONG args[ARGS_SIZE] =  { 0 };
	int cnt = 0;
	uint32 opties;

#if 0
	ULONG taken = 32;
	for (uint32 i = 2048; i ; i <<= 1) {
		ULONG taken2 = CalcNumReserved(i, 1024, 512);
		UWORD m;
		if (i >= 512 * 2048) {
			m = 9;
		} else {
			m = 14;
		}
		taken += taken * m / 16;
		ULONG outtaken = min(MAXNUMRESERVED, taken - 1);
		outtaken = (outtaken + 31) & ~0x1f;		/* multiple of 32 */
		if (i >= 1024 * 2048) {
			printf("%luG: %lu %lu\n", i / (1024 * 2048), outtaken, taken2);		
		} else {
			printf("%luM: %lu %lu\n", i / 2048, outtaken, taken2);
		}
	}
	return 0;
#endif

	if (!(rdarg = ReadArgs (TEMPLATE, args, NULL)))
	{
		PrintFault (ERROR_REQUIRED_ARG_MISSING, "pfsdoctor");
		return RETURN_FAIL;
	}

	strcpy(targetdevice.name, (char*)args[ARGS_DEVICE]);
	if (targetdevice.name[0] && targetdevice.name[strlen(targetdevice.name) - 1] == ':') {
		targetdevice.name[strlen(targetdevice.name) - 1] = 0;
	}

	if (args[ARGS_VERBOSE])
		verbose = TRUE;

	if (args[ARGS_NONINTER])
		noninteractive = TRUE;

	if (args[ARGS_CHECK]) {
		mode = check;
		cnt++;
	}
	if (args[ARGS_REPAIR]) {
		mode = repair;
		cnt++;
	}
	if (args[ARGS_SEARCH]) {
		mode = search;
		cnt++;
	}
	if (args[ARGS_UNFORMAT]) {
		mode = unformat;
		cnt++;
	}
	if (args[ARGS_INFO]) {
		mode = info;
		cnt++;
	}
	
	if (args[ARGS_DIRECTSCSI]) {
		directscsi = TRUE;
	}
	
	if (cnt == 0) {
		printf("INFO, CHECK, REPAIR, SEARCH or UNFORMAT required.\n");
		return RETURN_FAIL;
	}
	if (cnt > 1) {
		printf("Only one command (INFO, CHECK, REPAIR, SEARCH, UNFORMAT) parameter allowed.\n");
		return RETURN_FAIL;
	}
	
	if (args[ARGS_LOGFILE]) {
		logfh = fopen((char*)args[ARGS_LOGFILE], "w");
		if (!logfh) {
			printf("Could not open log file '%s'\n", (char*)args[ARGS_LOGFILE]);
			return RETURN_FAIL;
		}
	}

	if (mode == repair)
		opties = SSF_FIX|SSF_ANALYSE|SSF_GEN_BMMASK;
	else if (mode == unformat)
		opties = SSF_UNFORMAT|SSF_FIX|SSF_ANALYSE|SSF_GEN_BMMASK;
	else if (mode == info)
		opties = SSF_INFO;		
	else
		opties = SSF_CHECK|SSF_ANALYSE|SSF_GEN_BMMASK;
		
	if (verbose)
		opties |= SSF_VERBOSE;

	if (OpenVolume()) {
		StandardScan(opties);
		CloseVolume();
	}

	if (logfh)
		fclose(logfh);

	FreeArgs(rdarg);
	return 0;
}

void pfsfree(void *p)
{
	//ULONG size = ((ULONG*)p)[-1] - 4;
	//printf("pfsfree %p %ld\n", p, size);
	FreeVec(p);
	//printf("ok\n");
}
void *pfsmalloc(size_t a)
{
	void *p;
	
	//printf("pfsmalloc %ld\n", a);
	p  = AllocVec(a, 0);
	//printf("=%p\n", p);
	return p;
}
void *pfscalloc(size_t a, size_t b)
{
	void *p;
	//printf("pfscalloc %ld %ld\n", a, b);
	p  = AllocVec(a * b, MEMF_CLEAR);
	//printf("=%p\n", p);
	return p;
}