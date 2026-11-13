/* $Id: update.c 12.12 1999/05/14 11:31:34 Michiel Exp Michiel $ */
/* $Log: update.c $
 * Revision 12.12  1999/05/14  11:31:34  Michiel
 * Long filename support implemented; bugfixes
 *
 * Revision 12.11  1999/02/22  16:25:30  Michiel
 * Changes for increasing deldir capacity
 *
 * Revision 12.10  1998/09/27  11:26:37  Michiel
 * Adapted for supermode
 * ErrorMsg param
 *
 * Revision 12.9  1998/09/03  07:12:14  Michiel
 * versie 17.4
 * bugfixes 118, 121, 123 and superindexblocks and td64 support
 *
 * Revision 12.8  1998/05/29  19:31:18  Michiel
 * fixed bug 108
 *
 * Revision 12.7  1998/05/22  22:59:58  Michiel
 * Datestamps added
 *
 * Revision 12.6  1997/03/03  22:04:04  Michiel
 * Release 16.21
 *
 * Revision 12.5  1996/03/07  10:03:38  Michiel
 * enforcer hit in CheckUpdate fixed (Working tree)
 *
 * Revision 12.4  1996/01/30  12:49:46  Michiel
 * update of rext->curranseqnr added
 *
 * Revision 12.3  1996/01/03  10:02:59  Michiel
 * reserved rovingpointer added
 *
 * Revision 12.2  1995/11/15  15:56:33  Michiel
 * Rootblock extension update added
 * volume_data datestamp added
 * RemoveEmptyXBlock now calls ResToBeFreed to prevent state overlap
 * CommitReservedToBeFreed() added
 *
 * Revision 12.1  1995/11/07  15:04:07  Michiel
 * Real atomic update:
 * ReservedToBeFreed cache (CommitReservedToBeFreed())
 * Changed update order
 *
 * Revision 11.14  1995/11/02  10:00:31  Michiel
 * using UpdateDataCache (16.2)
 *
 * Revision 11.13  1995/10/03  09:55:22  Michiel
 * rootblock is datestamped at Update()
 *
 * Revision 11.12  1995/07/21  06:55:32  Michiel
 * DELDIR adaptions
 *
 * Revision 11.11  1995/07/11  17:29:31  Michiel
 * ErrorMsg () calls use messages.c variables now.
 *
 * Revision 11.10  1995/07/11  09:23:36  Michiel
 * DELDIR stuff
 *
 * Revision 11.9  1995/05/20  12:12:12  Michiel
 * Updated messages to reflect Ami-FileLock
 * CUTDOWN version
 * protection update
 *
 * Revision 11.8  1995/04/12  19:22:15  Michiel
 * fixed the 'cache inconsistency' bug, caused by
 * an Update() at the wrong moment
 *
 * Revision 11.7  1995/02/28  18:36:55  Michiel
 * Use of blocks_dirty added
 * Update order changed (see 12.6 bugfix)
 *
 * Revision 11.6  1995/02/15  16:43:39  Michiel
 * Release version
 * Using new headers (struct.h & blocks.h)
 *
 * Revision 11.5  1995/02/09  04:54:20  Michiel
 * Bugfix: Makeblockdirty should unlock block to prevent
 * pfs to run out of buffers
 *
 * Revision 11.4  1995/01/29  07:34:57  Michiel
 * Hash table implemented
 *
 * Revision 11.3  1995/01/18  04:29:34  Michiel
 * Bugfixes. Now ready for beta release.
 *
 * Revision 11.2  1995/01/08  16:22:44  Michiel
 * *** empty log message ***
 *
 * Revision 11.1  1995/01/03  17:16:19  Michiel
 * New update algorithm based on reallocation at makeblockdirty time
 * combined with bitmap-reserved allocation
 *
 * Revision 10.4  1994/12/05  08:06:20  Michiel
 * Last version of old algorithm
 *
 * Revision 10.3  1994/11/15  17:52:30  Michiel
 * Bugfix: added a DirectUpdate of anblks after indexblk reallocation
 *
 * Revision 10.2  1994/10/27  11:36:07  Michiel
 * Fixed bug that caused unneeded "Abandoned savewrite .." message
 *
 * Revision 10.1  1994/10/24  11:16:28  Michiel
 * first RCS revision
 * */

#define __USE_SYSBASE

/*
 * includes
 */

#include <exec/types.h>
#include <ctype.h>
#include "debug.h"

#include "blocks.h"
#include "struct.h"
#include "update_protos.h"
#include "volume_protos.h"
#include "disk_protos.h"
#include "allocation_protos.h"
#include "anodes_protos.h"
#include "lru_protos.h"
#include "ass_protos.h"
#include "init_protos.h"

#include "kswrapper.h"

/*
 * prototypes
 */

