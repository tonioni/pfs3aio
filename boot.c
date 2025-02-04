
/* $Id: boot.c 13.5 1999/05/14 11:31:34 Michiel Exp Michiel $ */
/* $Log: boot.c $
 * Revision 13.5  1999/05/14  11:31:34  Michiel
 * Long filename support implemented; bugfixes
 *
 * Revision 13.4  1998/09/27  11:26:37  Michiel
 * Beta version string
 *
 * Revision 13.3  1998/05/27  20:16:13  Michiel
 * AFS --> PFS2
 *
 * Revision 13.2  1997/03/03  22:04:04  Michiel
 * Release 16.21
 *
 * Revision 13.1  1996/03/29  16:57:43  Michiel
 * Improved Quit() which deals properly with locked and open files
 *
 * Revision 12.7  1995/11/07  14:51:26  Michiel
 * call to CheckUpdate added
 *
 * Revision 12.6  1995/11/02  16:34:59  Michiel
 * -- version 16.2
 *
 * Revision 12.5  1995/10/04  14:05:09  Michiel
 * using new memorypool functions (from support.c 10.9)
 *
 * Revision 12.4  1995/09/01  11:17:04  Michiel
 * ErrorMsg adaption (see disk.c and volume.c)
 *
 * Revision 12.3  1995/08/21  04:19:40  Michiel
 * added handler part for MODE_SLEEP
 *
 * Revision 12.2  1995/08/17  08:46:34  Michiel
 * adapted to new dostohandlerinterface
 *
 * Revision 12.1  1995/07/27  12:25:15  Michiel
 * Using new startup code (in assroutines)
 * Includes new die function Quit
 *
 * Revision 10.17  1995/07/11  17:29:31  Michiel
 * ErrorMsg () calls use messages.c variables now.
 *
 * Revision 10.16  1995/07/10  04:55:17  Michiel
 * StackSwap V36 didn't work due to compiler problems.
 * Now implemented in assembler (AssRoutines.asm)
 *
 * Revision 10.15  1995/07/07  14:43:08  Michiel
 * LITE stuff and
 * Stackswap for V36
 *
 * Revision 10.14  1995/06/23  17:26:10  Michiel
 * added use of global g->action
 *
 * Revision 10.13  1995/06/23  11:35:46  Michiel
 * multiuser stuff
 *
 * Revision 10.12  1995/06/15  18:56:53  Michiel
 * pooled mem
 *
 * Revision 10.11  1995/06/08  15:20:53  Michiel
 * multiuser changes
 *
 * Revision 10.10  1995/05/20  12:12:12  Michiel
 * Updated messages to reflect Ami-FileLock
 * CUTDOWN version
 * protection update
 *
 * Revision 10.9  1995/03/30  18:56:54  Michiel
 * Handling of notify reply messages added
 *
 * Revision 10.8  1995/02/15  16:43:39  Michiel
 * Release version
 * Using new headers (struct.h & blocks.h)
 *
 * Revision 10.7  1995/01/18  04:29:34  Michiel
 * Bugfixes. Now ready for beta release.
 *
 * Revision 10.6  1994/11/17  15:57:48  Michiel
 * Beta2
 *
 * Revision 10.5  1994/11/15  18:20:08  Michiel
 * Immediately checks exec version now
 *
 * Revision 10.4  1994/11/08  11:14:53  Michiel
 * Applied 9.5.4 fix (timeron/timeout bug)
 *
 * Revision 10.3  1994/10/29  08:49:24  Michiel
 * changed process references to msgport references
 *
 * Revision 10.2  1994/10/27  11:28:24  Michiel
 * *** empty log message ***
 *
 * Revision 10.1  1994/10/24  11:16:28  Michiel
 * first RCS revision
 * */

/* Boot: Het omvattende programma */
//#define DEBUG 1

#include "versionhistory.doc"

