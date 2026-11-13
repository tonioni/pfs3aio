/* $Id: anodes.c 13.9 1998/09/27 11:26:37 Michiel Exp Michiel $ */
/* $Log: anodes.c $
 * Revision 13.9  1998/09/27  11:26:37  Michiel
 * bugfixes
 * ErrorMsg param
 *
 * Revision 13.8  1998/09/03  07:12:14  Michiel
 * versie 17.4
 * bugfixes 118, 121, 123 and superindexblocks and td64 support
 *
 * Revision 13.7  1997/03/03  22:04:04  Michiel
 * Release 16.21
 *
 * Revision 13.6  1996/03/07  10:10:14  Michiel
 * format bug (wt)
 * realloc anodebitmap bug (wt)
 *
 * Revision 13.5  1996/01/30  12:50:15  Michiel
 * --- working tree overlap ---
 * anodebitmap implemented
 *
 * Revision 13.4  1995/11/07  14:50:07  Michiel
 * NewAnodeBlock: lock clear after use
 * GetAnodeBlock: no lock on indexblock
 *
 * Revision 13.3  1995/10/20  10:10:58  Michiel
 * Anode reserved area adaptions (16.3)
 * --> AllocAnode changed
 *
 * Revision 13.2  1995/10/04  09:00:30  Michiel
 * Bug in CorrectAnodeAC (instruction order) fixed
 *
 * Revision 13.1  1995/10/03  10:48:07  Michiel
 * merged develop tree: anodecache
 *
 * Revision 12.9  1995/07/11  17:29:31  Michiel
 * ErrorMsg () calls use messages.c variables now.
 *
 * Revision 12.8  1995/06/19  09:48:44  Michiel
 * small optimise and a couple of debug statements in GetAnodeBlock
 *
 * Revision 12.7  1995/06/04  06:06:38  Michiel
 * assembly divide function used
 *
 * Revision 12.6  1995/02/28  18:30:49  Michiel
 * Splitted mode and UWORD optimize
 *
 * Revision 12.5  1995/02/15  16:43:39  Michiel
 * Release version
 * Using new headers (struct.h & blocks.h)
 *
 * Revision 12.4  1995/01/29  07:34:57  Michiel
 * Reserved Raw read/write and locking change
 *
 * Revision 12.3  1995/01/24  09:52:21  Michiel
 * Cache hashing added
 *
 * Revision 12.2  1995/01/18  04:29:34  Michiel
 * Bugfixes. Now ready for beta release.
 *
 * Revision 12.1  1995/01/08  15:41:09  Michiel
 * Compiled
 *
 * Revision 11.2  1994/12/30  11:21:40  Michiel
 * adapted to bitmaps
 * Compilable
 *
 * Revision 11.1  1994/12/01  11:31:25  Michiel
 * Added indexblocks.
 * Uses new anodes.h include.
 *
 * Revision 10.2  1994/10/27  11:33:30  Michiel
 * Added some MakeBlockDirty() calls in anode routines
 *
 * Revision 10.1  1994/10/24  11:16:28  Michiel
 * first RCS revision
 * */

/* 9-10-94  Added usage of g->curranblk.
 *          Added InitAnodes()
 * */

#define __USE_SYSBASE

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <stdlib.h>
#include "debug.h"

#include "blocks.h"
#include "struct.h"
#include "anodes_protos.h"
#include "allocation_protos.h"
#include "disk_protos.h"
#include "lru_protos.h"
#include "update_protos.h"
#include "ass_protos.h"
#include "volume_protos.h"

#include "kswrapper.h"

/*
 * Contents (local functions)
 */
static struct anodechain *MakeAnodeChain (ULONG anodenr, globaldata *g);
static struct anodechain *FindAnodeChain (ULONG anodenr, globaldata *g);
static void FreeAnodeChain (struct anodechain *chain, globaldata *g);
static struct canodeblock *big_GetAnodeBlock(UWORD seqnr, globaldata *g);
static struct canodeblock *big_NewAnodeBlock(UWORD , globaldata * );
static struct cindexblock *NewIndexBlock(UWORD , globaldata * );
static struct cindexblock *NewSuperBlock (UWORD seqnr, globaldata *g);
static void MakeAnodeBitmap (BOOL formatting, globaldata *g);
static BOOL ReallocAnodeBitmap (ULONG newseqnr, globaldata *g);


/*********************************************************************/
/* Anode cache functions                                             */
/*********************************************************************/

/*
 * Get anodechain of anodenr, making it if necessary.
 * Returns chain or NULL for failure
 */
struct anodechain *GetAnodeChain(ULONG anodenr, globaldata *g)
{
	struct anodechain *ac;

	if (!(ac = FindAnodeChain(anodenr, g)))
		ac = MakeAnodeChain(anodenr, g);
	if (ac)
		ac->refcount++;

	return ac;
}

/*
 * Called when a reference to an anodechain ceases to exist
 */
void DetachAnodeChain(struct anodechain *chain, globaldata *g)
{
	chain->refcount--;
	if (!chain->refcount)
		FreeAnodeChain(chain, g);
}