static void RemoveEmptyABlocks(struct volumedata *volume, globaldata *g);
static void RemoveEmptyDBlocks(struct volumedata *volume, globaldata *g);
static void RemoveEmptyIBlocks(struct volumedata *volume, globaldata *g);
static void RemoveEmptySBlocks(struct volumedata *volume, globaldata *g);
static BOOL GetAnodeOfDBlk(ULONG anodenr, ULONG blocknr, struct canode *anode, ULONG *prev, globaldata *g);
static BOOL IsFirstDBlk(struct cdirblock *blk, globaldata *g);
static BOOL IsEmptyABlk(struct canodeblock *ablk, globaldata *g);
static BOOL IsEmptyIBlk(struct cindexblock *blk, globaldata *g);
static BOOL UpdateList (struct cachedblock *blk, globaldata *g);
static void CommitReservedToBeFreed (globaldata *g);
static BOOL CommitPostRootChanges (globaldata *g);
static BOOL UpdateDirtyBlock (struct cachedblock *blk, globaldata *g);

static BOOL RetryPendingBlocks(struct volumedata *volume, globaldata *g);
static BOOL UpdateBlocknr(struct cachedblock *blk, ULONG newblocknr, globaldata *g);
static BOOL UpdateABLK(struct cachedblock *, ULONG, globaldata *);
static BOOL UpdateDBLK(struct cachedblock *, ULONG, globaldata *);
static BOOL UpdateIBLK(struct cachedblock *, ULONG, globaldata *);
static BOOL UpdateSBLK(struct cachedblock *, ULONG, globaldata *);
static BOOL UpdateBMBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g);
static BOOL UpdateBMIBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g);
#if VERSION23
static void UpdateRBlkExtension (struct cachedblock *blk, ULONG newblocknr, globaldata *g);
#endif
#if DELDIR
static BOOL UpdateDELDIR (struct cachedblock *blk, ULONG newblocknr, globaldata *g);
#endif

/**********************************************************************/
/*                             UPDATEDISK                             */
/*                             UPDATEDISK                             */
/*                             UPDATEDISK                             */
/**********************************************************************/

#define IsFirstABlk(blk) (blk->blk.seqnr == 0)
#define IsFirstIBlk(blk) (blk->blk.seqnr == 0)
#define IsEmptyDBlk(blk) (FIRSTENTRY(blk)->next == 0)

/* indicates current state of update */
#define updateok g->updateok

/*
 * Snapshot to disk.
 * --> remove empty blocks
 * --> update freelist
 * --> save reserved blocks at new location, free the old
 *
 * Result FALSE/TRUE done nothing/volume updated
 */
BOOL UpdateDisk (globaldata *g)
{
  struct DateStamp time;
  struct volumedata *volume = g->currentvolume;
  BOOL success;
  BOOL freelist_applied = FALSE;
  UWORD i;

	ENTER("UpdateDisk");

	if (volume && volume->metadata_error)
	{
		if (!g->updatefailshown)
		{
			ErrorMsg(AFS_ERROR_UPDATE_FAIL, NULL, g);
			g->updatefailshown = TRUE;
		}
		return FALSE;
	}

	/*
	 * Do update
	 */
	if (volume && g->dirty && !g->softprotect)
	{
		/*
		 * For performance reasons avoid concurrent access to same physical
		 * device. Note that the lock can be broken safely, it's only used
		 * to avoid excessive seeking due to competing updates.
		 */
		lock_device_unit(g);

		g->uip = TRUE;
		updateok = TRUE;
		/* flush DiskRead DiskWrite cache; if user data cannot be
		 * written the commit is aborted (and retried later) rather
		 * than committing metadata that references unwritten data
		 */
		if (UpdateDataCache (g))
			updateok = FALSE;

#if VERSION23
		/* make sure rootblockextension is reallocated */
		if (volume->rblkextension)
			MakeBlockDirty ((struct cachedblock *)volume->rblkextension, g);
#endif

		/* commit user space free list */
		if (ValidateFreeList(g))
		{
			freelist_applied = UpdateFreeList(g);
			updateok &= freelist_applied;
		}
		else
			updateok = FALSE;
		updateok &= RetryPendingBlocks(volume, g);

		/* remove empty dir, anode, index and superblocks */
		if (updateok)
		{
			RemoveEmptyDBlocks(volume, g);
			RemoveEmptyABlocks(volume, g);
			RemoveEmptyIBlocks(volume, g);
			RemoveEmptySBlocks(volume, g);
			updateok &= RetryPendingBlocks(volume, g);
		}

		/* update anode, dir, index and superblocks (not changed by UpdateFreeList) */
		for (i=0; i<=HASHM_DIR; i++)
			updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->dirblks[i]), g);
		for (i=0; i<=HASHM_ANODE; i++)
			updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->anblks[i]), g);
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->indexblks), g);
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->superblks), g);
#if DELDIR
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->deldirblks), g);
#endif

