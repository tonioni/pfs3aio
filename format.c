/* $Id: format.c 11.29 1999/05/14 11:21:33 Michiel Exp Michiel $ */
/* $Log: format.c $
 * Revision 11.29  1999/05/14  11:21:33  Michiel
 * Bigger reserved area (2x)
 * MODE_LONGFN
 *
 * Revision 11.28  1999/03/09  10:26:35  Michiel
 * 00136, 00137: 1024 byte sector support
 * 00114: reserved roving
 * 00110: release number define
 *
 * Revision 11.27  1999/02/22  16:27:09  Michiel
 * Changes for increasing deldir capacity
 *
 * Revision 11.26  1998/10/05  16:30:48  Michiel
 * fixed version number error in requester
 *
 * Revision 11.25  1998/09/27  11:26:37  Michiel
 * fixed supermode related bugs
 *
 * Revision 11.24  1998/09/03  07:12:14  Michiel
 * versie 17.4
 * bugfixes 118, 121, 123 and superindexblocks and td64 support
 *
 * Revision 11.23  1998/06/13  01:46:22  Michiel
 * show format requester longer
 * fixed bug 117
 * patched bug 118
 *
 * Revision 11.22  1998/05/29  19:31:18  Michiel
 * datestamp initially 1
 *
 * Revision 11.21  1998/05/27  20:16:13  Michiel
 * MODE_DATESTAMP added
 *
 * Revision 11.20  1996/03/07  10:09:48  Michiel
 * format bugs fixed
 *
 * Revision 11.19  1995/12/28  13:47:48  Michiel
 * rext->afsversion bugfix
 * small changes
 *
 * Revision 11.18  1995/11/15  15:47:45  Michiel
 * Creation of rootblock extension added (MakeRBlkExtension)
 *
 * Revision 11.17  1995/10/20  10:12:08  Michiel
 * Anode reserved area adaptions (16.3)
 *
 * Revision 11.16  1995/10/03  09:59:05  Michiel
 * MakeDeldir --> g->dirty
 *
 * Revision 11.15  1995/08/21  04:24:13  Michiel
 * better checks for out of memory
 *
 * Revision 11.14  1995/08/04  04:13:52  Michiel
 * deldirblock protection now is DELENTRY_PROT
 * CUTDOWN/AFSLITE protection: limited number of reserved blocks
 *
 * Revision 11.13  1995/07/21  06:59:31  Michiel
 * DELDIR adaptions
 *
 * Revision 11.12  1995/07/11  17:29:31  Michiel
 * ErrorMsg () calls use messages.c variables now.
 *
 * Revision 11.11  1995/07/11  09:23:36  Michiel
 * DELDIR stuff
 *
 * Revision 11.10  1995/07/07  14:39:17  Michiel
 * AFSLITE stuff
 *
 * Revision 11.9  1995/06/20  11:56:58  Michiel
 * Error induced softprotect uit
 *
 * Revision 11.8  1995/06/16  10:02:42  Michiel
 * using Allec & FreeBufMem
 *
 * Revision 11.7  1995/06/15  18:56:53  Michiel
 * pooled mem
 *
 * Revision 11.6  1995/05/20  12:12:12  Michiel
 * Updated messages to reflect Ami-FileLock
 * CUTDOWN version
 * protection update
 *
 * Revision 11.5  1995/04/13  13:18:06  Michiel
 * Extra options added to rootblock
 *
 * Revision 11.4  1995/02/15  16:43:39  Michiel
 * Release version
 * Using new headers (struct.h & blocks.h)
 *
 * Revision 11.3  1995/02/13  03:38:14  Michiel
 * Added 10 reserved blocks.
 *
 * Revision 11.2  1995/01/18  04:29:34  Michiel
 * Bugfixes. Now ready for beta release.
 *
 * Revision 11.1  1995/01/08  16:20:16  Michiel
 * New diskformat. Compiled.
 *
 * Revision 10.2  1994/11/15  17:52:30  Michiel
 * __USE_SYSBASE moved..
 *
 * Revision 10.1  1994/10/24  11:16:28  Michiel
 * first RCS revision
 * */

/*
 * General includes
 */

