/* LIVE @src update.c @func GetAnodeOfDBlk @func UpdateDBLK @func FreeEmptyDBlk @func RemoveEmptyDBlocks
 *
 * GetAnodeOfDBlk(anodenr, blocknr, anode*, prev*, g) walks the anode chain of
 * a dir block to the anode of block `blocknr`, with a walk record so that a
 * chain that loops back (also onto itself) cannot be followed forever. Its
 * callers, UpdateDBLK and FreeEmptyDBlk, must treat FALSE as "not found".
 * Lifts the real GetAnodeOfDBlk, UpdateDBLK, FreeEmptyDBlk and
 * RemoveEmptyDBlocks from update.c. GetAnode, TrySaveAnode, RemoveFromAnodeChain, the lists and
 * the cache/free bookkeeping are mocks. GetNextAnode lives in anodes.c (the
 * test can lift from one file only): the copy below is VERBATIM from
 * anodes.c. GetAnode gives up after 100000 calls (runaway = endless loop) and,
 * like the real one, leaves clustersize = next = 0 and blocknr = ~0 on failure.
 *
 * Proven for GetAnodeOfDBlk:
 *  - first anode matches: TRUE, *prev = 0 (also when *prev held garbage)
 *  - block in the 3rd anode: TRUE, anode = that one, *prev = the 2nd
 *  - block not in the chain: walk ends at the tail (TRUE, anode.blocknr differs,
 *    *prev = the anode in front of the tail)
 *  - first/later GetAnode failure: FALSE; self loop, 2-cycle, tail into a loop: FALSE
 *    after a handful of reads, no endless loop
 *  - valid chains of any length (1..200, 70000) are never rejected
 * UpdateDBLK: walk success -> anode saved with new blocknr, dir block marked
 *  changed and rehashed; TrySaveAnode failure -> FALSE, nothing changed;
 *  FALSE from the walk -> FALSE without the "inconsistency" message (that is
 *  only for a chain without the block) and without saving anything, even if
 *  the leftover anode of a failed read happens to carry the wanted blocknr.
 * RemoveEmptyDBlocks (through FreeEmptyDBlk): empty blocks behind the first
 *  one with a good chain are removed (prev passed on); blocks whose chain
 *  loops / can't be read / lacks the block are kept, unlocked again, updateok
 *  is cleared, and the loop goes on with the next block; a failed walk never
 *  reaches RemoveFromAnodeChain. The first block of a directory and blocks
 *  with an unreadable head anode are kept without clearing updateok; a block
 *  locked by the packet is not even looked at.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG; typedef uint16_t UWORD; typedef int16_t WORD; typedef uint8_t UBYTE; typedef int8_t BYTE; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DB(x)
#define HASHM_DIR 0x1f
#define AFS_ERROR_CACHE_INCONSISTENCY "cache inconsistency"

struct canode { ULONG clustersize, blocknr, next, nr; };
struct anodewalk { ULONG mark, steps; };

struct MN { struct MN *next, *prev; };
struct ML { struct MN *mlh_Head, *mlh_Tail, *mlh_TailPred; };		/* Exec MinList layout */

struct cachedblock
{
	struct cachedblock *next, *prev;
	struct volumedata *volume;
	ULONG blocknr, oldblocknr;
	UWORD used;
	UBYTE changeflag, dummy;
};
struct direntry { UBYTE next; BYTE type; };
struct dirblock
{
	UWORD id, not_used;
	ULONG datestamp;
	UWORD not_used_2[2];
	ULONG anodenr, parent;
	UBYTE entries[16];
};
struct cdirblock
{
	struct cdirblock *next, *prev;
	struct volumedata *volume;
	ULONG blocknr, oldblocknr;
	UWORD used;
	UBYTE changeflag, dummy;
	struct dirblock blk;
};
struct volumedata { struct ML dirblks[HASHM_DIR + 1]; };

typedef struct globaldata
{
	ULONG locknr;
	BOOL updateok;
	struct volumedata *currentvolume;
} globaldata;

/* macros as in struct.h / blocks.h / update.c */
#define LOCK(blk) ((blk)->used = g->locknr)
#define ISLOCKED(blk) ((blk)->used == g->locknr)
#define HeadOf(list) ((void *)((list)->mlh_Head))
#define FIRSTENTRY(blok) ((struct direntry *)((blok)->blk.entries))
#define IsEmptyDBlk(blk) (FIRSTENTRY(blk)->next == 0)
#define updateok g->updateok
#define MinRemove(node) mock_remove((struct MN *)(node))