#if VERSION23
		if (volume->rblkextension)
		{
			struct crootblockextension *rext = volume->rblkextension;

			/* reserved roving and anode roving */
			rext->blk.reserved_roving = alloc_data.res_roving;
			rext->blk.rovingbit = alloc_data.rovingbit;
			rext->blk.curranseqnr = andata.curranseqnr;

			/* volume datestamp */
			DateStamp (&time);
			rext->blk.volume_date[0] = (UWORD)time.ds_Days;
			rext->blk.volume_date[1] = (UWORD)time.ds_Minute;
			rext->blk.volume_date[2] = (UWORD)time.ds_Tick;
			rext->blk.datestamp = volume->rootblk->datestamp;

			updateok &= UpdateDirtyBlock ((struct cachedblock *)rext, g);
		}
#endif


		/* update bitmap and bitmap index blocks */
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->bmblks), g);
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->bmindexblks), g);

		/* update root (MUST be done last) - this is the commit point,
		 * so its result must be checked as well
		 */
		if (volume->metadata_error)
			updateok = FALSE;
		if (updateok)
			updateok = RawWrite((UBYTE *)volume->rootblk,
				volume->rootblk->rblkcluster, ROOTBLOCK, g) == 0;

		if (updateok)
		{
			/* Now that the new rootblock is on disk, the old locations
			 * of all reallocated reserved blocks are unreferenced and
			 * may become allocatable. Committing the frees only after a
			 * successful root write means the just-written reserved
			 * bitmap is one commit conservative: a crash merely leaks
			 * those blocks until the next successful update, instead of
			 * risking reuse of blocks the on-disk root still references.
			 */
			updateok = CommitPostRootChanges(g);

			/* The primary commit succeeded even if its cleanup write did
			 * not. Advance the in-memory datestamp and flush the device in
			 * either case; on cleanup failure g->dirty and the rootblock
			 * change flag remain set so a later update retries it. The user
			 * free list must not be undone after this point because it was
			 * already part of the successful primary commit.
			 */
			volume->rootblk->datestamp++;
			UpdateAndMotorOff(g);

			if (updateok)
			{
				volume->rootblockchangeflag = FALSE;
				success = TRUE;
				g->dirty = FALSE;
				g->updatefailshown = FALSE;
			}
			else
			{
				if (!g->updatefailshown)
				{
					ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
					g->updatefailshown = TRUE;
				}
				success = FALSE;
			}
		}
		else
		{
			/* The update did not complete: keep g->dirty set so the
			 * timer/flush paths retry, and keep the queued reserved
			 * frees for the next successful commit. The user-space
			 * frees applied by UpdateFreeList are reverted so the
			 * allocator cannot reuse blocks the on-disk tree still
			 * references; the preserved tobefreed list reapplies them
			 * on the next attempt.
			 */
			if (freelist_applied)
				UndoFreeList(g);
			if (!g->updatefailshown)
			{
				ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
				g->updatefailshown = TRUE;
			}
			success = FALSE;
		}

		g->uip = FALSE;

		unlock_device_unit(g);
	}
	else
	{
		if (volume && g->dirty && g->softprotect)
		{
			/* volume is dirty but soft protected (usually error
			 * enforced): keep g->dirty so the data is flushed when the
			 * protection is lifted, but tell the user only once.
			 */
			if (!g->updatefailshown)
			{
				ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
				g->updatefailshown = TRUE;
			}
		}
		else
		{
			g->dirty = FALSE;
		}

		success = FALSE;
	}

	EXIT("UpdateDisk");
	return success;
}

/*
 * Empty Dirblocks 
 */
static void RemoveEmptyDBlocks(struct volumedata *volume, globaldata *g)
{
  struct cdirblock *blk, *next;
  struct canode anode;
  ULONG previous;
  UWORD i;

	for (i=0; i<=HASHM_DIR; i++)
	{
		for (blk = HeadOf(&volume->dirblks[i]); (next=blk->next); blk=next)
		{
			/* The condition and body can cache-miss and evict blocks
			 * via AllocLRU: neither the current block nor the saved
			 * iterator may be the victim, so both are locked (the list
			 * sentinel is not a block and must not be touched).
			 * ISLOCKED is tested before taking our own lock.
			 */
			UWORD nextlock = 0, myoldlock;
			BOOL nextislocked = next->next != NULL;
			BOOL waslocked = ISLOCKED(blk);
			if (nextislocked)
			{
				nextlock = next->used;
				LOCK(next);
			}
			myoldlock = blk->used;
			LOCK(blk);
			if (IsEmptyDBlk(blk) && !IsFirstDBlk(blk, g) && !waslocked )
			{
				if (GetAnodeOfDBlk(blk->blk.anodenr, blk->blocknr, &anode, &previous, g) &&
					anode.blocknr == blk->blocknr &&
					RemoveFromAnodeChain(&anode, previous, blk->blk.anodenr, g))
				{
					MinRemove(blk);
					if (blk->changeflag == TRUE)
					{
						FreeReservedBlock(blk->blocknr, g);
						ResToBeFreed(blk->oldblocknr, g);
					}
					else
						ResToBeFreed(blk->blocknr, g);
					FreeLRU((struct cachedblock *)blk);
				}
				else
				{
					updateok = FALSE;
					blk->used = myoldlock;
				}
			}
			else
			{
				blk->used = myoldlock;
			}
			if (nextislocked)
				next->used = nextlock;
		}
	}
}