#define __USE_SYSBASE

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <dos/filehandler.h>
#include <intuition/intuition.h>
#include <proto/intuition.h>
#include "debug.h"
#include <math.h>
#include <string.h>

/*
 * Own includes
 */
#include "blocks.h"
#include "struct.h"
#include "format_protos.h"
#include "disk_protos.h"
#include "directory_protos.h"
#include "allocation_protos.h"
#include "volume_protos.h"
#include "anodes_protos.h"
#include "init_protos.h"
#include "update_protos.h"
#include "lru_protos.h"
#include "versionhistory.doc"

#include "kswrapper.h"

/*
 * Contents
 */
static void ShowVersion (globaldata *g);
static ULONG MakeBootBlock(globaldata *g);
static rootblock_t *MakeRootBlock (DSTR diskname, ULONG rsblocks, globaldata *g);
static void MakeBitmap (globaldata *g);
static void MakeRootDir (globaldata *g);
static ULONG CalcNumReserved (globaldata *g, ULONG resblocksize);
static void MakeReservedBitmap (struct rootblock **rbl, ULONG numreserved, globaldata *g);
static crootblockextension_t *MakeFormatRBlkExtension (struct rootblock *rbl, globaldata *g);

/**********************************************************************/
/*                               FORMAT                               */
/*                               FORMAT                               */
/*                               FORMAT                               */
/**********************************************************************/

BOOL FDSFormat (DSTR diskname, LONG disktype, SIPTR *error, ULONG rsblocks, globaldata *g)
{
  struct rootblock *rootblock;
  struct volumedata *volume;
  struct crootblockextension *rext;
  ULONG err;
  ULONG i;

	ENTER("FDSFormat");

#if DELDIR
	g->deldirenabled = FALSE;
#endif

	/* remove error-induced soft protect */
	if (g->softprotect < 0) {
		*error = ERROR_DISK_WRITE_PROTECTED;
		return DOSFALSE;
	}

	if (g->softprotect && g->protectkey == ~0)
		g->softprotect = g->protectkey = 0;

	/* update dos envec and geom */
	GetDriveGeometry (g);

	ULONG *env = (ULONG*)g->dosenvec;

	// logical blocksize = size_block * sectors per block
	ULONG cbs = (env[DE_SIZEBLOCK] << 2) * env[DE_SECSPERBLK];
	CalculateBlockSize(g, 0, cbs);

	ShowVersion (g);

	/* issue 00118: disk cannot exceed MAX_DISK_SIZE */
	if (g->geom->dg_TotalSectors > MAXDISKSIZE) {
		*error = ERROR_OBJECT_TOO_LARGE;
		return DOSFALSE;
	}

	// Only 512, 1024, 2048 and 4096 block sizes are supported.
	// Last two require new large partition mode.
	if (g->geom->dg_SectorSize < 512 || g->geom->dg_SectorSize > 4096 || cbs > 4096) { 
		*error = ERROR_BAD_NUMBER;
		return DOSFALSE;
	}
	
	// block size might have changed
	// (formatting old pfs3 partition that ignored DE_SECSPERBLK)
	// reallocate data cache
	if (!InitDataCache(g)) {
		*error = ERROR_NO_FREE_STORE;
		return DOSFALSE;	
	}

	err = MakeBootBlock (g);
	if (err != 0) {
		*error = err;
 		return DOSFALSE;
	}

	if (!(rootblock = MakeRootBlock (diskname, rsblocks, g))) {
		*error = ERROR_NO_FREE_STORE;
		return DOSFALSE;
	}

	/*  make volumedata BEFORE rext ! (bug 00135) */
	g->currentvolume = volume = MakeVolumeData (rootblock, g);

	/* add extension */
	if (!(rext = MakeFormatRBlkExtension (rootblock, g))) {
		*error = ERROR_NO_FREE_STORE;
		return DOSFALSE;			// rootblock extension could not be created
	}

	volume->rblkextension = rext;
	rootblock->options |= MODE_EXTENSION;
	InitModules (volume, TRUE, g);

	MakeBitmap (g);
	
	do {
		i = AllocAnode (0, g);
	} while (i<ANODE_ROOTDIR-1);

	MakeRootDir (g);

#if DELDIR
	/* add deldir */
	SetDeldir(2, g);
	rootblock->options |= MODE_DELDIR | MODE_SUPERDELDIR;
	g->dirty = TRUE;
#endif

	UpdateDisk (g);
	FreeVolumeResources (volume, g);
	g->currentvolume = NULL;

	return DOSTRUE;
}