/*
 * Follows an anode chain: reads the anode that is linked from the previous
 * one. Fails if it was already visited by this walk (Brent's cycle detection),
 * so a corrupt chain that loops back (also onto itself) cannot be followed
 * endlessly. Valid chains never revisit an anode, so they are never rejected.
 */
BOOL GetNextAnode(struct canode *anode, ULONG anodenr, struct anodewalk *walk, globaldata *g)
{
	if (anodenr == walk->mark)
		return FALSE;
	walk->steps++;
	if (!(walk->steps & (walk->steps - 1)))
		walk->mark = anodenr;
	return GetAnode(anode, anodenr, g);
}

/*
 * makes anodechain
 */
static struct anodechain *MakeAnodeChain(ULONG anodenr, globaldata *g)
{
	struct anodechain *ac;
	struct anodechainnode *node, *newnode;
	struct anodewalk walk;

	ENTER("MakeAnodeChain");
	if (!(ac = AllocMemP(sizeof(struct anodechain), g)))
		return NULL;

	ac->refcount = 0;
	ac->next = NULL;
	node = &ac->head;
	node->next = NULL;
	if (!GetAnode(&node->an, anodenr, g))
		goto failure;
	walk.mark = anodenr;
	walk.steps = 0;
	while (node->an.next)
	{
		if (!(newnode = AllocMemP(sizeof(struct anodechainnode), g)))
			goto failure;
		newnode->next = NULL;
		node->next = newnode;
		if (!GetNextAnode(&newnode->an, node->an.next, &walk, g))
			goto failure;
		node = newnode;
	}

	MinAddHead(&g->currentvolume->anodechainlist, ac);
	return ac;
	
failure:
	FreeAnodeChain(ac, g);  
	return NULL;
}


/*
 * search anodechain. 
 * Return anodechain found, or 0 if not found
 */
static struct anodechain *FindAnodeChain (ULONG anodenr, globaldata *g)
{
  struct anodechain *chain;

	ENTER("FindAnodeChain");
	for (chain = HeadOf(&g->currentvolume->anodechainlist); chain->next; chain=chain->next)
	{
		if (chain->head.an.nr == anodenr)
			return chain;
	}

	return NULL;
}

/*
 * Free an anodechain. Anodechain will be removed from list if it is
 * in the list.
 */
static void FreeAnodeChain (struct anodechain *chain, globaldata *g)
{
	struct anodechainnode *node, *nextnode;

	ENTER("FreeAnodeChain");
	for (node=chain->head.next; node; node=nextnode)
	{
		nextnode = node->next;
		FreeMemP (node, g);
	}

	if (chain->next)
		MinRemove (chain);

	FreeMemP (chain, g);
}

/*
 * Tries to fetch the block that follows after anodeoffset. Returns success,
 * updates anodechainpointer and anodeoffset. If failed, acnode will point to
 * the tail of the anodechain
 */
BOOL NextBlockAC (struct anodechainnode **acnode, ULONG *anodeoffset, globaldata *g)
{
	(*anodeoffset)++;
	return CorrectAnodeAC (acnode, anodeoffset, g);
}

/* 
 * Correct anodeoffset overflow. Corrects anodechainnode pointer pointed to by acnode.
 * Returns success. If correction was not possible, acnode will be the tail of the
 * anodechain. Anodeoffset is updated to point to a block within the current
 * anodechainnode.
 */
BOOL CorrectAnodeAC (struct anodechainnode **acnode, ULONG *anodeoffset, globaldata *g)
{
	while (*anodeoffset >= (*acnode)->an.clustersize)
	{
		if (!(*acnode)->next)
			return DOSFALSE;

		*anodeoffset -= (*acnode)->an.clustersize;
		*acnode = (*acnode)->next;
	}

	return DOSTRUE;
}


/*********************************************************************/
/* Main functions                                                    */
/*********************************************************************/

/*
 * Tries to fetch the block that follows after anodeoffset. Returns
 * success and anodeoffset is updated.
 */
BOOL NextBlock (struct canode *anode, ULONG *anodeoffset, globaldata *g)
{
	(*anodeoffset)++;
	return CorrectAnode(anode, anodeoffset, g);
}

/* 
 * Correct anodeoffset overflow
 */
BOOL CorrectAnode (struct canode *anode, ULONG *anodeoffset, globaldata *g)
{
	struct anodewalk walk;

	walk.mark = anode->nr;
	walk.steps = 0;
	while(*anodeoffset >= anode->clustersize)
	{
		if(!anode->next)
			return DOSFALSE;

		*anodeoffset -= anode->clustersize;
		if (!GetNextAnode(anode, anode->next, &walk, g))
			return DOSFALSE;
	}

	return DOSTRUE;
}

/*
 * Retrieve an anode from disk
 */