/* LEVELS:
** 1 = Boot.c
** 2 = DosToHandlerInterface
** 3 = Everything dirictly called from DTHI loop
** 4 = Higher
*/

#define RES1(p) (p->dp_Res1)
#define RES2(p) (p->dp_Res2)
#define __USE_SYSBASE

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/alerts.h>
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <devices/trackdisk.h>
#include <devices/timer.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#if MULTIUSER
#include <libraries/multiuser.h>
#include <proto/multiuser.h>
#endif
#ifdef __MORPHOS__
#define muFSRendezVous() \
	LP0(0x6C, BOOL, muFSRendezVous, \
	, MULTIUSER_BASE_NAME, IF_CACHEFLUSHALL, NULL, 0, IF_CACHEFLUSHALL, NULL, 0)
#endif

#ifdef __SASC
#include <dos.h>
#endif
#include <stdio.h>
#include <string.h>
#include "debug.h"

#ifdef DEBUG
BOOL debug=FALSE;
#endif

#include "blocks.h"
#include "struct.h"
#include "directory_protos.h"
#include "volume_protos.h"
#include "init_protos.h"
#include "disk_protos.h"
#include "update_protos.h"
#include "ass_protos.h"
#include "lock_protos.h"
#include "lru_protos.h"

#include "kswrapper.h"

/* protos */
// extern void SAVEDS EntryWithNewStack(void);
void SAVEDS EntryPoint(void);
void NormalCommands(struct DosPacket *, globaldata *);
void HandleSleepMsg (globaldata *g);
void ReturnPacket(struct DosPacket *, struct MsgPort *, globaldata *);
static void Quit(globaldata *);

/* vars */
#ifdef BETAVERSION
CONST UBYTE version[] = "$VER: PFS-III " REVISION " BETA (" REVDATE ") "
	"written by Michiel Pelt and copyright (c) 1994-2012 Peltin BV";
#else
#if MULTIUSER
CONST UBYTE version[] = "$VER: " "Professional-File-System-III " REVISION " MULTIUSER-VERSION (" REVDATE ") "
	 "written by Michiel Pelt and copyright (c) 1994-2012 Peltin BV";
#else
CONST UBYTE version[] = "$VER: " "Professional-File-System-III " REVISION " PFS3AIO-VERSION (" REVDATE ") "
	 "written by Michiel Pelt and copyright (c) 1994-2012 Peltin BV";
#endif
#endif

CONST UBYTE shortname[] = "pfs3aio";

#if MULTIUSER
CONST struct muExtOwner NOBODY = {0,0,0};
#endif  


/* proto */
static void SetTimer(int, globaldata *);

#if MULTIUSER
static BOOL FindInLibraryList (CONST_STRPTR, globaldata *);
#endif

/**********************************************************************/
/*                               DEBUG                                */
/**********************************************************************/
#ifdef DEBUG
static UBYTE debugbuf[120];
#define DebugOn debug += 1
#define DebugOff debug = 0
#define DebugMsg(msg) NormalErrorMsg(msg, NULL)
#define DebugMsgNum(msg, num) sprintf(debugbuf, "%s %ld.", msg, num); \
			if(debug) {NormalErrorMsg(debugbuf, NULL); debug=0;}
#define DebugMsgName(msg, name) sprintf(debugbuf, "%s >%s<.", msg, name); \
			if(debug) {NormalErrorMsg(debugbuf, NULL); debug=0;}
#else
#define DebugOn
#define DebugOff
#define DebugMsg(m)
#define DebugMsgNum(msg,num)
#define DebugMsgName(msg, name)
#endif

/**********************************************************************/
/*                                MAIN                                */
/*                                MAIN                                */
/*                                MAIN                                */
/**********************************************************************/