/*
 * Walks the anode chain starting at anodenr until the anode of block blocknr.
 * *prev is the anode in front of it (0 for the first one). Returns FALSE if an
 * anode can't be read or the chain loops; otherwise the walk ended at the
 * anode of blocknr or, if the chain doesn't contain it, at the chain tail.
 */
static BOOL GetAnodeOfDBlk(ULONG anodenr, ULONG blocknr, struct canode *anode, ULONG *prev, globaldata *g)
{
  struct anodewalk walk;

	*prev = 0;
	if (!GetAnode(anode, anodenr, g))
		return FALSE;
	walk.mark = anodenr;
	walk.steps = 0;
	while (anode->blocknr != blocknr && anode->next)
	{
		*prev = anode->nr;
		if (!GetNextAnode(anode, anode->next, &walk, g))
			return FALSE;
	}

	return TRUE;
}

static BOOL IsFirstDBlk(struct cdirblock *blk, globaldata *g)
{
  BOOL first;
  struct canode anode;

	first = !GetAnode(&anode, blk->blk.anodenr, g) ||
		anode.blocknr == blk->blocknr;

	return first;
}


/*
 * Empty block check
 */
static void RemoveEmptyIBlocks(struct volumedata *volume, globaldata *g)
{
  struct cindexblock *blk, *next;

	for (blk = HeadOf(&volume->indexblks); (next=blk->next); blk=next)
	{
		/* see RemoveEmptyDBlocks: protect iterator and current block
		 * from eviction (UpdateIBLK can load a superblock)
		 */
		UWORD nextlock = 0, myoldlock;
		BOOL nextislocked = next->next != NULL;
		BOOL waslocked = ISLOCKED(blk);
		if (nextislocked)
		{
			nextlock = next->used;
			LOCK(next);
		}
		myoldlock = blk->used;
		LOCK(blk);
		if (blk->changeflag && !IsFirstIBlk(blk) && IsEmptyIBlk(blk,g) && !waslocked )
		{
			if (UpdateIBLK((struct cachedblock *)blk, 0, g))
			{
				MinRemove(blk);
				if (blk->changeflag == TRUE)
				{
					FreeReservedBlock(blk->blocknr, g);
					ResToBeFreed(blk->oldblocknr, g);
				}
				else
					ResToBeFreed(blk->blocknr, g);
				FreeLRU((struct cachedblock *)blk);
			}
			else
			{
				updateok = FALSE;
				blk->used = myoldlock;
			}
		}
		else
		{
			blk->used = myoldlock;
		}
		if (nextislocked)
			next->used = nextlock;
	}
}

static void RemoveEmptySBlocks(struct volumedata *volume, globaldata *g)
{
  struct cindexblock *blk, *next;

	for (blk = HeadOf(&volume->superblks); (next=blk->next); blk=next)
	{
		/* see RemoveEmptyDBlocks: protect the iterator from eviction */
		UWORD nextlock = 0;
		BOOL nextislocked = next->next != NULL;
		if (nextislocked)
		{
			nextlock = next->used;
			LOCK(next);
		}
		if (blk->changeflag && !IsFirstIBlk(blk) && IsEmptyIBlk(blk, g) && !ISLOCKED(blk) )
		{
			if (UpdateSBLK((struct cachedblock *)blk, 0, g))
			{
				MinRemove(blk);
				if (blk->changeflag == TRUE)
				{
					FreeReservedBlock(blk->blocknr, g);
					ResToBeFreed(blk->oldblocknr, g);
				}
				else
					ResToBeFreed(blk->blocknr, g);
				FreeLRU((struct cachedblock *)blk);
			}
			else
				updateok = FALSE;
		}
		if (nextislocked)
			next->used = nextlock;
	}
}