BOOL GetAnode (struct canode *anode, ULONG anodenr, globaldata *g)
{
  ULONG temp;
  UWORD seqnr, anodeoffset;
  struct canodeblock *ablock;

	if (!andata.anodesperblock ||
		(!g->anodesplitmode && (UWORD)(anodenr >> 16) >= andata.anodesperblock))
		goto failed;
	if(g->anodesplitmode)
	{
		anodenr_t *split = (anodenr_t *)&anodenr;
		seqnr = split->seqnr;
		anodeoffset = split->offset;
	}
	else
	{
		temp		 = divide(anodenr, andata.anodesperblock);
		seqnr        = temp;				// 1e block = 0
		anodeoffset  = temp >> 16;
	}
	
	anode->nr = anodenr;
	ablock = anodeoffset < andata.anodesperblock ? GetAnodeBlock(seqnr, g) : NULL;
	if(ablock)
	{
		anode->clustersize = ablock->blk.nodes[anodeoffset].clustersize;
		anode->blocknr     = ablock->blk.nodes[anodeoffset].blocknr;
		anode->next        = ablock->blk.nodes[anodeoffset].next;
		anode->nr          = anodenr;
		return TRUE;
	}
	else
	{
	failed:
		anode->clustersize = anode->next = 0;
		anode->blocknr     = ~0UL;
		anode->nr          = anodenr;
		// ErrorMsg (AFS_ERROR_DNV_ALLOC_INFO, NULL);
		DBERR(ErrorTrace(5,"GetAnode","ERR: anode = 0x%lx\n",anodenr));
		return FALSE;
	}
}


/* saves and anode..
*/
static BOOL SaveAnodeInternal (struct canode *anode, ULONG anodenr, globaldata *g, BOOL latch_failure)
{
  ULONG temp;
  UWORD seqnr, anodeoffset;
  struct canodeblock *ablock;

	if (!andata.anodesperblock ||
		(!g->anodesplitmode && (UWORD)(anodenr >> 16) >= andata.anodesperblock))
		goto failed;
	if (g->anodesplitmode)
	{
		anodenr_t *split = (anodenr_t *)&anodenr;
		seqnr = split->seqnr;
		anodeoffset = split->offset;
	}
	else
	{
		temp		= divide(anodenr,andata.anodesperblock);
		seqnr       = temp;      // 1e block = 0
		anodeoffset = temp >> 16;
	}

	anode->nr   = anodenr;

	/* Save Anode */
	ablock = anodeoffset < andata.anodesperblock ? GetAnodeBlock (seqnr, g) : NULL;
	if (ablock)
	{
		if (!latch_failure)
		{
			MakeBlockDirty ((struct cachedblock *)ablock, g);
			if (ablock->changeflag == CBLK_DIRTY_PENDING)
				goto failed;
		}
		ablock->blk.nodes[anodeoffset].clustersize = anode->clustersize;
		ablock->blk.nodes[anodeoffset].blocknr     = anode->blocknr;
		ablock->blk.nodes[anodeoffset].next        = anode->next;
		if (latch_failure)
			MakeBlockDirty ((struct cachedblock *)ablock, g);
		return TRUE;
	}
	else
	{
	failed:
		DBERR(ErrorTrace(5,"SaveAnode","ERR: anode = 0x%lx\n",anodenr));
		/* Most legacy callers cannot roll back their other changes. Do not
		 * commit an incomplete transaction after such a failure. Update's
		 * own callers propagate errors and can retry instead.
		 */
		if (latch_failure && !g->uip && g->currentvolume)
		{
			g->currentvolume->metadata_error = TRUE;
			g->dirty = TRUE;
		}
		return FALSE;
	}
}

BOOL SaveAnode (struct canode *anode, ULONG anodenr, globaldata *g)
{
	return SaveAnodeInternal(anode, anodenr, g, TRUE);
}

/* The caller can safely discard or retry its own operation on failure. */
BOOL TrySaveAnode (struct canode *anode, ULONG anodenr, globaldata *g)
{
	return SaveAnodeInternal(anode, anodenr, g, FALSE);
}


/* allocates an anode and marks it as reserved
 * connect is anodenr to connect to (0 = no connection)
 */
