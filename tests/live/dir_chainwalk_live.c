/* LIVE @src directory.c @func IsDelfileValid @func MoveToPrevious @src anodes.c @func GetNextAnode
 *
 * Lifts the checkout's real IsDelfileValid (checks the anode chain of a
 * deleted file) and MoveToPrevious (searches the block in front of an entry's
 * block). Both follow an anode chain with GetNextAnode. GetAnode is a mock
 * over a table that gives up (runaway) after 100000 reads.
 *
 * Proven:
 *  - IsDelfileValid: a valid chain with no block taken is valid; a taken
 *    block frees the chain and clears the entry; a chain that loops back
 *    (also onto its first anode or itself) ends with PFS_ERROR_INCONSISTENT
 *    after a few reads
 *  - MoveToPrevious: the block in front of the entry's block is found in a
 *    valid chain; a chain that loops back without reaching the entry's block
 *    ends with FALSE after a few reads
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "live.h"
#include "pfs3disk.h"

#define PFS_ERROR_INCONSISTENT 213
#define AFS_ERROR_CACHE_INCONSISTENCY 1

struct cachedblock;
struct cdirblock { ULONG blocknr; ULONG used; struct { ULONG anodenr, parent; UBYTE entries[64]; } blk; };
struct cdeldirblock { ULONG blocknr; };
struct canode { ULONG clustersize, blocknr, next, nr; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct anodewalk { ULONG mark, steps; };
typedef struct globaldata { ULONG locknr; } globaldata;

#define FIRSTENTRY(blok) ((struct direntry*)((blok)->blk.entries))
#define LOCK(blk) ((blk)->used = g->locknr)

#define NTAB 64
#define CAP 100000
static ULONG nextof[NTAB];
static int used[NTAB], taken[NTAB];
static int reads, runaway, freed_chain, dirty, loaded, fitted;
static ULONG loaded_blocknr;
static struct cdirblock prevblk;

static BOOL GetAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	if (++reads > CAP) runaway = 1;
	if (runaway || nr == 0 || nr >= NTAB || !used[nr]) {
		a->clustersize = a->next = 0; a->blocknr = ~0UL; a->nr = nr;
		return FALSE;
	}
	a->clustersize = 1; a->blocknr = 1000 + nr; a->next = nextof[nr]; a->nr = nr;
	return TRUE;
}

/* IsDelfileValid */
static LONG BlockTaken(struct canode *a, globaldata *g) { (void)g; return taken[a->nr]; }
static BOOL FreeAnodesInChain(ULONG nr, globaldata *g) { (void)nr; (void)g; freed_chain++; return TRUE; }
static void MakeBlockDirty(struct cachedblock *b, globaldata *g) { (void)b; (void)g; dirty++; }

/* MoveToPrevious: the block in front is loaded, the entry fits in it */
static struct cdirblock *LoadDirBlock(ULONG blocknr, globaldata *g)
{
	(void)g;
	loaded++;
	loaded_blocknr = blocknr;
	prevblk.blocknr = blocknr;
	return &prevblk;
}
static struct direntry *CheckFit(struct cdirblock *b, int needed, globaldata *g) { (void)needed; (void)g; fitted++; return FIRSTENTRY(b); }
static void ErrorMsg(int code, void *arg, globaldata *g) { (void)code; (void)arg; (void)g; }
static void RemoveDirEntry(struct fileinfo info, globaldata *g) { (void)info; (void)g; }
static BOOL UpdateChangedRef(struct fileinfo from, struct fileinfo *to, int diff, globaldata *g) { (void)from; (void)to; (void)diff; (void)g; return TRUE; }
/* never reached: the entry always fits in the block in front */
static struct cdirblock *NewFirstDirBlock(ULONG a, ULONG p, globaldata *g) { (void)a; (void)p; (void)g; return NULL; }
static ULONG AllocReservedBlock(globaldata *g) { (void)g; return 0; }
static void FreeReservedBlock(ULONG b, globaldata *g) { (void)b; (void)g; }
static ULONG AllocAnode(ULONG c, globaldata *g) { (void)c; (void)g; return 0; }
static BOOL FreeAnode(ULONG nr, globaldata *g) { (void)nr; (void)g; return TRUE; }
static BOOL SaveAnode(struct canode *a, ULONG nr, globaldata *g) { (void)a; (void)nr; (void)g; return TRUE; }
static struct cdirblock *MakeDirBlock(ULONG b, ULONG a, ULONG r, ULONG p, globaldata *g) { (void)b; (void)a; (void)r; (void)p; (void)g; return NULL; }

