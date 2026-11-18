/* LIVE @src directory.c @func SetDeldir
 *
 * Lifts the checkout's real SetDeldir, the handler side of setdeldir. Its
 * UpdateDisk commits pending changes first and is FALSE both when a commit
 * fails and when there is nothing to commit.
 *  - The size of a clean volume changes although UpdateDisk is FALSE.
 *  - The size of a dirty volume changes after the commit.
 *  - A failed commit gives PFS_ERROR_INCONSISTENT and the size stays.
 *  - A size out of range (-1, 33) gives ERROR_BAD_NUMBER.
 *  - A lock on the deldir or on a deleted file gives ERROR_OBJECT_IN_USE.
 *  - A smaller size frees the deleted files of the dropped blocks and puts
 *    the dropped blocks on the reserved free list.
 *  - Without room for a new block it gives ERROR_DISK_FULL and keeps the
 *    blocks added so far.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "live.h"

#define PFS_ERROR_INCONSISTENT 213	/* ERROR_DISK_NOT_VALIDATED, as in struct.h */
#define MAXDELDIR_BLOCKS 32
#define DELENTRIES_PER_BLOCK 31

struct minnode { struct minnode *mln_Succ, *mln_Pred; };
struct minlist { void *mlh_Head; };
#define HeadOf(list) ((void *)((list)->mlh_Head))

union objectinfo { struct { int special; } deldir; };
#define SPECIAL_DELDIR 1
#define SPECIAL_DELFILE 2
#define IsDelDir(oi) ((oi).deldir.special==SPECIAL_DELDIR)
#define IsDelFile(oi) ((oi).deldir.special==SPECIAL_DELFILE)
typedef struct listentry { struct listentry *next; union objectinfo info; } listentry_t;

struct deldirentry { ULONG anodenr; };
struct cdeldirblock {
	struct cdeldirblock *next;
	struct minnode lru;
	struct { struct deldirentry entries[DELENTRIES_PER_BLOCK]; } blk;
};
#define LRU_CHAIN(b) (&(b)->lru)
struct crootblockextension {
	struct { UWORD deldirsize; ULONG deldirroving; ULONG deldir[MAXDELDIR_BLOCKS]; } blk;
};
struct volumedata {
	struct crootblockextension *rblkextension;
	struct minlist fileentries, deldirblks;
};
typedef struct globaldata {
	struct volumedata *currentvolume;
	BOOL dirty, deldirenabled;
	struct { struct minlist LRUpool; } glob_lrudata;
} globaldata;

struct cachedblock;
static struct crootblockextension rext;
static struct volumedata vol;
static globaldata gd;
static listentry_t locks[2], fe_tail;
static struct cdeldirblock dd_tail, ddblks[MAXDELDIR_BLOCKS];
static int update_fails, updates, freed_chains, restobefreed, room;

/* FALSE when nothing is dirty, as the handler's; a failed commit keeps dirty */
static BOOL UpdateDisk(globaldata *g)
{
	updates++;
	if (!g->dirty || update_fails)
		return FALSE;
	g->dirty = FALSE;
	return TRUE;
}
static struct cdeldirblock *GetDeldirBlock(UWORD nr, globaldata *g) { (void)g; return &ddblks[nr]; }
static void FreeAnodesInChain(ULONG anodenr, globaldata *g) { (void)anodenr; (void)g; freed_chains++; }
static void FlushBlock(void *b, globaldata *g) { (void)b; (void)g; }
static void MinRemove(struct minnode *n) { (void)n; }
static void MinAddHead(struct minlist *l, struct minnode *n) { (void)l; (void)n; }
static void ResToBeFreed(ULONG blocknr, globaldata *g) { (void)blocknr; (void)g; restobefreed++; }
static void MakeBlockDirty(void *b, globaldata *g) { (void)b; g->dirty = TRUE; }
static BOOL NewDeldirBlock(UWORD seqnr, globaldata *g)
{
	(void)g;
	if (!room)
		return FALSE;
	room--;
	rext.blk.deldir[seqnr] = 100 + seqnr;
	return TRUE;
}