ULONG AllocAnode (ULONG connect, globaldata *g)
{
	UWORD i;
	WORD j, k;
	struct canodeblock *ablock;
	struct anode *anodes;
	BOOL found = 0;
	ULONG seqnr = 0, field;

	if (!andata.anblkbitmap || !andata.anodesperblock)
		return 0;
	if (connect && g->anodesplitmode)
	{
		/* try to place new anode in same block */
		ablock = big_GetAnodeBlock (seqnr = connect>>16, g);
		if (ablock)
		{
			anodes = ablock->blk.nodes;
			for (k = andata.anodesperblock-1; k > -1 && !found; k--)
				found = (anodes[k].clustersize == 0 &&
						 anodes[k].blocknr == 0 &&
						 anodes[k].next == 0);
		}
	}
	else
	{
		for (i = andata.curranseqnr/32; i < andata.maxanseqnr/32 + 1; i++)
		{
			
			DBERR(if (i >= andata.anblkbitmapsize / 4)
				ErrorTrace(5, "AllocAnode","ERR: anblkbitmap out of bounds %lu >= %lu\n", i, andata.anblkbitmapsize / 4));
			
			if (i >= andata.anblkbitmapsize / sizeof(*andata.anblkbitmap))
				return 0;
			field = andata.anblkbitmap[i];
			if (field)
			{
				for (j = 31; j >= 0; j--)
				{
					if (field & (1UL << j))
					{
						seqnr = i*32 + 31-j;
						if (seqnr > MAX_ANODE_SEQNR)
							return 0;
						ablock = big_GetAnodeBlock(seqnr, g);
						if (ablock)
						{
							anodes = ablock->blk.nodes;
							for (k=0; k<andata.reserved && !found; k++)
								found = (anodes[k].clustersize == 0 &&
										 anodes[k].blocknr == 0 &&
										 anodes[k].next == 0);

							if (found)
								goto found_it;
							else
								/* mark anodeblock as full */
								andata.anblkbitmap[i] &= ~(1UL << j);
						}
						/* anodeblock does not exist */
						else goto found_it;
					}
				}
			}
		}
		
		seqnr = andata.maxanseqnr + 1;
	}

  found_it:

	if (!found)
	{
		/* give up connect mode and try again */
		if (connect)
			return AllocAnode (0, g);

		/* start over if not started from start of list;
		 * else make new block
		 */
		if (andata.curranseqnr)
		{
			andata.curranseqnr = 0;
			return AllocAnode (0, g);
		}
		else
		{
			if (seqnr > MAX_ANODE_SEQNR)
				return 0;
			if (!(ablock = big_NewAnodeBlock (seqnr, g)))
				return 0;
			anodes = ablock->blk.nodes;
			k = 0;
		}
	}
	else
	{
		if (connect)
			k++;
		else
			k--;
	}

	anodes[k].clustersize = 0;
	anodes[k].blocknr     = 0xffffffff;
	anodes[k].next        = 0;

	MakeBlockDirty((struct cachedblock *)ablock, g);
	andata.curranseqnr = seqnr;

	if(g->anodesplitmode)   
		return (ULONG)(seqnr<<16 | k);
	else
		return (ULONG)(seqnr*andata.anodesperblock + k);
}


/*
 * frees an anode for later reuse
 * universal version
 */
BOOL FreeAnode (ULONG anodenr, globaldata *g)
{
  struct canode anode = {0};
  ULONG seqnr;

	/* don't kill reserved anodes */
	if (anodenr < ANODE_USERFIRST)
	{
		anode.blocknr = (ULONG)~0L;
	}

	if (!SaveAnode (&anode, anodenr, g))
		return FALSE;
	/* the anodenr encodes the block seqnr differently depending on
	 * split mode (cf. GetAnode/SaveAnode); anodenr>>16 is only valid
	 * in split mode - in non-split mode it marked the wrong block as
	 * having free slots
	 */
	if (g->anodesplitmode)
		seqnr = anodenr >> 16;
	else
		seqnr = anodenr / andata.anodesperblock;
	if (andata.anblkbitmap &&
		seqnr/32 < andata.anblkbitmapsize / sizeof(*andata.anblkbitmap))
		andata.anblkbitmap[seqnr/32] |= 1UL << (31 - (seqnr%32));
	return TRUE;
}


/***********************************************************************/
/* LOCAL functions                                                     */
/***********************************************************************/


/* MODE_BIG has indexblocks, and negative blocknrs indicating freenode
** blocks instead of anodeblocks
*/
static struct canodeblock *big_GetAnodeBlock (UWORD seqnr, globaldata *g)
{
  ULONG blocknr;
  ULONG temp;
  struct canodeblock *ablock;
  struct cindexblock *indexblock;
  struct volumedata *volume = g->currentvolume;

	if (!andata.indexperblock)
		return NULL;
	temp = divide (seqnr, andata.indexperblock);

	/* not in cache, put it in */
	/* get the indexblock */
	if (!(indexblock = GetIndexBlock (temp /*& 0xffff*/, g)))
	{
		DBERR(ErrorTrace(5, "GetAnodeBlock","ERR: index not found. %lu %lu %08lx\n", seqnr, andata.indexperblock, temp));
		return NULL;
	}

	/* get blocknr */
	if (!(blocknr = indexblock->blk.index[temp >> 16]))
	{
		DBERR(ErrorTrace(5,"GetAnodeBlock","ERR: index zero %lu %lu %08lx\n", seqnr, andata.indexperblock, temp));
		return NULL;
	}

	/* check cache */
	ablock = (struct canodeblock *)CheckCache (volume->anblks, HASHM_ANODE, blocknr, g);
	if (ablock)
		return ablock;

	if (!(ablock = (struct canodeblock *)AllocLRU(g)))
	{
		DBERR(ErrorTrace(5,"GetAnodeBlock","ERR: alloclru failed\n"));
		return NULL;
	}

	DBERR(ErrorTrace(10,"GetAnodeBlock", "seqnr = %lu blocknr = %lu\n", seqnr, blocknr));

	/* read it */
	if (RawRead ((UBYTE*)&ablock->blk, RESCLUSTER, blocknr, g) != 0)
	{
		DB(Trace(5,"GetAnodeBlock","Read ERR: seqnr = %lu blocknr = %lx\n", seqnr, blocknr));
		FreeLRU ((struct cachedblock *)ablock);
		return NULL;
	}

