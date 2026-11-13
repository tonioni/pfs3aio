/* LIVE @src update.c @func MakeBlockDirty @func RetryPendingLists @func RetryPendingBlocks
 *
 * A cached block whose relocation fails (no free reserved block, or the parent
 * cannot be updated) is left CBLK_DIRTY_PENDING and retried at the next update.
 * The volume remembers that such a block exists, so an update with nothing
 * pending does not scan every cache list:
 *  - nothing pending: RetryPendingBlocks returns TRUE without reading a list;
 *  - an allocation failure or a failed parent update marks the volume;
 *  - a retry that relocates every pending block clears the mark;
 *  - a retry that fails again keeps it and reports the failure;
 *  - the rootblock extension is retried the same way.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int16_t BOOL;
#define TRUE 1
#define FALSE 0
#define VERSION23 1
#define DELDIR 1
#define HASHM_DIR 3
#define HASHM_ANODE 3
#define CBLK_DIRTY_PENDING 2

struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
#define HeadOf(list) ((void *)((list)->mlh_Head))

struct volumedata;
struct cachedblock {
	struct cachedblock *next, *prev;
	struct volumedata *volume;
	ULONG blocknr, oldblocknr;
	UWORD used;
	UBYTE changeflag;
};
struct crootblockextension {
	struct crootblockextension *next, *prev;
	struct volumedata *volume;
	ULONG blocknr, oldblocknr;
	UWORD used;
	UBYTE changeflag;
};
struct volumedata {
	struct crootblockextension *rblkextension;
	struct MinList anblks[HASHM_ANODE + 1];
	struct MinList dirblks[HASHM_DIR + 1];
	struct MinList indexblks, bmblks, superblks, deldirblks, bmindexblks;
	BOOL pendingblocks;
};
typedef struct globaldata { BOOL dirty; UWORD locknr; } globaldata;
#define LOCK(blk) ((blk)->used = g->locknr)

/* ---- mocks ---- */
static int allocs, alloc_ok, update_ok, frees;
static ULONG nextblock = 1000;
static ULONG AllocReservedBlock(globaldata *g) { (void)g; allocs++; return alloc_ok ? nextblock++ : 0; }
static void FreeReservedBlock(ULONG blocknr, globaldata *g) { (void)blocknr; (void)g; frees++; }
/* as UpdateDBLK and the others: the parent now points at the new block */
static BOOL UpdateBlocknr(struct cachedblock *blk, ULONG blocknr, globaldata *g)
{
	(void)blocknr; (void)g;
	if (update_ok)
		blk->changeflag = TRUE;
	return update_ok;
}

BOOL MakeBlockDirty(struct cachedblock *blk, globaldata *g);
#include "gen/MakeBlockDirty.c"		/* the REAL functions from ../../update.c */
#include "gen/RetryPendingLists.c"
#include "gen/RetryPendingBlocks.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void newlist(struct MinList *l)
{
	l->mlh_Head = (struct MinNode *)&l->mlh_Tail;
	l->mlh_Tail = NULL;
	l->mlh_TailPred = (struct MinNode *)&l->mlh_Head;
}

static void addhead(struct MinList *l, struct cachedblock *b)
{
	struct MinNode *n = (struct MinNode *)b;
	n->mln_Succ = l->mlh_Head;
	n->mln_Pred = (struct MinNode *)&l->mlh_Head;
	l->mlh_Head->mln_Pred = n;
	l->mlh_Head = n;
}

static struct volumedata vol;
static struct crootblockextension rext;
static globaldata gd;

static void reset(void)
{
	struct MinList *l;
	for (l = vol.anblks; l <= &vol.bmindexblks; l++)
		newlist(l);
	vol.pendingblocks = FALSE;
	vol.rblkextension = &rext;
	rext.volume = &vol; rext.changeflag = TRUE;
	gd.dirty = FALSE; gd.locknr = 5;
	allocs = frees = 0; alloc_ok = 1; update_ok = 1;
}

static void block(struct cachedblock *b, struct MinList *l, ULONG nr)
{
	b->volume = &vol; b->blocknr = nr; b->oldblocknr = 0; b->used = 0; b->changeflag = FALSE;
	addhead(l, b);
}

int main(void)
{
	struct cachedblock a, d;

	/* nothing pending: no list is read, even one holding a pending block */
	reset();
	block(&d, &vol.dirblks[2], 10);
	d.changeflag = CBLK_DIRTY_PENDING;
	CHECK(RetryPendingBlocks(&vol, &gd) && allocs == 0 && d.changeflag == CBLK_DIRTY_PENDING,
		"nothing marked: lists skipped (allocs %d)", allocs);

	/* allocation fails: pending, volume marked */
	reset();
	block(&d, &vol.dirblks[1], 10);
	alloc_ok = 0;
	MakeBlockDirty(&d, &gd);
	CHECK(d.changeflag == CBLK_DIRTY_PENDING && vol.pendingblocks && gd.dirty && d.blocknr == 10,
		"alloc failure: pending and marked");

	/* the retry relocates it and clears the mark */
	alloc_ok = 1;
	CHECK(RetryPendingBlocks(&vol, &gd) && d.changeflag == TRUE && d.blocknr == 1000 && d.oldblocknr == 10
		&& !vol.pendingblocks, "retry: relocated, mark cleared");

	/* a retry that fails again keeps the mark */
	reset();
	block(&a, &vol.anblks[0], 20);
	alloc_ok = 0;
	MakeBlockDirty(&a, &gd);
	CHECK(!RetryPendingBlocks(&vol, &gd) && vol.pendingblocks && a.changeflag == CBLK_DIRTY_PENDING,
		"retry fails: reported, mark kept");
	alloc_ok = 1;
	CHECK(RetryPendingBlocks(&vol, &gd) && !vol.pendingblocks && a.changeflag == TRUE,
		"later retry: relocated, mark cleared");

	/* the parent update fails: the new block is freed, pending, marked */
	reset();
	block(&d, &vol.dirblks[0], 30);
	update_ok = 0;
	MakeBlockDirty(&d, &gd);
	CHECK(d.changeflag == CBLK_DIRTY_PENDING && d.blocknr == 30 && frees == 1 && vol.pendingblocks,
		"parent update failure: pending and marked");

	/* the rootblock extension is retried too */
	reset();
	rext.changeflag = FALSE;
	alloc_ok = 0;
	MakeBlockDirty((struct cachedblock *)&rext, &gd);
	CHECK(rext.changeflag == CBLK_DIRTY_PENDING && vol.pendingblocks, "extension: pending and marked");
	alloc_ok = 1;
	CHECK(RetryPendingBlocks(&vol, &gd) && rext.changeflag == TRUE && !vol.pendingblocks,
		"extension: relocated, mark cleared");

	if (failures) { printf("pending_retry_live: %d FAILED\n", failures); return 1; }
	printf("  PASS: pending blocks are retried, and only scanned for when one exists\n");
	return 0;
}