/* ---- mock anode store ---- */
#define MAXN 70100
#define CAP 100000UL
static struct { ULONG blocknr, next; int exists; } anodes[MAXN];
static ULONG getcalls;
static int runaway, failnr;

static BOOL GetAnode(struct canode *anode, ULONG anodenr, globaldata *g)
{
	(void)g;
	getcalls++;
	if (getcalls > CAP)
	{
		runaway = 1;
		goto failed;
	}
	if (anodenr >= MAXN || !anodes[anodenr].exists || (ULONG)failnr == anodenr)
		goto failed;
	anode->clustersize = 1;
	anode->blocknr = anodes[anodenr].blocknr;
	anode->next = anodes[anodenr].next;
	anode->nr = anodenr;
	return TRUE;
failed:
	anode->clustersize = anode->next = 0;
	anode->blocknr = 0xFFFFFFFFUL;
	anode->nr = anodenr;
	return FALSE;
}

/* VERBATIM copy of GetNextAnode from anodes.c */
BOOL GetNextAnode(struct canode *anode, ULONG anodenr, struct anodewalk *walk, globaldata *g)
{
	if (anodenr == walk->mark)
		return FALSE;
	walk->steps++;
	if (!(walk->steps & (walk->steps - 1)))
		walk->mark = anodenr;
	return GetAnode(anode, anodenr, g);
}

/* ---- mocks for UpdateDBLK / RemoveEmptyDBlocks ---- */
static int ntrysave, trysave_ok, nerrmsg, nrehash, nremovechain, nfreelru, nfreereserved, ntofree;
static ULONG saved_nr, saved_blocknr;
static ULONG rc_nr[8], rc_prev[8], rc_anodenr[8];
static ULONG freeres[8], tofree[8];

static BOOL TrySaveAnode(struct canode *anode, ULONG anodenr, globaldata *g)
{
	ntrysave++;
	saved_nr = anodenr;
	saved_blocknr = anode->blocknr;
	return trysave_ok;
}
#define ReHash(blk, list, mask) (nrehash++)
static long ErrorMsg(const char *m, void *args, globaldata *g) { nerrmsg++; return 0; }
static BOOL RemoveFromAnodeChain(struct canode *anode, ULONG previous, ULONG anodenr, globaldata *g)
{
	if (nremovechain < 8) { rc_nr[nremovechain] = anode->nr; rc_prev[nremovechain] = previous; rc_anodenr[nremovechain] = anodenr; }
	nremovechain++;
	return TRUE;
}
static void FreeReservedBlock(ULONG blocknr, globaldata *g) { if (nfreereserved < 8) freeres[nfreereserved] = blocknr; nfreereserved++; }
static void ResToBeFreed(ULONG blocknr, globaldata *g) { if (ntofree < 8) tofree[ntofree] = blocknr; ntofree++; }
#define FreeLRU(blk) (nfreelru++)

static void ml_init(struct ML *l) { l->mlh_Head = (struct MN *)&l->mlh_Tail; l->mlh_Tail = NULL; l->mlh_TailPred = (struct MN *)l; }
static void ml_addtail(struct ML *l, struct MN *n)
{
	n->next = (struct MN *)&l->mlh_Tail; n->prev = l->mlh_TailPred;
	l->mlh_TailPred->next = n; l->mlh_TailPred = n;
}
static void mock_remove(struct MN *n) { n->prev->next = n->next; n->next->prev = n->prev; }

#include "gen/GetAnodeOfDBlk.c"                  /* the REAL functions from ../../update.c */
#include "gen/UpdateDBLK.c"
#include "gen/FreeEmptyDBlk.c"
#include "gen/RemoveEmptyDBlocks.c"
#undef updateok				/* only the lifted code needs the macro */

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata G;
static struct volumedata VOL;

static void reset(void)
{
	memset(anodes, 0, sizeof anodes);
	getcalls = 0; runaway = 0; failnr = 0;
	ntrysave = nerrmsg = nrehash = nremovechain = nfreelru = nfreereserved = ntofree = 0;
	trysave_ok = TRUE;
	memset(&G, 0, sizeof G);
	G.locknr = 7;
	G.currentvolume = &VOL;
}

