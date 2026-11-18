/* LIVE @src directory.c @func DirChainEnds @func NextDirAnode @func GetFirstNonEmptyDE @func GetFirstEntry @func GetNextEntryBlock @func GetNextEntry @src anodes.c @func GetNextAnode
 *
 * Lifts the checkout's real GetFirstEntry, GetNextEntry(Block), GetFirstNonEmptyDE,
 * NextDirAnode and DirChainEnds: the walk of an ExNext/ExAll position along
 * the anode chain of a directory. GetAnode and LoadDirBlock are mocks over a
 * table; every anode has one block, holding one entry or none.
 *
 * Proven:
 *  - a valid chain is listed block by block, empty blocks are skipped, the
 *    listing ends without an error
 *  - a looping chain of empty blocks ends GetFirstEntry with
 *    PFS_ERROR_INCONSISTENT after a few reads (one packet does not hang)
 *  - a looping chain of listed blocks ends the listing with
 *    PFS_ERROR_INCONSISTENT after a few repeated blocks, also behind a
 *    listed block
 *  - an anode number that comes back ahead of the position (freed and
 *    linked in again) on a chain that ends is not a loop: the listing goes
 *    on to the end without an error
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "live.h"

#define PFS_ERROR_INCONSISTENT 213

struct direntry { UBYTE next; };
struct cdirblock { ULONG blocknr; ULONG used; struct { UBYTE entries[16]; } blk; };
struct canode { ULONG clustersize, blocknr, next, nr; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct anodewalk { ULONG mark, steps; };
typedef struct listentry { ULONG anodenr; } listentry_t;
typedef struct lockentry {
	listentry_t le;
	ULONG nextanode;
	struct anodewalk nextwalk;
	struct fileinfo nextentry;
	ULONG nextdirblocknr;
	ULONG nextdirblockoffset;
	SIPTR nexterror;
} lockentry_t;
typedef struct globaldata { ULONG locknr; } globaldata;

#define NEXTENTRY(de) ((struct direntry*)((UBYTE*)(de) + (de)->next))
#define FIRSTENTRY(blok) ((struct direntry*)((blok)->blk.entries))
#define LOCK(blk) ((blk)->used = g->locknr)

#define NTAB 64
#define CAP 100000
static ULONG nextof[NTAB];
static int used[NTAB], full[NTAB];
static struct cdirblock blocks[NTAB];
static int reads, runaway;

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

static struct cdirblock *LoadDirBlock(ULONG blocknr, globaldata *g)
{
	struct cdirblock *b = &blocks[blocknr - 1000];

	(void)g;
	memset(b, 0, sizeof(*b));
	b->blocknr = blocknr;
	if (full[blocknr - 1000])
		b->blk.entries[0] = 8;		/* one entry of 8 bytes, then the end */
	return b;
}

#include "gen/GetNextAnode.c"
#include "gen/DirChainEnds.c"
#include "gen/NextDirAnode.c"
#include "gen/GetFirstNonEmptyDE.c"
#include "gen/GetFirstEntry.c"
#include "gen/GetNextEntryBlock.c"
#include "gen/GetNextEntry.c"


static globaldata G;
static lockentry_t le;
static ULONG listed[256];
static int nlisted;

/* chain "1 2 3": anode k links to the next one, the last to [last]; [fullset]
 * has the anodes whose block holds an entry */
static void chain(const ULONG *nrs, int n, ULONG last, const ULONG *fullset, int nfull)
{
	int i;

	memset(nextof, 0, sizeof nextof);
	memset(used, 0, sizeof used);
	memset(full, 0, sizeof full);
	for (i = 0; i < n; i++) {
		used[nrs[i]] = 1;
		nextof[nrs[i]] = i + 1 < n ? nrs[i + 1] : last;
	}
	for (i = 0; i < nfull; i++)
		full[fullset[i]] = 1;
	reads = runaway = 0;
}

static void record(void)
{
	if (nlisted < 256)
		listed[nlisted++] = le.nextentry.dirblock->blocknr - 1000;
}

/* GetFirstEntry, then GetNextEntry up to [max] times; returns the listing's error */
static SIPTR list(ULONG head, int max, BOOL *first)
{
	memset(&le, 0, sizeof le);
	le.le.anodenr = head;
	nlisted = 0;
	*first = GetFirstEntry(&le, &G);
	while (le.nextentry.dirblock && !le.nexterror && max--) {
		record();
		GetNextEntry(&le, &G);
	}
	return le.nexterror;
}