/*
 * Protection and cutdown check
 */
static void ShowVersion (globaldata *g)
{
  /* data needed for requester */
  static const struct EasyStruct req =
  {
	sizeof(struct EasyStruct),
	0,
	"Professional File System 3 V" RELEASE,
	(STRPTR)FORMAT_MESSAGE,
	"OK"
  };

  ULONG iflags = IDCMP_INACTIVEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_INTUITICKS;
  ULONG regnr = 0;
  struct Window	*window;
  ULONG rec_idcmp, retval, tick;

#ifdef KSWRAPPER
	if (!g->v37DOS)
		return;
#endif

	/*
	 * Show  copyright message
	 */
	tick = 0;
	window = BuildEasyRequestArgs (NULL, &req, iflags, &regnr);
	while ( (retval = SysReqHandler (window, &rec_idcmp, TRUE )) != 0 )
	{
		if (retval == -1)
		{
			if (rec_idcmp & IDCMP_INTUITICKS)
			{
				tick++;
				rec_idcmp = 0;
				if (tick == 60)
					break;
			}
			else
			{
				break;
			}
		}
	}
	FreeSysRequest (window);
}



/*
 * creates & writes the two bootblocks
 */
static ULONG MakeBootBlock (globaldata *g)
{
  struct bootblock *bbl;
  ULONG error;

	if (!(bbl = AllocBufmem (2 * BLOCKSIZE, g)))
		return ERROR_NO_FREE_STORE;

#if ACCESS_DETECT
	if (!detectaccessmode((UBYTE*)bbl, g))
		return ERROR_OBJECT_TOO_LARGE;
#endif

	memset (bbl, 0, 2*BLOCKSIZE);
	bbl->disktype = ID_PFS_DISK;
	error = RawWrite ((UBYTE *)bbl, 2, BOOTBLOCK1, g);
	FreeBufmem (bbl, g);
	return error;
}

/*
 * creates rootblock (does not write)
 * including reserved bitmap
 * (will be written by Update and freed by FreeVolumeRes.)
 */