	/* check it */
	if (ablock->blk.id != ABLKID || ablock->blk.seqnr != seqnr)
	{
		ULONG args[2];
		args[0] = ablock->blk.id;
		args[1] = blocknr;
		FreeLRU ((struct cachedblock *)ablock);
		ErrorMsg (AFS_ERROR_DNV_WRONG_ANID, args, g);
		return NULL;
	}

	/* initialize it */
	ablock->volume     = volume;
	ablock->blocknr    = blocknr;
	ablock->used       = FALSE;
	ablock->changeflag = FALSE;
	Hash (ablock, volume->anblks, HASHM_ANODE);

	return ablock;
}


/* MODE_BIG has difference between anodeblocks and fnodeblocks
*/
static struct canodeblock *big_NewAnodeBlock (UWORD seqnr, globaldata *g)
{
  struct canodeblock *blok;
  struct volumedata *volume = g->currentvolume;
  struct cindexblock *indexblock;
  ULONG indexblnr, oldmaxseqnr;
  LONG blocknr;
  UWORD indexoffset, oldlock;

	/* get indexblock */
	if (!andata.indexperblock)
		return NULL;
	indexblnr = seqnr/andata.indexperblock;
	indexoffset = seqnr%andata.indexperblock;
	oldmaxseqnr = andata.maxanseqnr;
	if (!ReallocAnodeBitmap(seqnr, g))
		return NULL;
	if (!(indexblock = GetIndexBlock(indexblnr, g))) {
		if (!(indexblock = NewIndexBlock(indexblnr, g))) {
			DBERR(ErrorTrace(10,"big_NewAnodeBlock","ERR: NewIndexBlock %lu %lu %lu %lu\n", seqnr, indexblnr, indexoffset, andata.indexperblock));
			andata.maxanseqnr = oldmaxseqnr;
			return NULL;
		}
	}
	if (indexblock->blk.index[indexoffset]) {
		andata.maxanseqnr = oldmaxseqnr;
		return NULL;
	}

	oldlock = indexblock->used;
	LOCK(indexblock);
	if (!(blok = (struct canodeblock *)AllocLRU(g)) || !(blocknr = AllocReservedBlock(g)) ) {
		DBERR(ErrorTrace(10,"big_NewAnodeBlock","ERR: AllocLRU/AllocReservedBlock %lu %lu %lu\n", seqnr, indexblnr, indexoffset));
		if (blok)
			FreeLRU((struct cachedblock *)blok);
		andata.maxanseqnr = oldmaxseqnr;
		indexblock->used = oldlock;         // unlock block
		return NULL;
	}

	DBERR(ErrorTrace(10,"big_NewAnodeBlock", "seqnr = %lu block = %lu\n", seqnr, blocknr));

	indexblock->blk.index[indexoffset] = blocknr;

	blok->volume     = volume;
	blok->blocknr    = blocknr;
	blok->used       = FALSE;
	blok->blk.id     = ABLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = TRUE;
	Hash(blok, volume->anblks, HASHM_ANODE);
	MakeBlockDirty((struct cachedblock *)indexblock, g);
	indexblock->used = oldlock;         // unlock block

	return blok;
}



/**********************************************************************/
/* indexblocks                                                        */
/**********************************************************************/

/*
 * get indexblock nr
 * returns NULL if failure
 */
struct cindexblock *GetIndexBlock (UWORD nr, globaldata *g)
{
  ULONG blocknr, temp;
  struct cindexblock *indexblk;
  struct cindexblock *superblk;
  struct volumedata *volume = g->currentvolume;

	/* check cache (can be empty) */
	for (indexblk = HeadOf(&volume->indexblks); indexblk->next; indexblk=indexblk->next)    
	{
		if (indexblk->blk.seqnr == nr)
		{
			MakeLRU (indexblk);
			return indexblk;
		}
	}

	/* not in cache, put it in
	 * first, get blocknr
	 */
	if (g->supermode) 
	{
		/* temp is chopped by auto cast */
		if (!andata.indexperblock)
			return NULL;
		temp = divide(nr, andata.indexperblock);
		if (!(superblk = GetSuperBlock (temp, g)))
		{
			DBERR(ErrorTrace(5, "GetIndexBlock", "ERR: superblock not found. %lu %lu %08lx\n", nr, andata.indexperblock, temp));
			return NULL;
		}

		if (!(blocknr = superblk->blk.index[temp>>16]))
		{
			DBERR(ErrorTrace(5, "GetIndexBlock", "ERR: super zero. %lu %lu %08lx\n", nr, andata.indexperblock, temp));
			return NULL;
		}
	}
	else
	{
		if ((nr>MAXSMALLINDEXNR) || !(blocknr = volume->rootblk->idx.small.indexblocks[nr])) 
			return NULL;
	}

	/* allocate space from cache */
	if (!(indexblk = (struct cindexblock *)AllocLRU(g))) {
		DBERR(ErrorTrace(5, "GetIndexBlock", "ERR: AllocLRU. %lu %lu %08lx %lu\n", nr, andata.indexperblock, temp, blocknr));
		return NULL;
	}