/* anodes first..first+n-1 form a chain, anode first+i owns block base+i */
static void chain(ULONG first, ULONG n, ULONG base)
{
	ULONG i;
	for (i = 0; i < n; i++)
	{
		anodes[first + i].exists = 1;
		anodes[first + i].blocknr = base + i;
		anodes[first + i].next = i + 1 < n ? first + i + 1 : 0;
	}
}

static BOOL walk(ULONG anodenr, ULONG blocknr, struct canode *an, ULONG *prev)
{
	BOOL r;
	getcalls = 0; runaway = 0;
	*prev = 0xDEADBEEFUL;				/* must be set by the function */
	r = GetAnodeOfDBlk(anodenr, blocknr, an, prev, &G);
	CHECK(!runaway, "walk %lu/%lu: endless loop", (unsigned long)anodenr, (unsigned long)blocknr);
	return r;
}

static void test_walk(void)
{
	struct canode an;
	ULONG prev, n;

	reset();
	chain(20, 3, 200);				/* 20(200) -> 21(201) -> 22(202) */
	CHECK(walk(20, 200, &an, &prev) == TRUE && an.nr == 20 && prev == 0 && getcalls == 1, "first anode: nr %lu prev %lu reads %lu", (unsigned long)an.nr, (unsigned long)prev, (unsigned long)getcalls);
	CHECK(walk(20, 201, &an, &prev) == TRUE && an.nr == 21 && an.blocknr == 201 && prev == 20 && getcalls == 2, "second anode: nr %lu prev %lu", (unsigned long)an.nr, (unsigned long)prev);
	CHECK(walk(20, 202, &an, &prev) == TRUE && an.nr == 22 && prev == 21 && getcalls == 3, "third anode: nr %lu prev %lu", (unsigned long)an.nr, (unsigned long)prev);
	CHECK(walk(21, 202, &an, &prev) == TRUE && an.nr == 22 && prev == 21, "start in the middle of the chain");
	CHECK(walk(20, 999, &an, &prev) == TRUE && an.nr == 22 && an.blocknr == 202 && prev == 21 && an.next == 0, "block not in chain: must end at the tail (nr %lu prev %lu)", (unsigned long)an.nr, (unsigned long)prev);
	reset();
	chain(5, 1, 50);
	CHECK(walk(5, 77, &an, &prev) == TRUE && an.nr == 5 && an.blocknr == 50 && prev == 0, "single anode without the block: tail, prev 0");

	/* read failures */
	reset();
	chain(20, 3, 200);
	CHECK(walk(30, 200, &an, &prev) == FALSE && prev == 0, "unknown first anode must fail with prev 0");
	failnr = 20;
	CHECK(walk(20, 200, &an, &prev) == FALSE && prev == 0, "failing first GetAnode must fail");
	failnr = 21;
	CHECK(walk(20, 202, &an, &prev) == FALSE, "failing later GetAnode must fail");
	CHECK(walk(20, 200, &an, &prev) == TRUE && an.nr == 20, "an anode behind the wanted one is never read");
	failnr = 22;
	CHECK(walk(20, 201, &an, &prev) == TRUE && an.nr == 21 && prev == 20, "failing tail not read when the block is before it");
	CHECK(walk(20, 202, &an, &prev) == FALSE, "failing last anode must fail");

	/* loops */
	reset();
	chain(1, 1, 10); anodes[1].next = 1;
	CHECK(walk(1, 99, &an, &prev) == FALSE && getcalls <= 4, "self loop: must fail quickly (%lu reads)", (unsigned long)getcalls);
	CHECK(walk(1, 10, &an, &prev) == TRUE && prev == 0 && getcalls == 1, "self loop, block in the first anode: found");
	reset();
	chain(1, 2, 10); anodes[2].next = 1;
	CHECK(walk(1, 99, &an, &prev) == FALSE && getcalls <= 8, "loop 1-2-1: must fail quickly (%lu reads)", (unsigned long)getcalls);
	CHECK(walk(1, 11, &an, &prev) == TRUE && an.nr == 2 && prev == 1, "loop 1-2-1, block in 2nd anode: found");
	reset();
	chain(1, 6, 10); anodes[6].next = 3;
	CHECK(walk(1, 99, &an, &prev) == FALSE && getcalls <= 40, "tail into loop: must fail quickly (%lu reads)", (unsigned long)getcalls);
	CHECK(walk(1, 15, &an, &prev) == TRUE && an.nr == 6 && prev == 5, "tail into loop, block before the loop closes: found");
	reset();
	chain(7, 2, 70); anodes[8].next = 7;
	CHECK(walk(7, 99, &an, &prev) == FALSE && getcalls <= 8, "loop not starting at 1: must fail quickly");

	/* valid chains are never rejected */
	for (n = 1; n <= 200; n++)
	{
		reset();
		chain(1, n, 1000);
		CHECK(walk(1, 1000 + n - 1, &an, &prev) == TRUE && an.nr == n && prev == (n > 1 ? n - 1 : 0), "valid chain of %lu: last anode not reached", (unsigned long)n);
		CHECK(walk(1, 5, &an, &prev) == TRUE && an.nr == n && an.next == 0, "valid chain of %lu: tail not reached", (unsigned long)n);
	}
	reset();
	chain(1, 70000, 100000);
	CHECK(walk(1, 100000 + 69999, &an, &prev) == TRUE && an.nr == 70000 && prev == 69999, "valid chain of 70000: last anode not reached");
	CHECK(walk(1, 5, &an, &prev) == TRUE && an.nr == 70000 && an.blocknr != 5, "valid chain of 70000: not ended at the tail");
}