void SAVEDS EntryPoint (void)
{
	/* globals */
	struct globaldata *g;
	struct MsgPort *msgport;
	struct DosPacket *pkt;
	struct DosPacket *diepkt = NULL;
	struct DeviceNode *devnode;
	struct FileSysStartupMsg *fssm;
	struct Message *msg;
	UBYTE *mountname;
	ULONG signal, dossig, timesig, notifysig, sleepsig, waitmask;
#undef SysBase
	struct ExecBase *SysBase;

	SysBase =  *((struct ExecBase **)4);

	/* init globaldata */
	g = AllocMem(sizeof(struct globaldata), MEMF_CLEAR);
	if (!g)
	{
		Alert (AG_NoMemory);
		Wait (0);
	}
	g->g_SysBase = SysBase;
	g->largeDiskSafeOS = SysBase->LibNode.lib_Version >= OS_VERSION_SAFE_LARGE_DISK;

	/* open libs */
	IntuitionBase = (APTR)OpenLibrary ("intuition.library", MIN_LIB_VERSION);
	DOSBase = (struct DosLibrary *)OpenLibrary ("dos.library", MIN_LIB_VERSION);
#ifdef KSWRAPPER
	g->v37DOS = DOSBase->dl_lib.lib_Version >= 37;
	g->v37EXEC = SysBase->LibNode.lib_Version >= 37;
	g->v39EXEC = SysBase->LibNode.lib_Version >= 39;
	/* This must not cause indirect DOS call! */
	if (g->v37DOS && g->v37EXEC)
#endif
	UtilityBase = OpenLibrary ("utility.library",0L);
	msgport = &((struct Process *)FindTask (NULL))->pr_MsgPort;

	if (!IntuitionBase ||
#ifndef KSWRAPPER
		!UtilityBase ||
#endif
		!DOSBase)
	{
		NormalErrorMsg (AFS_ERROR_LIBRARY_PROBLEM, NULL, 1);
		Wait (0);
	}

	//DebugMsg("Requester debug enabled");

	/* get startpacket */
	WaitPort (msgport);
	msg = GetMsg (msgport);
	pkt = (struct DosPacket *)msg->mn_Node.ln_Name;

	/* The startpakket contains:
	 *
	 * ARG1 = BSTR to mount name
	 * ARG2 = Value from dn_Startup
	 * ARG3 = BPTR to DeviceNode
	 */

#ifdef KSWRAPPER
	FixStartupPacket(pkt, g);
#endif

	mountname = (UBYTE *)BADDR(pkt->dp_Arg1);
	fssm = (struct FileSysStartupMsg *)BADDR(pkt->dp_Arg2);
	devnode = (struct DeviceNode *)BADDR(pkt->dp_Arg3);

	/* Enter Process ID, so that following references
	 * to our handler do not generate new processes
	 */
	devnode->dn_Task = msgport;

	DB(Trace(1,"boot","g=%lx\n",g));
	if (!Initialize ((DSTR)mountname, fssm, devnode, g))
	{
		NormalErrorMsg (AFS_ERROR_INIT_FAILED, NULL, 1);
		RES1(pkt) = DOSFALSE;
		RES2(pkt) = 0;
		ReturnPacket (pkt, msgport, g);
		goto terminate;
	}

	g->DoCommand = NormalCommands;  //%4.5
	g->inhibitcount = 0;

	/* send startuppacket back */
	RES1(pkt) = DOSTRUE;
	ReturnPacket (pkt, msgport, g);

	dossig = 1 << msgport->mp_SigBit;
	timesig = 1 << g->timeport->mp_SigBit;
	notifysig = 1 << g->notifyport->mp_SigBit;
#if EXTRAPACKETS
	sleepsig = 1 << g->sleepport->mp_SigBit;
	waitmask = sleepsig | dossig | timesig | notifysig | g->diskchangesignal | g->resethandlersignal;
#else
	waitmask = dossig | timesig | notifysig | g->diskchangesignal | g->resethandlersignal;
#endif
	g->timeout = 0;
	// Set to no disk. Mount below can get delayed if dos list is locked.
	g->disktype = ID_NO_DISK_PRESENT;

#if MULTIUSER
	g->muFS_ready = FALSE;
#endif

#define SysBase g->g_SysBase

	g->newvolumepending = TRUE; // initial new volume mount

	while (1)
	{
		if (g->newvolumepending) {
			signal = dossig;
		} else {
			signal = Wait(waitmask);
		}

		// Handle this here because dos list could be locked which would
		// cause deadlock. Process any pending packet(s) and retry.
		if (((signal & g->diskchangesignal) && g->inhibitcount<=0) || g->newvolumepending)
		{
			//DebugMsg("DISKCHANGE SIGNAL!!");
			DB(Trace(1, "boot", "DiskChange\n"));
			if (AttemptLockDosList(LDF_VOLUMES | LDF_WRITE)) {
				NewVolume(TRUE, g);     /* %10 set to TRUE */
				g->newvolumepending = FALSE;
				UnLockDosList(LDF_VOLUMES | LDF_WRITE);
			} else {
				// list was locked, process pending packet(s) without waiting and try again
				g->newvolumepending = TRUE;
				Delay(5);
			}
		}
		
#if MULTIUSER
		if (!g->muFS_ready)
		{
			if (FindInLibraryList ((CONST_STRPTR) "multiuser.library", g) &&
				(muBase = (APTR)OpenLibrary ("multiuser.library", 39)))
			{
				muFSRendezVous ();
				g->muFS_ready = TRUE;
			}
		}
#endif

		if (signal & timesig)
		{
			if ((msg = GetMsg(g->timeport)))
			{
				if (g->inhibitcount == 0)
				{
					/* postpone until timeoutcounter 'timeout' is 0 */
					if (g->timeout)
					{
						SetTimer(700000, g);
						g->timeout--;
					}
					else
					{
						struct idlehandle *idle;
						if (g->dirty)
						{
							/* Update disk but wait with turning out motor */
							UpdateDisk(g);
							SetTimer(200000, g);
							for (idle = HeadOf(&g->idlelist); idle->next; idle = idle->next)
								Signal (idle->task, 1L<<idle->dirtysignal);
						}
						else
						{
							UpdateAndMotorOff(g);
							g->timeron = FALSE;
							for (idle = HeadOf(&g->idlelist); idle->next; idle = idle->next)
								Signal (idle->task, 1L<<idle->cleansignal);

						}
					}
				}
			}
		}

		if (signal & dossig)
		{
			while ((msg = GetMsg(msgport)))
			{
				g->action = pkt = (struct DosPacket *)msg->mn_Node.ln_Name;

				DB(if(pkt->dp_Type != ACTION_EXAMINE_NEXT && pkt->dp_Type != ACTION_IS_FILESYSTEM))
					DB(Trace(2, "boot", "Pakket %ld\n", pkt->dp_Type));

#if MULTIUSER
				/* get current task owner */
				if (g->muFS_ready)
					g->user = muGetTaskExtOwner (pkt->dp_Port->mp_SigTask);
				else
					g->user = (APTR) &NOBODY;
#endif
				/* handle the packet */
				(g->DoCommand) (pkt, g);    //%4.5

				/* unlock all blocks locked by DoCommand */
				UNLOCKALL();

#if MULTIUSER
				/* free structure previously allocated by muGetTaskOwner */
				if (g->muFS_ready)
					muFreeExtOwner (g->user);
#endif  

				if (!g->timeron && (g->timeout || g->dirty))
				{
					/* set timer for first period (always extended) */
					SetTimer(200000, g);
					g->timeron = TRUE;
				}

				if (g->dieing) {
					CheckUpdate (RTBF_CHECK_TH, g);
					if (pkt->dp_Type == ACTION_DIE)
						diepkt = pkt;
					goto terminate;
				}
	
				ReturnPacket (pkt, msgport, g);
				CheckUpdate (RTBF_CHECK_TH, g);
			}
		}

		if (signal & notifysig)
		{
		  struct NotifyMessage *nmsg;
		  struct NotifyRequest *nr;

			while ((nmsg = (struct NotifyMessage *)GetMsg (g->notifyport)))
			{
				nr = nmsg->nm_NReq;
				if (nr->nr_Flags & NRF_MAGIC)
				{
					nr->nr_Flags ^= NRF_MAGIC;
					PutMsg (nr->nr_stuff.nr_Msg.nr_Port, &nmsg->nm_ExecMessage);
				}
				else
				{
					nmsg->nm_NReq->nr_MsgCount--;
					FreeMemP (nmsg, g);
				}
			}
		}

#if EXTRAPACKETS
		if (signal & sleepsig)
		{
			HandleSleepMsg (g);
		}
#endif

		if (signal & g->resethandlersignal)
		{
			if (g->inhibitcount == 0)
			{
				/* Update pending changes to disk before letting the system to reboot */
				UpdateDisk(g);
			}

			/* Make sure any disk buffers have been flushed to disk */
			UpdateAndMotorOff(g);

			// Enable write protect
			g->softprotect = 1;
			g->protectkey = ~0;

			/* Mark the reset handler as done (system might reboot right after) */
			HandshakeResetHandler(g);
		}
	}

terminate:

	Quit (g);

	g->devnode->dn_Task = NULL;
	
	if (diepkt)
		ReturnPacket (diepkt, msgport, g);

#if MULTIUSER
	if (muBase)
		CloseLibrary((struct Library *) muBase);
#endif
	if (UtilityBase)
		CloseLibrary(UtilityBase);
	CloseLibrary((struct Library *) DOSBase);
	CloseLibrary((struct Library *) IntuitionBase);

	FreeMem(g, sizeof(struct globaldata));
}