	DBERR(ErrorTrace(10,"GetIndexBlock","seqnr = %lu blocknr = %lu\n", nr, blocknr));

	if (RawRead ((UBYTE*)&indexblk->blk, RESCLUSTER, blocknr, g) != 0) {
		FreeLRU ((struct cachedblock *)indexblk);
		return NULL;
	}
		
	if (indexblk->blk.id == IBLKID && indexblk->blk.seqnr == nr)
	{
		indexblk->volume     = volume;
		indexblk->blocknr    = blocknr;
		indexblk->used       = FALSE;
		indexblk->changeflag = FALSE;
		MinAddHead (&volume->indexblks, indexblk);
	}
	else
	{
		ULONG args[5];
		args[0] = indexblk->blk.id;
		args[1] = IBLKID;
		args[2] = blocknr;
		args[3] = nr;
		args[4] = andata.indexperblock;
		FreeLRU ((struct cachedblock *)indexblk);
		ErrorMsg (AFS_ERROR_DNV_WRONG_INDID, args, g);
		return NULL;
	}

	return indexblk;
}
		

static struct cindexblock *NewIndexBlock (UWORD seqnr, globaldata *g)
{
  struct cindexblock *blok;
  struct cindexblock *superblok = NULL;
  struct volumedata *volume = g->currentvolume;
  ULONG superblnr = 0;
  LONG blocknr;
  UWORD superoffset = 0;
  UWORD oldlock = 0;

	if (g->supermode)
	{
		if (!andata.indexperblock || seqnr / andata.indexperblock > MAXSUPER)
			return NULL;
		superblnr = seqnr/andata.indexperblock;
		superoffset = seqnr%andata.indexperblock;
		if (!(superblok = GetSuperBlock (superblnr, g)))
			if (!(superblok = NewSuperBlock (superblnr, g))) {
				DBERR(ErrorTrace(1, "NewIndexBlock", "ERR: Super not found. %lu %lu %lu %lu\n", seqnr, andata.indexperblock, superblnr, superoffset));
				return NULL;
			} else {
				DBERR(ErrorTrace(1, "NewIndexBlock", "OK. %lu %lu %lu %lu\n", seqnr, andata.indexperblock, superblnr, superoffset));		
			}
		if (superblok->blk.index[superoffset])
			return NULL;

		oldlock = superblok->used;
		LOCK(superblok);
	}
	else if (seqnr > MAXSMALLINDEXNR ||
		volume->rootblk->idx.small.indexblocks[seqnr]) {
		return NULL;
	}

	if (!(blok = (struct cindexblock *)AllocLRU(g)) ||
		!(blocknr = AllocReservedBlock(g)) )
	{
		DBERR(ErrorTrace(1, "NewIndexBlock", "ERR: AllocLRU/AllocReservedBlock. %lu %lu %lu %lu\n", seqnr, blocknr, superblnr, superoffset));
		if (blok)
			FreeLRU((struct cachedblock *)blok);
		if (superblok)
			superblok->used = oldlock;
		return NULL;
	}

	DBERR(ErrorTrace(10,"NewIndexBlock", "seqnr = %lu block = %lu\n", seqnr, blocknr));

	if (g->supermode) {
		superblok->blk.index[superoffset] = blocknr;
		MakeBlockDirty((struct cachedblock *)superblok, g);
		superblok->used = oldlock;
	} else {
		volume->rootblk->idx.small.indexblocks[seqnr] = blocknr;
		volume->rootblockchangeflag = TRUE;
	}

	blok->volume     = volume;
	blok->blocknr    = blocknr;
	blok->used       = FALSE;
	blok->blk.id     = IBLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = TRUE;
	MinAddHead(&volume->indexblks, blok);

	return blok;
}

struct cindexblock *GetSuperBlock (UWORD nr, globaldata *g)
{
  ULONG blocknr;
  struct cindexblock *superblk;
  struct volumedata *volume = g->currentvolume;

	DBERR(blocknr = 0xffdddddd);

	/* check supermode */
	if (!g->supermode) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: Illegally entered\n"));
		return NULL;
	}

	/* check cache (can be empty) */
	for (superblk = HeadOf(&volume->superblks); superblk->next; superblk=superblk->next)    
	{
		if (superblk->blk.seqnr == nr)
		{
			MakeLRU (superblk);
			return superblk;
		}
	}

	/* not in cache, put it in
	 * first, get blocknr
	 */
	if (!volume->rblkextension || (nr>MAXSUPER) ||
		!(blocknr = volume->rblkextension->blk.superindex[nr])) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: out of bounds. %lu %lu\n", nr, blocknr));
		return NULL;
	}

	/* allocate space from cache */
	if (!(superblk = (struct cindexblock *)AllocLRU(g))) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: AllocLRU error. %lu %lu\n", nr, blocknr));
		return NULL;
	}

	DBERR(ErrorTrace(10,"GetSuperBlock","seqnr = %lu blocknr = %lu\n", nr, blocknr));

	if (RawRead ((UBYTE*)&superblk->blk, RESCLUSTER, blocknr, g) != 0) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: read error. %lu %lu\n", nr, blocknr));
		FreeLRU ((struct cachedblock *)superblk);
		return NULL;
	}
		
	if (superblk->blk.id == SBLKID && superblk->blk.seqnr == nr)
	{
		superblk->volume     = volume;
		superblk->blocknr    = blocknr;
		superblk->used       = FALSE;
		superblk->changeflag = FALSE;
		MinAddHead (&volume->superblks, superblk);
	}
	else
	{
		ULONG args[5];
		args[0] = superblk->blk.id;
		args[1] = SBLKID;
		args[2] = blocknr;
		args[3] = nr;
		args[4] = 0;
		FreeLRU ((struct cachedblock *)superblk);
		ErrorMsg (AFS_ERROR_DNV_WRONG_INDID, args, g);
		return NULL;
	}

	return superblk;
}