static struct cdirblock DB1;

static BOOL update(ULONG anodenr, ULONG oldblocknr, ULONG newblocknr)
{
	memset(&DB1, 0, sizeof DB1);
	DB1.blk.anodenr = anodenr;
	DB1.oldblocknr = oldblocknr;
	DB1.blocknr = newblocknr;
	ntrysave = nerrmsg = nrehash = 0; saved_nr = saved_blocknr = 0;
	getcalls = 0; runaway = 0;
	return UpdateDBLK((struct cachedblock *)&DB1, newblocknr, &G);
}

static void test_updatedblk(void)
{
	reset();
	chain(20, 3, 200);
	CHECK(update(20, 200, 555) == TRUE && ntrysave == 1 && saved_nr == 20 && saved_blocknr == 555 && DB1.changeflag == TRUE && nrehash == 1 && !nerrmsg, "first anode: updated");
	CHECK(update(20, 202, 556) == TRUE && saved_nr == 22 && saved_blocknr == 556 && DB1.changeflag == TRUE && nrehash == 1, "third anode: saved anode %lu", (unsigned long)saved_nr);
	CHECK(DB1.used == G.locknr, "dir block must be locked");

	trysave_ok = FALSE;
	CHECK(update(20, 201, 557) == FALSE && ntrysave == 1 && DB1.changeflag == 0 && nrehash == 0, "TrySaveAnode failure: block must stay unchanged");
	trysave_ok = TRUE;

	/* the chain does not contain the block: inconsistency message */
	CHECK(update(20, 999, 558) == FALSE && nerrmsg == 1 && ntrysave == 0 && DB1.changeflag == 0 && nrehash == 0 && !runaway, "block not in chain: FALSE with message, nothing saved");

	/* the walk fails: FALSE, no message, nothing saved */
	failnr = 20;
	CHECK(update(20, 200, 559) == FALSE && nerrmsg == 0 && ntrysave == 0 && DB1.changeflag == 0 && nrehash == 0, "first anode unreadable: quiet FALSE (msg %d, saves %d)", nerrmsg, ntrysave);
	failnr = 21;
	CHECK(update(20, 202, 559) == FALSE && nerrmsg == 0 && ntrysave == 0 && DB1.changeflag == 0, "later anode unreadable: quiet FALSE (msg %d, saves %d)", nerrmsg, ntrysave);
	failnr = 0;
	reset();
	chain(1, 3, 10); anodes[3].next = 1;
	CHECK(update(1, 999, 560) == FALSE && !runaway && nerrmsg == 0 && ntrysave == 0 && DB1.changeflag == 0 && getcalls <= 16, "looping chain: quiet FALSE, no endless loop (msg %d, saves %d, reads %lu)", nerrmsg, ntrysave, (unsigned long)getcalls);
	reset();
	chain(1, 1, 10); anodes[1].next = 1;
	CHECK(update(1, 999, 560) == FALSE && !runaway && nerrmsg == 0 && ntrysave == 0, "self loop: quiet FALSE, no endless loop");

	/* the failed read leaves blocknr = ~0: even a dir block "at" ~0 must not be treated as found */
	reset();
	CHECK(update(30, 0xFFFFFFFFUL, 561) == FALSE && ntrysave == 0 && DB1.changeflag == 0 && nrehash == 0, "FALSE of the walk treated as found (saves %d)", ntrysave);
	reset();
	chain(1, 2, 10); failnr = 2;
	CHECK(update(1, 0xFFFFFFFFUL, 562) == FALSE && ntrysave == 0 && DB1.changeflag == 0, "failed later read treated as found (saves %d)", ntrysave);
}