#include "gen/GetNextAnode.c"
#include "gen/IsDelfileValid.c"
#include "gen/MoveToPrevious.c"


static globaldata G;

/* anodes nrs[0..n-1] linked in this order, the last to [last] */
static void chain(const ULONG *nrs, int n, ULONG last)
{
	int i;

	memset(nextof, 0, sizeof nextof);
	memset(used, 0, sizeof used);
	memset(taken, 0, sizeof taken);
	for (i = 0; i < n; i++) {
		used[nrs[i]] = 1;
		nextof[nrs[i]] = i + 1 < n ? nrs[i + 1] : last;
	}
	reads = runaway = freed_chain = dirty = loaded = fitted = 0;
	loaded_blocknr = 0;
}

static BOOL delfile(ULONG head, SIPTR *error, ULONG *left)
{
	struct deldirentry dde;
	struct cdeldirblock ddblk;
	BOOL r;

	dde.anodenr = head;
	*error = 0;
	r = IsDelfileValid(&dde, &ddblk, error, &G);
	*left = dde.anodenr;
	return r;
}

/* entry in the block of anode [owner] of the directory with first anode [head] */
static BOOL move(ULONG head, ULONG owner)
{
	struct cdirblock blk;
	struct direntry to, *de;
	struct fileinfo info, result;

	memset(&blk, 0, sizeof blk);
	blk.blocknr = 1000 + owner;
	blk.blk.anodenr = head;
	de = FIRSTENTRY(&blk);
	de->next = 16;
	info.direntry = de;
	info.dirblock = &blk;
	memset(&to, 0, sizeof to);
	to.next = 16;
	return MoveToPrevious(info, &to, &result, &G);
}

int main(void)
{
	SIPTR error;
	ULONG left;
	BOOL r;

	{
		static const ULONG nrs[] = { 7, 8, 9 };
		chain(nrs, 3, 0);
		r = delfile(7, &error, &left);
		CHECK(r && error == 0 && reads == 3, "delfile, valid chain: %d, error %ld, %d reads", r, (long)error, reads);
		chain(nrs, 3, 0);
		taken[9] = 1;
		r = delfile(7, &error, &left);
		CHECK(!r && error == 0 && freed_chain == 1 && left == 0 && dirty == 1, "delfile, taken block: %d, error %ld, chain freed %d, anodenr %lu", r, (long)error, freed_chain, (unsigned long)left);
	}
	{
		static const struct { const char *name; ULONG last; } c[] = {
			{ "loop back to the first anode", 7 }, { "loop back to the second anode", 8 }, { "last anode linked to itself", 9 } };
		static const ULONG nrs[] = { 7, 8, 9 };
		unsigned k;

		for (k = 0; k < sizeof(c) / sizeof(c[0]); k++) {
			chain(nrs, 3, c[k].last);
			r = delfile(7, &error, &left);
			CHECK(!r && error == PFS_ERROR_INCONSISTENT && !runaway && reads < 20 && left == 7,
				"delfile, %s: %d, error %ld, %d reads", c[k].name, r, (long)error, reads);
		}
	}
	{
		static const ULONG nrs[] = { 5, 6, 7, 8 };
		chain(nrs, 4, 0);
		r = move(5, 7);
		CHECK(r && fitted == 1 && loaded_blocknr == 1006, "move, valid chain: %d, block in front %lu", r, (unsigned long)loaded_blocknr);
		chain(nrs, 4, 6);
		r = move(5, 20);
		CHECK(!r && !runaway && reads < 20 && loaded == 0, "move, loop without the entry's block: %d, %d reads", r, reads);
	}

	if (fails) {
		printf("  %d check(s) failed\n", fails);
		return 1;
	}
	printf("  PASS: IsDelfileValid and MoveToPrevious end on a looping anode chain, valid chains are walked\n");
	return 0;
}