static rootblock_t *MakeRootBlock (DSTR diskname, ULONG rsblocks, globaldata *g)
{
	struct rootblock *rbl;
	struct DateStamp time;
	ULONG numreserved;
	ULONG rescluster;
	ULONG resblocksize;
	ULONG options = 0, disktype = ID_PFS_DISK;
	ULONG *env = (ULONG*)g->dosenvec;

	g->supermode = 0;

	// determine reserved blocksize and logical blocksize
	resblocksize = 1024;
	if (g->geom->dg_TotalSectors > MAXSMALLDISK)
	{
		options |= MODE_SUPERINDEX;
		g->supermode = 1;
		if (g->geom->dg_TotalSectors > MAXDISKSIZE1K) {
			resblocksize = 2048;
			if (g->geom->dg_TotalSectors > MAXDISKSIZE2K) {
				resblocksize = 4096;
			}
		}
	}

	// resblocksize is always at least same as logical block size
	if (resblocksize < g->blocksize_phys) {
		resblocksize = g->blocksize_phys;
	}
	if (g->blocksize > resblocksize) {
		resblocksize = g->blocksize;
	}

	// resblock size > 1024 or logical block > 512: new format type
	if (resblocksize > 1024 || g->blocklogshift) {
		disktype = ID_PFS2_DISK;
	}
	// max supported resblock size is 4096
	if (resblocksize > 4096) {
		resblocksize = 4096;
	}

	if (!(rbl = AllocBufmem (BLOCKSIZE, g)))
		return NULL;

	memset (rbl, 0, BLOCKSIZE);
	DateStamp (&time);

	rbl->disktype = disktype;
#ifdef SIZEFIELD
	rbl->options = MODE_HARDDISK | MODE_SPLITTED_ANODES | MODE_DIR_EXTENSION |
				   MODE_SIZEFIELD | MODE_DATESTAMP | MODE_EXTROVING |
				   MODE_LONGFN;
	rbl->disksize = g->geom->dg_TotalSectors;
#else
	rbl->options = MODE_HARDDISK | MODE_SPLITTED_ANODES | MODE_DIR_EXTENSION |
				   MODE_DATESTAMP | MODE_EXTROVING | MODE_LONGFN;
#endif
	rbl->options |= options;

	// Store geometry if partition only drive or new format type
	if (env[DE_LOWCYL] == 0 || disktype == ID_PFS2_DISK) {
		rbl->options |= MODE_STORED_GEOM;
	}

	if (!InitLRU(g, resblocksize)) {
		FreeBufmem(rbl, g);
		return NULL;
	}

	// show experimental warning if new format
	if (rbl->disktype == ID_PFS2_DISK) {
		g->supermode = 1;
		rbl->options |= MODE_SUPERINDEX;
		ULONG args[3];
		args[0] = g->blocksize_phys;
		args[1] = BLOCKSIZE;
		args[2] = resblocksize;
		NormalErrorMsg(AFS_WARNING_EXPERIMENTAL_DISK, args, 1);
	}

	rescluster = resblocksize/BLOCKSIZE;
	rbl->reserved_blksize = resblocksize;

#if LARGE_FILE_SIZE > 1
	rbl->options |= MODE_LARGEFILE;
	rbl->disktype = ID_PFS2_DISK;
	NormalErrorMsg(AFS_WARNING_EXPERIMENTAL_FILE, NULL, 1);
#endif

	rbl->datestamp = 1;
	rbl->creationday = (UWORD)time.ds_Days;
	rbl->creationminute = (UWORD)time.ds_Minute;
	rbl->creationtick = (UWORD)time.ds_Tick;
	rbl->protection	= 0xf0;
	if (rsblocks == 0) {
		numreserved = CalcNumReserved (g, resblocksize);
	} else {
		numreserved = (rsblocks + 31) & ~31;
		if (numreserved < 32) {
			numreserved = 32;
		}
	}
	rbl->firstreserved = 2;
	rbl->lastreserved = rescluster*numreserved + rbl->firstreserved - 1;
	rbl->reserved_free = numreserved;
	rbl->blocksfree = (g->geom->dg_TotalSectors >> g->blocklogshift) - rescluster*numreserved - rbl->firstreserved;
	rbl->alwaysfree = rbl->blocksfree/20;
	// rbl->roving_ptr = 0;

	memcpy(rbl->diskname, diskname, min(*diskname+1, DNSIZE));
	MakeReservedBitmap(&rbl, numreserved, g);	// sets reserved_free & rblkcluster too

	return rbl;
}

/* Create rootblockextension. Needed for AFS 2.3 options.
 */
BOOL MakeRBlkExtension (globaldata *g)
{
  struct volumedata *volume = g->currentvolume;
  ULONG blocknr;

	if (!(blocknr = AllocReservedBlock (g)))
		return FALSE;

	volume->rootblk->extension = blocknr;
	if (!(volume->rblkextension = MakeFormatRBlkExtension(volume->rootblk, g))) {
		FreeReservedBlock (blocknr, g);
		return FALSE;
	}

	volume->rootblk->options |= MODE_EXTENSION;
	volume->rootblockchangeflag = TRUE;
	return TRUE;
}

static crootblockextension_t *MakeFormatRBlkExtension (struct rootblock *rbl, globaldata *g)
{
  crootblockextension_t *rext;

	if (!(rext = AllocBufmem (sizeof(struct cachedblock) + rbl->reserved_blksize, g)))
		return FALSE;

	memset (rext, 0, sizeof(struct cachedblock) + rbl->reserved_blksize);

	rext->volume				= g->currentvolume;
	rext->blocknr				= rbl->extension;
	// rext->oldblocknr			= 0;
	rext->changeflag			= TRUE;
	rext->blk.id 		  		= EXTENSIONID;
	// rext->blk.ext_options 	= 0;
	rext->blk.pfs2version       = (VERNUM<<16) + REVNUM;
	rext->blk.root_date[0]      = rbl->creationday;
	rext->blk.root_date[1]      = rbl->creationminute;
	rext->blk.root_date[2]      = rbl->creationtick;
	// rext->blk.volume_date[0] = 0;	/* volume date, initially 0: see gurubook */
	// rext->blk.volume_date[1] = 0;
	// rext->blk.volume_date[2] = 0;
	// rext->blk.tobedone currently zero 
	// rext->reserved_roving initially zero
	rext->blk.fnsize			= 32;

	// Store geometry
	ULONG *env = (ULONG*)g->dosenvec;
	int size = env[0] > 16 ? 16 : env[0];
	rext->blk.dosenvec[0] = size;
	memcpy(&rext->blk.dosenvec[1], &env[1], size * sizeof(ULONG));

	g->dirty = TRUE;
	return rext;
}