static void RemoveEmptyABlocks(struct volumedata *volume, globaldata *g)
{
  struct canodeblock *blk, *next;
  ULONG indexblknr, indexoffset;
  UWORD i;
  struct cindexblock *index;

	for (i=0; i<=HASHM_ANODE; i++)
	{
		for (blk = HeadOf(&volume->anblks[i]); (next=blk->next); blk=next)
		{
			/* see RemoveEmptyDBlocks: protect iterator and current
			 * block from eviction (GetIndexBlock can cache-miss)
			 */
			UWORD nextlock = 0, myoldlock;
			BOOL nextislocked = next->next != NULL;
			BOOL waslocked = ISLOCKED(blk);
			if (nextislocked)
			{
				nextlock = next->used;
				LOCK(next);
			}
			myoldlock = blk->used;
			LOCK(blk);
			if (blk->changeflag && !IsFirstABlk(blk) && IsEmptyABlk(blk, g) && !waslocked )
			{
				indexblknr  = ((struct canodeblock *)blk)->blk.seqnr / andata.indexperblock;
				indexoffset = ((struct canodeblock *)blk)->blk.seqnr % andata.indexperblock;

				/* remove the index reference first: GetIndexBlock can
				 * cache-miss (the block is NOT guaranteed to be cached)
				 * and must not run after the anodeblock has been freed
				 */
				index = GetIndexBlock(indexblknr, g);
				DBERR(if (!index) ErrorTrace(5,"RemoveEmptyABlocks", "GetIndexBlock returned NULL!"));
				if (index)
				{
					MakeBlockDirty((struct cachedblock *)index, g);
					if (index->changeflag != CBLK_DIRTY_PENDING)
					{
						index->blk.index[indexoffset] = 0;
						MinRemove(blk);
						if (blk->changeflag == TRUE)
						{
							FreeReservedBlock(blk->blocknr, g);
							ResToBeFreed(blk->oldblocknr, g);
						}
						else
							ResToBeFreed(blk->blocknr, g);
						FreeLRU((struct cachedblock *)blk);
					}
					else
					{
						updateok = FALSE;
						blk->used = myoldlock;
					}
				}
				else
				{
					/* index block unavailable: keep the anodeblock,
					 * it will be retried next update */
					blk->used = myoldlock;
					updateok = FALSE;
				}
			}
			else
			{
				blk->used = myoldlock;
			}
			if (nextislocked)
				next->used = nextlock;
		}
	}
}

static BOOL IsEmptyABlk(struct canodeblock *ablk, globaldata *g)
{
  struct anode *anodes;
  ULONG j; 
  BOOL found = 0;

	/* zoek bezette anode */
	anodes = ablk->blk.nodes;
	for(j=0; j<andata.anodesperblock && !found; j++)
		found |= (anodes[j].blocknr != 0);

	found = !found;
	return found;       /* not found -> empty */
}

static BOOL IsEmptyIBlk(struct cindexblock *blk, globaldata *g)
{
  ULONG *index;
  UWORD i;
  BOOL found = 0;

	index = blk->blk.index;
	for(i=0; i<andata.indexperblock; i++)
	{
		found = index[i] != 0;
		if (found)
			break;
	}

	found = !found;
	return found;
}

static BOOL UpdateList (struct cachedblock *blk, globaldata *g)
{
  ULONG error;
  struct cbitmapblock *blk2;

	if (!updateok)
		return FALSE;

	while (blk->next)
	{
		if (blk->changeflag == CBLK_DIRTY_PENDING)
			return FALSE;
		if (blk->changeflag)
		{
			/* Queue the old location instead of freeing it directly:
			 * it is still referenced by the on-disk rootblock and must
			 * not become allocatable until the commit (root write) has
			 * actually succeeded. CommitReservedToBeFreed runs after it.
			 */
			ResToBeFreed (blk->oldblocknr, g);
			blk2 = (struct cbitmapblock *)blk;
			blk2->blk.datestamp = blk2->volume->rootblk->datestamp;
			blk->oldblocknr = 0;
			error = RawWrite ((UBYTE *)&blk->data, RESCLUSTER, blk->blocknr, g);
			if (error)
				goto update_error;

			blk->changeflag = FALSE;
		}

		blk=blk->next;
	}

	return TRUE;

  update_error:
	if (!g->updatefailshown)
	{
		ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
		g->updatefailshown = TRUE;
	}
	return FALSE;
}

static BOOL UpdateDirtyBlock (struct cachedblock *blk, globaldata *g)
{
  ULONG error;

	if (!updateok)
		return FALSE;

	if (blk->changeflag == CBLK_DIRTY_PENDING)
		return FALSE;
	if (blk->changeflag)
	{
		/* see UpdateList: keep the old location reserved until the
		 * commit has succeeded */
		ResToBeFreed (blk->oldblocknr, g);
		blk->oldblocknr = 0;
		error = RawWrite ((UBYTE *)&blk->data, RESCLUSTER, blk->blocknr, g);
		if (error)
		{
			if (!g->updatefailshown)
			{
				ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
				g->updatefailshown = TRUE;
			}
			return FALSE;
		}
	}

	blk->changeflag = FALSE;
	return TRUE;
}