/* ---- RemoveEmptyDBlocks ---- */
#define NB 12
static struct cdirblock BL[NB];

static struct cdirblock *addblk(int list, int idx, ULONG blocknr, ULONG anodenr, int empty)
{
	struct cdirblock *b = &BL[idx];
	memset(b, 0, sizeof *b);
	b->blocknr = blocknr;
	b->oldblocknr = blocknr + 1000;
	b->blk.anodenr = anodenr;
	b->blk.entries[0] = empty ? 0 : 20;
	ml_addtail(&VOL.dirblks[list], (struct MN *)b);
	return b;
}
static int inlist(struct cdirblock *b)
{
	struct MN *n;
	int i;
	for (i = 0; i <= HASHM_DIR; i++)
		for (n = VOL.dirblks[i].mlh_Head; n->next; n = n->next)
			if (n == (struct MN *)b)
				return 1;
	return 0;
}

static void test_remove(void)
{
	int i;
	struct cdirblock *ok1, *ok2, *loop, *unread, *missing, *nonempty, *ok3, *selfloop, *first, *locked;

	reset();
	for (i = 0; i <= HASHM_DIR; i++)
		ml_init(&VOL.dirblks[i]);
	chain(10, 2, 100);					/* 10(100) -> 11(101) */
	chain(20, 2, 200);					/* 20(200) -> 21(201) */
	chain(30, 2, 300); anodes[31].next = 30;	/* loop, blocks 300/301 */
	chain(50, 2, 500);					/* chain without block 599 */
	chain(70, 3, 700);					/* 70 -> 71 -> 72 */
	chain(80, 1, 800); anodes[80].next = 80;	/* anode linking itself */
	chain(90, 2, 900);					/* 90(900) -> 91(901) */
	/* anode 40 does not exist */

	/* list 0: ok1, loop, unread, ok2 ; list 3: missing, nonempty, selfloop, first, ok3, locked */
	ok1 = addblk(0, 0, 101, 10, 1);
	loop = addblk(0, 1, 399, 30, 1);
	unread = addblk(0, 2, 401, 40, 1);
	ok2 = addblk(0, 3, 201, 20, 1);
	ok2->changeflag = TRUE;
	missing = addblk(3, 4, 599, 50, 1);
	nonempty = addblk(3, 5, 701, 70, 0);
	selfloop = addblk(3, 6, 899, 80, 1);
	first = addblk(3, 7, 200, 20, 1);			/* head anode 20 owns it */
	ok3 = addblk(3, 8, 702, 70, 1);
	locked = addblk(3, 9, 901, 90, 1);
	locked->used = G.locknr;				/* in use by the packet */

	RemoveEmptyDBlocks(&VOL, &G);

	CHECK(!runaway, "RemoveEmptyDBlocks: endless loop on a looping anode chain");
	CHECK(!inlist(ok1) && !inlist(ok2) && !inlist(ok3), "blocks with a good chain must be removed (%d %d %d still listed)", inlist(ok1), inlist(ok2), inlist(ok3));
	CHECK(inlist(loop) && inlist(unread) && inlist(missing) && inlist(nonempty) && inlist(selfloop) && inlist(first) && inlist(locked), "blocks with a bad chain, non-empty, first or locked block must stay (loop %d unread %d missing %d nonempty %d selfloop %d first %d locked %d)", inlist(loop), inlist(unread), inlist(missing), inlist(nonempty), inlist(selfloop), inlist(first), inlist(locked));
	CHECK(nremovechain == 3 && nfreelru == 3, "RemoveFromAnodeChain called %d times (want 3), FreeLRU %d", nremovechain, nfreelru);
	CHECK(rc_nr[0] == 11 && rc_prev[0] == 10 && rc_anodenr[0] == 10, "ok1: removed with anode %lu prev %lu first %lu", (unsigned long)rc_nr[0], (unsigned long)rc_prev[0], (unsigned long)rc_anodenr[0]);
	CHECK(rc_nr[1] == 21 && rc_prev[1] == 20 && rc_anodenr[1] == 20, "ok2: removed with anode %lu prev %lu first %lu", (unsigned long)rc_nr[1], (unsigned long)rc_prev[1], (unsigned long)rc_anodenr[1]);
	CHECK(rc_nr[2] == 72 && rc_prev[2] == 71 && rc_anodenr[2] == 70, "ok3: removed with anode %lu prev %lu first %lu", (unsigned long)rc_nr[2], (unsigned long)rc_prev[2], (unsigned long)rc_anodenr[2]);
	/* a changed block frees its reserved block and queues the old location, an unchanged one its own */
	CHECK(nfreereserved == 1 && freeres[0] == 201 && ntofree == 3 && tofree[0] == 101 && tofree[1] == 1201 && tofree[2] == 702, "block accounting: %d reserved, %d to free", nfreereserved, ntofree);
	CHECK(G.updateok == FALSE, "a block that could not be removed must clear updateok");
	CHECK(loop->used == 0 && unread->used == 0 && missing->used == 0 && selfloop->used == 0 && nonempty->used == 0 && first->used == 0, "kept blocks must be unlocked again");
	CHECK(locked->used == G.locknr, "a block locked by the packet must stay locked");

	/* the first block, an unreadable head and a locked block: kept, updateok untouched, no read for the locked one */
	reset();
	for (i = 0; i <= HASHM_DIR; i++)
		ml_init(&VOL.dirblks[i]);
	chain(20, 2, 200);
	first = addblk(1, 0, 200, 20, 1);
	unread = addblk(1, 1, 401, 40, 1);
	G.updateok = TRUE;
	RemoveEmptyDBlocks(&VOL, &G);
	CHECK(inlist(first) && inlist(unread) && nremovechain == 0 && nfreelru == 0 && G.updateok == TRUE, "first block / unreadable head: kept %d %d, updateok %d", inlist(first), inlist(unread), G.updateok);
	reset();
	for (i = 0; i <= HASHM_DIR; i++)
		ml_init(&VOL.dirblks[i]);
	chain(20, 2, 200);
	locked = addblk(1, 0, 201, 20, 1);
	locked->used = G.locknr;
	G.updateok = TRUE;
	RemoveEmptyDBlocks(&VOL, &G);
	CHECK(inlist(locked) && getcalls == 0 && G.updateok == TRUE, "locked block: kept %d, %lu anode reads", inlist(locked), (unsigned long)getcalls);

	/* only good chains: updateok untouched */
	reset();
	for (i = 0; i <= HASHM_DIR; i++)
		ml_init(&VOL.dirblks[i]);
	chain(10, 2, 100);
	ok1 = addblk(1, 0, 101, 10, 1);
	G.updateok = TRUE;
	RemoveEmptyDBlocks(&VOL, &G);
	CHECK(!inlist(ok1) && nremovechain == 1 && G.updateok == TRUE, "only good blocks: removed, updateok must stay TRUE");

	/* a failed walk must not be taken for 'found' even if the leftover anode carries the block number */
	reset();
	for (i = 0; i <= HASHM_DIR; i++)
		ml_init(&VOL.dirblks[i]);
	chain(40, 2, 400); failnr = 41;
	unread = addblk(2, 0, 0xFFFFFFFFUL, 40, 1);			/* anode 41 unreadable: blocknr = ~0 */
	G.updateok = TRUE;
	RemoveEmptyDBlocks(&VOL, &G);
	CHECK(inlist(unread) && nremovechain == 0 && nfreelru == 0 && G.updateok == FALSE, "failed walk treated as found: removed %d, RemoveFromAnodeChain %d", !inlist(unread), nremovechain);
}

int main(void)
{
	test_walk();
	test_updatedblk();
	test_remove();

	if (fails) { printf("  %d check(s) failed\n", fails); return 1; }
	printf("  PASS: GetAnodeOfDBlk ends on loops/failures, UpdateDBLK and RemoveEmptyDBlocks honour FALSE\n");
	return 0;
}