void ReturnPacket (struct DosPacket *packet, struct MsgPort *sender, globaldata *g)
{
  struct MsgPort *rec = packet->dp_Port;

	packet->dp_Link->mn_Node.ln_Name = (UBYTE*)packet;
	packet->dp_Link->mn_Node.ln_Succ = NULL;
	packet->dp_Link->mn_Node.ln_Pred = NULL;
	packet->dp_Port = sender;
	PutMsg (rec, packet->dp_Link);
}

static void SetTimer (int micros, globaldata *g)
{
	g->trequest->tr_node.io_Command = TR_ADDREQUEST;
	g->trequest->tr_time.tv_secs     = 0;
	g->trequest->tr_time.tv_micro    = micros;
	SendIO ((struct IORequest *)g->trequest);
}

#if MULTIUSER
static BOOL FindInLibraryList (CONST_STRPTR name, globaldata *g)
{
  struct Node *n;
#ifdef __MORPHOS__
	n = FindExecNode(EXECLIST_LIBRARY, name);
#else
	Forbid();
	n = FindName(&SysBase->LibList, (STRPTR)name);
	Permit();
#endif
	return (BOOL)(n != 0);
}
#endif

/* ACTION_DIE */

static void Quit (globaldata *g)
{
  struct volumedata *volume = NULL;
  struct NotifyMessage *nmsg;
  struct NotifyRequest *nr;
  struct Message *msg;
#if UNSAFEQUIT
	BOOL waited = FALSE;
#endif

	// DebugMsg("ACTION_DIE");
	ENTER("dd_Quit");

	UpdateDisk (g);

#if MULTIUSER
	if (g->user->uid != muROOT_UID)
		return;
#endif

	UninstallResetHandler(g);

	/* remove diskchangehandler */
	UninstallDiskChangeHandler(g);

	for (;;) {
		struct volumedata *v = g->currentvolume;
		if (v) {
			/* 'remove' disk */
			if (SafeDiskRemoveSequence(g)) {
				volume = v;
			} else {
				Delay(5);
			}
		}
#if UNSAFEQUIT
		/* wait for wb to return locks */
		if (!waited) {
			Delay(50);
			waited = TRUE;
		}
#endif
		/* check if packets queued */
		while (g->msgport && (msg = GetMsg(g->msgport)))
		{
			g->action = (struct DosPacket *)msg->mn_Node.ln_Name;
			g->action->dp_Res1 = DOSFALSE;
			g->action->dp_Res2 = ERROR_DEVICE_NOT_MOUNTED;
			ReturnPacket (g->action, g->msgport, g);
		}
		if (!g->currentvolume)
			break;
	}

	/* check if notifypackets queued */
	while (g->notifyport && (nmsg = (struct NotifyMessage *)GetMsg (g->notifyport)))
	{
		nr = nmsg->nm_NReq;
		if (nr->nr_Flags & NRF_MAGIC)
		{
			nr->nr_Flags ^= NRF_MAGIC;
			PutMsg (nr->nr_stuff.nr_Msg.nr_Port, &nmsg->nm_ExecMessage);
		}
		else
		{
			nmsg->nm_NReq->nr_MsgCount--;
			FreeMemP (nmsg, g);
		}
	}

	Forbid ();

	/* remove devicenode */
	if (g->devnode)
		RemDosEntry ((struct DosList *)g->devnode);

	/* cleanup timer device (OK) */
	/* FreeSignal wil even niet..(signalnr!) */
	if (g->trequest) {
		if(!(CheckIO((struct IORequest *)g->trequest)))
			AbortIO((struct IORequest *)g->trequest);
		WaitIO((struct IORequest *)g->trequest);
		CloseDevice((struct IORequest *)g->trequest);
	}
	DeleteIORequest((struct IORequest *)g->trequest);
	DeleteMsgPort(g->timeport);

	/* clean up device */
	if (g->request) {
		if(!(CheckIO((struct IORequest *)g->request)))
			AbortIO((struct IORequest *)g->request);
		WaitIO((struct IORequest *)g->request);
		CloseDevice((struct IORequest *)g->request);
	}
	DeleteIORequest((struct IORequest *)g->request);
	DeleteMsgPort(g->port);

	DeleteMsgPort(g->notifyport);
#if EXTRAPACKETS
	DeleteMsgPort(g->sleepport);
#endif

	FreeVec (g->mountname);
	FreeMemP (g->geom, g);
	FreeDataCache(g);
	DeallocLRU(g);
	if (alloc_data.reservedtobefreed)
		FreeMem (alloc_data.reservedtobefreed, sizeof(*alloc_data.reservedtobefreed) * alloc_data.rtbf_size);

	Permit ();

#if UNSAFEQUIT
	/* wait 'till all locks freed */
	while (volume && !IsMinListEmpty(&volume->fileentries))
	{
		listentry_t *listentry;

		msg = GetMsg (g->msgport);
		if (!msg) {
			Wait (1 << g->msgport->mp_SigBit);
		}
		g->action = (struct DosPacket *)msg->mn_Node.ln_Name;
		if (g->action->dp_Type == ACTION_FREE_LOCK)
		{
			listentry = ListEntryFromLock (g->action->dp_Arg1);
			if (listentry)
				RemoveListEntry (listentry, g);
			g->action->dp_Res1 = DOSTRUE;
		}
		else if (g->action->dp_Type == ACTION_END)
		{
			listentry = (listentry_t *)g->action->dp_Arg1;
			if (listentry)
				RemoveListEntry (listentry, g);
			g->action->dp_Res1 = DOSTRUE;
		}
		else
		{
			g->action->dp_Res1 = DOSFALSE;
			g->action->dp_Res2 = ERROR_DEVICE_NOT_MOUNTED;
		}

		ReturnPacket (g->action, g->msgport, g);
	}
#endif

	LibDeletePool (g->bufferPool);
	LibDeletePool (g->mainPool);
}