static void CommitReservedToBeFreed (globaldata *g)
{
	int i;
	for (i=0;i<alloc_data.rtbf_index;i++)
	{
		if (alloc_data.reservedtobefreed[i])
		{
			FreeReservedBlock (alloc_data.reservedtobefreed[i], g);
			alloc_data.reservedtobefreed[i] = 0;
		}
	}

	alloc_data.rtbf_index = 0;
}

/* Finish a successful primary root commit. The metadata locations queued in
 * reservedtobefreed are no longer referenced, so release them and persist the
 * resulting reserved bitmap with a root-only write. If there were no reserved
 * frees, the primary root write already contains the complete state.
 */
static BOOL CommitPostRootChanges (globaldata *g)
{
	struct volumedata *volume = g->currentvolume;
	BOOL cleanup_needed = alloc_data.rtbf_index != 0;

	CommitReservedToBeFreed(g);
	CommitFreeList(g);

	if (!cleanup_needed)
		return TRUE;

	return RawWrite((UBYTE *)volume->rootblk,
		volume->rootblk->rblkcluster, ROOTBLOCK, g) == 0;
}

/* Check if an update is needed (see atomic.fw)
 * rtbf_threshold indicates how full the rtbf cache is allowed to be,
 * normally RTBF_THRESHOLD, RTBF_POSTPONED_TH for intermediate checks.
 */
void CheckUpdate (ULONG rtbf_threshold, globaldata *g)
{
	if (g->currentvolume &&
	   ((alloc_data.rtbf_index > rtbf_threshold) ||
	    (alloc_data.tobefreed_index > TBF_THRESHOLD) ||
		(g->rootblock->reserved_free < RESFREE_THRESHOLD + 5 + alloc_data.tbf_resneed)))
	{
		UpdateDisk (g);

		if (g->rootblock->reserved_free <= RESFREE_THRESHOLD)
			alloc_data.res_alert = TRUE;
		else
			alloc_data.res_alert = FALSE;
	}
}


/**********************************************************************/
/*                            MAKEBLOCKDIRTY                          */
/*                            MAKEBLOCKDIRTY                          */
/*                            MAKEBLOCKDIRTY                          */
/**********************************************************************/

/* --> part of update
 * marks a directory or anodeblock dirty. Nothing happens if it already
 * was dirty. If it wasn't, the block will be reallocated and marked dirty.
 * If the reallocation fails, an error is displayed.
 *
 * result: TRUE = was clean; FALSE: was already dirty
 * 
 * LOCKing the block until next packet proves to be too restrictive,
 * so unlock afterwards.
 */