static struct cindexblock *NewSuperBlock (UWORD seqnr, globaldata *g)
{
  struct cindexblock *blok;
  struct volumedata *volume = g->currentvolume;

	DBERR(blok = NULL;)

	if (!volume->rblkextension || (seqnr > MAXSUPER) ||
		!(blok = (struct cindexblock *)AllocLRU(g)) ) {
		DBERR(ErrorTrace(1, "NewSuperBlock", "ERR: out of bounds or LRU error. %lu %p\n", seqnr, blok));
		return NULL;
	}

	if (volume->rblkextension->blk.superindex[seqnr] ||
		!(volume->rblkextension->blk.superindex[seqnr] = AllocReservedBlock(g)))
	{
		DBERR(ErrorTrace(1, "NewSuperBlock", "ERR: AllocReservedBlock. %lu %p\n", seqnr, blok));
		FreeLRU((struct cachedblock *)blok);
		return NULL;
	}
 
	DBERR(ErrorTrace(10,"NewSuperBlock", "seqnr = %lu block = %lu\n", seqnr, volume->rblkextension->blk.superindex[seqnr]));

	/* MakeBlockDirty, not a direct changeflag set: the extension must be
	 * reallocated (copy-on-write) before its committed copy may be
	 * rewritten by UpdateDisk.
	 */
	MakeBlockDirty ((struct cachedblock *)volume->rblkextension, g);

	blok->volume     = volume;
	blok->blocknr    = volume->rblkextension->blk.superindex[seqnr];
	blok->used       = FALSE;
	blok->blk.id     = SBLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = TRUE;
	MinAddHead(&volume->superblks, blok);

	return blok;
}

/* Remove anode from anodechain
 * If previous==0, anode->next becomes head.
 * Otherwise previous->next becomes anode->next.
 * Anode is freed.
 *
 * Arguments:
 * anode = anode to be removed
 * previous = previous in chain; or 0 if anode is head
 * head = anodenr of head of list
 */
BOOL RemoveFromAnodeChain (const struct canode *anode, ULONG previous, ULONG head, globaldata *g)
{
  struct canode sparenode;

	if(previous)
	{
		if (!GetAnode(&sparenode, previous, g))
			return FALSE;
		sparenode.next = anode->next;
		if (!SaveAnode(&sparenode, sparenode.nr, g))
			return FALSE;
		if (!FreeAnode(anode->nr, g))
			goto free_failed;
	}
	else
	{
		/* anode is head of list (check both tails here) */
		if (anode->next)
		{
			/* There is a next entry -> becomes head */
			if (!GetAnode(&sparenode, anode->next, g) ||
				!SaveAnode(&sparenode, head, g))
				return FALSE;
			if (!FreeAnode(anode->next, g))
				goto free_failed;
		}
		else
		{
			/* No anode->next: Free list. */
			if (!FreeAnode(head, g))
				return FALSE;
		}
	}
	return TRUE;

free_failed:
	/* The predecessor may already have been changed; this cannot be
	 * retried as an ordinary unlink without reconstructing the chain.
	 */
	g->currentvolume->metadata_error = TRUE;
	g->dirty = TRUE;
	return FALSE;
}


/*********************************************************************/
/* Initialization                                                    */
/*********************************************************************/

void InitAnodes (struct volumedata *volume, BOOL formatting, globaldata *g)
{
	if(!g->harddiskmode)
	{
		ErrorMsg (AFS_ERROR_ANODE_INIT, NULL, g);
	}
	else
	{
		g->getanodeblock = big_GetAnodeBlock;

		andata.curranseqnr = volume->rblkextension ? volume->rblkextension->blk.curranseqnr : 0;
		andata.anodesperblock = (volume->rootblk->reserved_blksize - sizeof(anodeblock_t)) / sizeof(anode_t);
		andata.indexperblock = (volume->rootblk->reserved_blksize - sizeof(indexblock_t)) / sizeof(LONG);
		/* An anode block sequence number is 16 bits wide (anodenr_t.seqnr,
		 * UWORD GetAnodeBlock() arguments, 16-bit DIVU quotient), so the
		 * maximum can never exceed MAX_ANODE_SEQNR. This is a block count,
		 * not an anode count, hence no multiplication by anodesperblock.
		 */
		andata.maxanodeseqnr = g->supermode ?
				MAX_ANODE_SEQNR :
				((MAXSMALLINDEXNR + 1) * andata.indexperblock - 1);
		if (andata.maxanodeseqnr > MAX_ANODE_SEQNR)
			andata.maxanodeseqnr = MAX_ANODE_SEQNR;
		andata.reserved = andata.anodesperblock - RESERVEDANODES;
		MakeAnodeBitmap (formatting, g);
	}
}