#include "gen/SetDeldir.c"


static void setup(int size, int dirty)
{
	int i;
	memset(&rext, 0, sizeof(rext));
	memset(ddblks, 0, sizeof(ddblks));
	rext.blk.deldirsize = size;
	for (i = 0; i < size; i++)
	{
		rext.blk.deldir[i] = 100 + i;
		ddblks[i].blk.entries[0].anodenr = 500 + i;
	}
	vol.rblkextension = &rext;
	fe_tail.next = NULL;
	vol.fileentries.mlh_Head = &fe_tail;
	dd_tail.next = NULL;
	vol.deldirblks.mlh_Head = &dd_tail;
	gd.currentvolume = &vol;
	gd.dirty = dirty;
	gd.deldirenabled = size > 0;
	update_fails = updates = freed_chains = restobefreed = 0;
	room = MAXDELDIR_BLOCKS;
}

int main(void)
{
	ULONG err;

	setup(2, 0);
	err = SetDeldir(8, &gd);
	CHECK(err == 0, "clean volume: no error");
	CHECK(rext.blk.deldirsize == 8 && gd.deldirenabled, "clean volume: 8 blocks");

	setup(2, 1);
	err = SetDeldir(8, &gd);
	CHECK(err == 0 && rext.blk.deldirsize == 8, "dirty volume: committed, 8 blocks");

	setup(2, 1);
	update_fails = 1;
	err = SetDeldir(8, &gd);
	CHECK(err == PFS_ERROR_INCONSISTENT, "failed commit: PFS_ERROR_INCONSISTENT");
	CHECK(rext.blk.deldirsize == 2, "failed commit: size stays");

	setup(2, 0);
	CHECK(SetDeldir(-1, &gd) == ERROR_BAD_NUMBER, "-1: ERROR_BAD_NUMBER");
	CHECK(SetDeldir(33, &gd) == ERROR_BAD_NUMBER, "33: ERROR_BAD_NUMBER");
	CHECK(updates == 0 && rext.blk.deldirsize == 2, "out of range: nothing done");

	setup(2, 0);
	locks[0].info.deldir.special = 0;
	locks[0].next = &locks[1];
	locks[1].info.deldir.special = SPECIAL_DELDIR;
	locks[1].next = &fe_tail;
	vol.fileentries.mlh_Head = &locks[0];
	CHECK(SetDeldir(4, &gd) == ERROR_OBJECT_IN_USE, "lock on the deldir: ERROR_OBJECT_IN_USE");
	locks[1].info.deldir.special = SPECIAL_DELFILE;
	CHECK(SetDeldir(4, &gd) == ERROR_OBJECT_IN_USE, "lock on a deleted file: ERROR_OBJECT_IN_USE");
	CHECK(rext.blk.deldirsize == 2, "in use: size stays");

	setup(8, 0);
	err = SetDeldir(2, &gd);
	CHECK(err == 0 && rext.blk.deldirsize == 2, "8 -> 2: 2 blocks");
	CHECK(freed_chains == 6, "8 -> 2: the files of the 6 dropped blocks freed");
	CHECK(restobefreed == 6 && !rext.blk.deldir[2] && !rext.blk.deldir[7], "8 -> 2: dropped blocks to be freed");

	setup(0, 0);
	err = SetDeldir(0, &gd);
	CHECK(err == 0 && rext.blk.deldirsize == 0 && !gd.deldirenabled, "0: off");

	setup(2, 0);
	room = 3;
	err = SetDeldir(8, &gd);
	CHECK(err == ERROR_DISK_FULL, "no room: ERROR_DISK_FULL");
	CHECK(rext.blk.deldirsize == 5, "no room: the 3 blocks added stay");

	if (fails) { printf("setdeldir_live: %d FAILED\n", fails); return 1; }
	printf("  PASS: SetDeldir changes the size of a clean and a dirty volume, refuses a failed commit, a bad size and a deldir in use\n");
	return 0;
}