BOOL MakeBlockDirty (struct cachedblock *blk, globaldata *g)
{
  ULONG blocknr;
  UWORD oldlock;

	if (!blk->changeflag || blk->changeflag == CBLK_DIRTY_PENDING)
	{
		g->dirty = TRUE;
		oldlock = blk->used;
		LOCK(blk);

		blocknr = AllocReservedBlock (g);
		if (blocknr)
		{
			blk->oldblocknr = blk->blocknr;
			blk->blocknr = blocknr;
			if (!UpdateBlocknr (blk, blocknr, g))
			{
				blk->blocknr = blk->oldblocknr;
				blk->oldblocknr = 0;
				FreeReservedBlock(blocknr, g);
				blk->changeflag = CBLK_DIRTY_PENDING;
				blk->volume->pendingblocks = TRUE;
			}
		}
		else
		{
#ifdef BETAVERSION
			ErrorMsg(AFS_BETA_WARNING_2, NULL, g);
#endif
			blk->changeflag = CBLK_DIRTY_PENDING;
			blk->volume->pendingblocks = TRUE;
		}

		blk->used = oldlock;    // unlock block
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

/* Relocation can fail after callers have modified a cached block. Keep those
 * contents pinned and retry linking them before writing any metadata/root.
 * Restart each scan because relocation may rehash the current block.
 */
static BOOL RetryPendingLists(struct MinList *list, UWORD count, BOOL *pending, globaldata *g)
{
	struct cachedblock *blk;

	for (; count; count--, list++)
		for (blk = (struct cachedblock *)HeadOf(list); blk->next; blk = blk->next)
			if (blk->changeflag == CBLK_DIRTY_PENDING)
			{
				*pending = TRUE;
				MakeBlockDirty(blk, g);
				if (blk->changeflag == CBLK_DIRTY_PENDING)
					return FALSE;
				break;
			}
	return TRUE;
}

static BOOL RetryPendingBlocks(struct volumedata *volume, globaldata *g)
{
	BOOL pending;

	if (!volume->pendingblocks)
		return TRUE;
	do
	{
		pending = FALSE;
		if (!RetryPendingLists((struct MinList *)volume->dirblks, HASHM_DIR + 1, &pending, g) ||
			!RetryPendingLists((struct MinList *)volume->anblks, HASHM_ANODE + 1, &pending, g) ||
			!RetryPendingLists((struct MinList *)&volume->indexblks, 1, &pending, g) ||
			!RetryPendingLists((struct MinList *)&volume->superblks, 1, &pending, g) ||
			!RetryPendingLists((struct MinList *)&volume->bmblks, 1, &pending, g) ||
			!RetryPendingLists((struct MinList *)&volume->bmindexblks, 1, &pending, g)
#if DELDIR
			|| !RetryPendingLists((struct MinList *)&volume->deldirblks, 1, &pending, g)
#endif
			)
			return FALSE;
#if VERSION23
		if (volume->rblkextension &&
			volume->rblkextension->changeflag == CBLK_DIRTY_PENDING)
		{
			pending = TRUE;
			MakeBlockDirty((struct cachedblock *)volume->rblkextension, g);
			if (volume->rblkextension->changeflag == CBLK_DIRTY_PENDING)
				return FALSE;
		}
#endif
	} while (pending);
	volume->pendingblocks = FALSE;
	return TRUE;
}

static BOOL UpdateBlocknr (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	switch (((UWORD *)blk->data)[0])
	{
		case DBLKID:    /* dirblock */
			return UpdateDBLK (blk, newblocknr, g);

		case ABLKID:    /* anodeblock */
			return UpdateABLK (blk, newblocknr, g);

		case IBLKID:    /* indexblock */
			return UpdateIBLK (blk, newblocknr, g);

		case BMBLKID:   /* bitmapblock */
			return UpdateBMBLK (blk, newblocknr, g);

		case BMIBLKID:  /* bitmapindexblock */
			return UpdateBMIBLK (blk, newblocknr, g);

#if VERSION23
		case EXTENSIONID:   /* rootblockextension */
			UpdateRBlkExtension (blk, newblocknr, g);
			break;
#endif

#if DELDIR
		case DELDIRID:  /* deldir */
			return UpdateDELDIR (blk, newblocknr, g);
#endif

		case SBLKID:	/* superblock */
			return UpdateSBLK (blk, newblocknr, g);
		default:
			return FALSE;
	}
	return TRUE;
}


static BOOL UpdateDBLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cdirblock *dblk = (struct cdirblock *)blk;
  struct canode anode;
  ULONG oldblocknr = dblk->oldblocknr;
  ULONG prev;

	LOCK(blk);

	/* get old anode (all 1-block anodes) */
	if (!GetAnodeOfDBlk(dblk->blk.anodenr, oldblocknr, &anode, &prev, g))
		return FALSE;

	/* change it.. */
	if (anode.blocknr != oldblocknr)
	{
		DB(Trace(4, "UpdateDBLK", "anode.blocknr=%ld, dblk->blocknr=%ld\n",
			anode.blocknr, dblk->blocknr));
		ErrorMsg (AFS_ERROR_CACHE_INCONSISTENCY, NULL, g);
		return FALSE;
	}

	/* This must happen AFTER anode correction, because Update() could be called,
	 * causing trouble (invalid checkpoint: dirblock uptodate, anode not)
	 */
	anode.blocknr = newblocknr;
	if (!TrySaveAnode(&anode, anode.nr, g))
		return FALSE;
	blk->changeflag = TRUE;

	ReHash(blk, g->currentvolume->dirblks, HASHM_DIR);
	return TRUE;
}

static BOOL UpdateABLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cindexblock *index;
  ULONG indexblknr, indexoffset, temp;

	temp = ((struct canodeblock *)blk)->blk.seqnr;
	if (!andata.indexperblock || temp > 0xffffUL)
		return FALSE;
	indexblknr  = temp / andata.indexperblock;
	indexoffset = temp % andata.indexperblock;

	/* this one should already be in the cache */
	index = GetIndexBlock(indexblknr, g);

	DBERR(if (!index) ErrorTrace(5,"UpdateABLK", "GetIndexBlock returned NULL!"));
	if (!index)
		return FALSE;
	MakeBlockDirty ((struct cachedblock *)index, g);
	if (index->changeflag == CBLK_DIRTY_PENDING)
		return FALSE;
	index->blk.index[indexoffset] = newblocknr;
	blk->changeflag = TRUE;
	ReHash(blk, g->currentvolume->anblks, HASHM_ANODE);
	return TRUE;
}

static BOOL UpdateIBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cindexblock *superblk;
  ULONG temp;

	if (g->supermode)
	{
		if (!andata.indexperblock ||
			((struct cindexblock *)blk)->blk.seqnr / andata.indexperblock > MAXSUPER)
			return FALSE;
		temp = divide (((struct cindexblock *)blk)->blk.seqnr, andata.indexperblock);
		superblk = GetSuperBlock (temp /* & 0xffff */, g);

		DBERR(if (!superblk) ErrorTrace(5,"UpdateIBLK", "GetSuperBlock returned NULL!"));
		if (!superblk)
			return FALSE;
		MakeBlockDirty ((struct cachedblock *)superblk, g);
		if (superblk->changeflag == CBLK_DIRTY_PENDING)
			return FALSE;
		superblk->blk.index[temp >> 16] = newblocknr;
	}
	else
	{
		if (((struct cindexblock *)blk)->blk.seqnr > MAXSMALLINDEXNR)
			return FALSE;
		blk->volume->rootblk->idx.small.indexblocks[((struct cindexblock *)blk)->blk.seqnr] = newblocknr;
		blk->volume->rootblockchangeflag = TRUE;
	}
	blk->changeflag = TRUE;
	return TRUE;
}