/* Find out how large the anblkbitmap must be, allocate it and
 * initialise it. Free any preexisting anblkbitmap
 *
 * The anode bitmap is used for allocating anodes. It has the
 * following properties:
 * - It is  maintained in memory only (not on disk). 
 * - Intialization is lazy: all anodes are marked as available
 * - When allocation anodes (see AllocAnode), this bitmap is used
 *   to find available anodes. It then checks with the actual
 *   anode (which should be 0,0,0 if available). If it isn't really
 *   available, the anodebitmap is updated, otherwise the anode is
 *   taken.
 */
/* Position of the last nonzero entry of index[0..n-1], -1 if there is none */
static int LastIndex(LONG *index, UWORD n)
{
	LONG *p = index + n;

	while (p != index)
		if (*--p)
			return p - index;
	return -1;
}

static void MakeAnodeBitmap (BOOL formatting, globaldata *g)
{
	struct cindexblock *iblk;
	struct cindexblock *sblk;
	int i, j, s;
	ULONG size;

	if (andata.anblkbitmap)
		FreeMemP (andata.anblkbitmap, g);
	andata.anblkbitmap = NULL;
	andata.anblkbitmapsize = 0;

	/* count number of anodeblocks and allocate bitmap */
	if (formatting)
	{
		i = 0; s = 0; j = 1;
	}
	else
	{
		if (g->supermode)
		{
			if (!g->currentvolume->rblkextension)
				goto error;
			for (s=MAXSUPER; s >= 0 && !g->currentvolume->rblkextension->blk.superindex[s]; s--);
			if (s < 0)
				goto error;
				
			sblk = GetSuperBlock (s, g);

			DBERR(if (!sblk) ErrorTrace(1, "MakeAnodeBitmap", "ERR: GetSuperBlock returned NULL!. %ld\n", s));
			if (!sblk)
				goto error;

			i = LastIndex(sblk->blk.index, andata.indexperblock);
		}
		else
		{
			for (s=0, i=MAXSMALLINDEXNR; i >= 0 && !g->rootblock->idx.small.indexblocks[i]; i--);
		}

		if (i < 0)
			goto error;
		iblk = GetIndexBlock (s * andata.indexperblock + i, g);

		DBERR(if (!iblk) ErrorTrace(1, "MakeAnodeBitmap", "ERR: GetIndexBlock returned NULL!. %ld %ld\n", s, i));
		if (!iblk)
			goto error;

		j = LastIndex(iblk->blk.index, andata.indexperblock);
		if (j < 0)
			goto error;
	}

	if (g->supermode)
	{
		andata.maxanseqnr = s * andata.indexperblock * andata.indexperblock + i * andata.indexperblock + j;
		size = ((s * andata.indexperblock + i + 1) * andata.indexperblock + 7) / 8;
	}
	else
	{
		andata.maxanseqnr = i * andata.indexperblock + j;
		size = ((i+1) * andata.indexperblock + 7) / 8;
	}
	if (andata.maxanseqnr > andata.maxanodeseqnr)
		goto error;

	andata.anblkbitmapsize = (size + 3) & ~3;
	andata.anblkbitmap = AllocMemP (andata.anblkbitmapsize, g);
	if (!andata.anblkbitmap)
	{
		andata.anblkbitmapsize = 0;
		goto error;
	}

	for (i = 0; i < andata.anblkbitmapsize/4; i++)
		andata.anblkbitmap[i] = 0xffffffff;         /* all available */

	return;

 error:
	g->currentvolume->metadata_error = TRUE;
	ErrorMsg(AFS_ERROR_ANODE_ERROR, NULL, g);
}


/* test if new anodeseqnr causes change in anblkbitmap */
static BOOL ReallocAnodeBitmap (ULONG newseqnr, globaldata *g)
{
	ULONG *newbitmap, newsize;
	int t;

	if (newseqnr > andata.maxanseqnr)
	{
		newsize = ((newseqnr/andata.indexperblock + 1) * andata.indexperblock + 7) / 8;
		if (newsize > andata.anblkbitmapsize)
		{
			newsize = (newsize + 3) & ~3;   /* longwords */
			newbitmap = AllocMemP (newsize, g);
			if (!newbitmap)
				return FALSE;
			for (t=0; t < newsize/4; t++)
				newbitmap[t] = 0xffffffff;
			memcpy (newbitmap, andata.anblkbitmap, andata.anblkbitmapsize);
			FreeMemP (andata.anblkbitmap, g);
			andata.anblkbitmap = newbitmap;
			andata.anblkbitmapsize = newsize;
		}
		andata.maxanseqnr = newseqnr;
	}
	return TRUE;
}