static void MakeBitmap (globaldata *g)
{
  ULONG i;

	/* use no_bmb as calculated by InitAllocation */
	for (i=0;i<alloc_data.no_bmb;i++)
		NewBitmapBlock (i,g);
}

static void MakeRootDir (globaldata *g)
{
  struct cdirblock *blk;
  ULONG blocknr, anodenr;

	blocknr = AllocReservedBlock (g);
	anodenr = AllocAnode (0, g);
	blk = MakeDirBlock (blocknr, anodenr, anodenr, 0, g);
}

// returns number of reserved blocks needed
// TODO: this method is 2T limited
static ULONG CalcNumReserved (globaldata *g, ULONG resblocksize)
{
	ULONG temp, taken;

	temp = g->geom->dg_TotalSectors;
	taken = 32;
	for (ULONG i = 2048; i && i / 2 < temp; i <<= 1) {
		UWORD m = 14;
		if (i >= 512 * 2048) {
			m = 10;
		}
		taken += taken * m / 16;
	}
	taken >>= (g->blockshift - 9);
	taken = min(MAXNUMRESERVED, taken - 1);
	taken = (taken + 31) & ~0x1f;		/* multiple of 32 */
	return taken;
}

/* makes reserved bitmap and allocates rootblockextension */
static void MakeReservedBitmap (struct rootblock **rbl, ULONG numreserved, globaldata *g)
{
  struct bitmapblock *bmb;
  struct rootblock *newrootblock;
  ULONG *bitmap, numblocks, i, last, cluster, rescluster;

	/* calculate number of 1024 byte blocks */
	numblocks = 1;
	for(i=125; i<numreserved/32; i+=256)
		numblocks++;

	// convert to number of reserved blocks and allocate
	numblocks = (1024*numblocks + (*rbl)->reserved_blksize - 1) / ((*rbl)->reserved_blksize);
	(*rbl)->reserved_free -= numblocks;

	// convert to number of sectors
	rescluster = ((*rbl)->reserved_blksize) / BLOCKSIZE;
	cluster = (*rbl)->rblkcluster = rescluster * numblocks;

	/* reallocate rootblock */
	newrootblock = AllocBufmemR(cluster << BLOCKSHIFT, g);
	memset (newrootblock, 0, cluster << BLOCKSHIFT);
	memcpy (newrootblock, *rbl, BLOCKSIZE);
	FreeBufmem(*rbl, g);
	*rbl = newrootblock;
	bmb = (bitmapblock_t *)(*rbl+1);		/* bitmap directly behind rootblock */

	/* init bitmapblock header */
	bmb->id = BMBLKID;
	bmb->seqnr = 0;

	/* fill bitmap */
	bitmap = bmb->bitmap;
	for (i = 0; i<numreserved/32; i++)
		*bitmap++ = ~0;

	/* the last border */
	last = 0;
	for (i=0; i < numreserved%32; i++)
		last |= 0x80000000>>i;
	*bitmap = last;

	/* allocate taken blocks + rootblock extension (de + 1)
	 * The reserved area starts with the rootblock.
	 * Convert numblocks from 1K blocks to actual reserved area blocks
	 * */
	for (i=0; i < numblocks + 1; i++)
		bmb->bitmap[i/32] ^= 0x80000000>>(i%32);

	/* rootblock extension position */
	(*rbl)->extension = (*rbl)->firstreserved + cluster;
	(*rbl)->reserved_free--;
}