static BOOL UpdateSBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	/* Use MakeBlockDirty (cf. UpdateDELDIR): setting the changeflag
	 * directly would skip the copy-on-write reallocation of the
	 * rootblockextension, and UpdateDisk would then overwrite it at the
	 * location the on-disk rootblock still references, before the
	 * rootblock itself is committed.
	 */
	if (!blk->volume->rblkextension ||
		((struct cindexblock *)blk)->blk.seqnr > MAXSUPER)
		return FALSE;
	MakeBlockDirty ((struct cachedblock *)blk->volume->rblkextension, g);
	if (blk->volume->rblkextension->changeflag == CBLK_DIRTY_PENDING)
		return FALSE;
	blk->volume->rblkextension->blk.superindex[((struct cindexblock *)blk)->blk.seqnr] = newblocknr;
	blk->changeflag = TRUE;
	return TRUE;
}

static BOOL UpdateBMBLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cindexblock *indexblock;
  struct cbitmapblock *bmb = (struct cbitmapblock *)blk;
  ULONG temp;

	if (!andata.indexperblock ||
		bmb->blk.seqnr / andata.indexperblock >
			(g->supermode ? MAXBITMAPINDEX : MAXSMALLBITMAPINDEX))
		return FALSE;
	temp = divide (bmb->blk.seqnr, andata.indexperblock);
	indexblock = GetBitmapIndex (temp /* & 0xffff */, g);

	DBERR(if (!indexblock) ErrorTrace(5,"UpdateBMBLK", "GetBitmapIndex returned NULL!"));
	if (!indexblock)
		return FALSE;
	MakeBlockDirty ((struct cachedblock *)indexblock, g);   /* recursion !! */
	if (indexblock->changeflag == CBLK_DIRTY_PENDING)
		return FALSE;
	indexblock->blk.index[temp >> 16] = newblocknr;
	blk->changeflag = TRUE;
	return TRUE;
}

/* validness of seqnr is checked when block is loaded */
static BOOL UpdateBMIBLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	if (((struct cindexblock *)blk)->blk.seqnr >
		(g->supermode ? MAXBITMAPINDEX : MAXSMALLBITMAPINDEX))
		return FALSE;
	blk->changeflag = TRUE;
	blk->volume->rootblk->idx.large.bitmapindex[((struct cindexblock *)blk)->blk.seqnr] = newblocknr;
	blk->volume->rootblockchangeflag = TRUE;
	return TRUE;
}

#if VERSION23
static void UpdateRBlkExtension (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	blk->changeflag = TRUE;
	blk->volume->rootblk->extension = newblocknr;
	blk->volume->rootblockchangeflag = TRUE;
}
#endif

#if DELDIR
static BOOL UpdateDELDIR (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	if (!blk->volume->rblkextension ||
		((struct cdeldirblock *)blk)->blk.seqnr > MAXDELDIR)
		return FALSE;
	MakeBlockDirty((struct cachedblock *)blk->volume->rblkextension, g);
	if (blk->volume->rblkextension->changeflag == CBLK_DIRTY_PENDING)
		return FALSE;
	blk->volume->rblkextension->blk.deldir[((struct cdeldirblock *)blk)->blk.seqnr] = newblocknr;
	blk->changeflag = TRUE;
	return TRUE;
}
#endif

/* Update datestamp (copy from rootblock
 * Call before writing block (lru.c)
 */
void UpdateDatestamp (struct cachedblock *blk, globaldata *g)
{
  struct cdirblock *dblk = (struct cdirblock *)blk;
  struct crootblockextension *rext = (struct crootblockextension *)blk;

	switch (((UWORD *)blk->data)[0])
	{
		case DBLKID:    /* dirblock */
		case ABLKID:    /* anodeblock */
		case IBLKID:    /* indexblock */
		case BMBLKID:   /* bitmapblock */
		case BMIBLKID:  /* bitmapindexblock */
		case DELDIRID:  /* deldir */
		case SBLKID:	/* superblock */
			dblk->blk.datestamp = g->currentvolume->rootblk->datestamp;
			break;

		case EXTENSIONID:   /* rootblockextension */
			rext->blk.datestamp = g->currentvolume->rootblk->datestamp;
			break;
	}
}