static int same(const ULONG *want, int n)
{
	return nlisted == n && !memcmp(listed, want, n * sizeof(ULONG));
}

int main(void)
{
	BOOL first;
	SIPTR e;

	{
		/* valid chain, empty blocks are skipped */
		static const ULONG nrs[] = { 1, 2, 3, 4, 5, 6 }, ful[] = { 1, 3, 4, 6 };
		chain(nrs, 6, 0, ful, 4);
		e = list(1, 100, &first);
		CHECK(first && e == 0 && same(ful, 4) && !le.nextentry.dirblock, "valid chain: error %ld, %d blocks listed", (long)e, nlisted);
	}
	{
		/* valid chain, every block empty: no entry, no error */
		static const ULONG nrs[] = { 1, 2, 3 };
		chain(nrs, 3, 0, NULL, 0);
		e = list(1, 100, &first);
		CHECK(first && e == 0 && nlisted == 0, "empty chain: error %ld, %d blocks listed", (long)e, nlisted);
	}
	{
		/* empty blocks loop: 1 2 3 2, GetFirstEntry ends */
		static const ULONG nrs[] = { 1, 2, 3 };
		chain(nrs, 3, 2, NULL, 0);
		e = list(1, 100, &first);
		CHECK(!first && e == PFS_ERROR_INCONSISTENT && !runaway && reads < 40, "empty blocks loop: error %ld, %d reads%s", (long)e, reads, runaway ? ", runaway" : "");
	}
	{
		/* empty head linked to itself */
		static const ULONG nrs[] = { 1 };
		chain(nrs, 1, 1, NULL, 0);
		e = list(1, 100, &first);
		CHECK(!first && e == PFS_ERROR_INCONSISTENT && !runaway, "empty head linked to itself: error %ld, %d reads", (long)e, reads);
	}
	{
		/* listed blocks loop: 1 2 3 4 2 */
		static const ULONG nrs[] = { 1, 2, 3, 4 };
		chain(nrs, 4, 2, nrs, 4);
		e = list(1, 200, &first);
		CHECK(first && e == PFS_ERROR_INCONSISTENT && nlisted >= 4 && nlisted < 20 && !runaway, "listed blocks loop: error %ld, %d blocks listed", (long)e, nlisted);
	}
	{
		/* a listed block, then empty blocks that loop: 1 2 3 2 */
		static const ULONG nrs[] = { 1, 2, 3 }, ful[] = { 1 };
		chain(nrs, 3, 2, ful, 1);
		e = list(1, 200, &first);
		CHECK(first && e == PFS_ERROR_INCONSISTENT && same(ful, 1) && !runaway, "listed block, then an empty loop: error %ld, %d blocks listed", (long)e, nlisted);
	}
	{
		/* anode 3 is freed behind the position and linked in again ahead of
		 * it: 1 2 3 4 5 6 becomes 1 2 4 3 5 6 while the listing is at 4 */
		static const ULONG nrs[] = { 1, 2, 3, 4, 5, 6 }, moved[] = { 1, 2, 4, 3, 5, 6 };
		static const ULONG want[] = { 1, 2, 3, 4, 3, 5, 6 };
		int i;

		chain(nrs, 6, 0, nrs, 6);
		memset(&le, 0, sizeof le);
		le.le.anodenr = 1;
		nlisted = 0;
		first = GetFirstEntry(&le, &G);
		for (i = 0; i < 4 && le.nextentry.dirblock && !le.nexterror; i++) {
			record();
			if (i < 3)
				GetNextEntry(&le, &G);
		}
		chain(moved, 6, 0, moved, 6);
		GetNextEntry(&le, &G);
		while (le.nextentry.dirblock && !le.nexterror && nlisted < 50) {
			record();
			GetNextEntry(&le, &G);
		}
		CHECK(first && le.nexterror == 0 && same(want, 7) && !le.nextentry.dirblock, "anode linked in again ahead: error %ld, %d blocks listed", (long)le.nexterror, nlisted);
	}

	if (fails) {
		printf("  %d check(s) failed\n", fails);
		return 1;
	}
	printf("  PASS: ExNext walk ends on a looping chain (also of empty blocks), valid chains and anodes linked in again list to the end\n");
	return 0;
}
