/* LIVE @src lru.c @func EvictLRU @func SetLRUSize @func ResizeLRU
 *
 * ResizeLRU changes the number of cache buffers in place. The request is
 * clamped to MIN_BUFFERS..MAX_BUFFERS (the upper clamp is new), the
 * resulting pool size is returned, and AllocLRU relies on that:
 * `SetLRUSize(g, poolsize + NEW_LRU_ENTRIES, LONG_MAX) <= poolsize` means "the
 * cache can not grow any more" (ResizeLRU is SetLRUSize limited to MAX_BUFFERS). The REAL ResizeLRU and EvictLRU (used when shrinking)
 * are lifted from lru.c; the allocator (AllocVec/FreeVec with a registry that
 * detects leaks and double frees and can fail on demand or when memory is
 * "exhausted"), the list primitives, FlushBlock and ISLOCKED are mocks.
 * MIN_BUFFERS / MAX_BUFFERS are copied from lru.c.
 *
 * Proven:
 *  - request above MAX_BUFFERS is clamped to MAX_BUFFERS (exactly MAX_BUFFERS
 *    entries allocated, no memory exhaustion from a huge request), one below
 *    MIN_BUFFERS is raised to MIN_BUFFERS (also 0 and negative); MIN and MAX
 *    themselves, and MAX-1, are not clamped
 *  - the cache never grows past MAX_BUFFERS, also through repeated calls the
 *    way AllocLRU makes them (pool + 5): the call at MAX_BUFFERS returns
 *    MAX_BUFFERS (<= poolsize) and allocates nothing
 *  - growing and shrinking keep pool size, de_NumBuffers, the LRUarray, the
 *    two lists and the allocator's live blocks consistent
 *  - shrinking takes pool entries first, then evicts unlocked, linked blocks
 *    from the queue; it stops early (returning the larger count) when only
 *    locked/unlinked/pending blocks remain
 *  - a failing allocation at ANY point of a grow (array or any entry) gives 0
 *    and leaves the pool exactly as it was, with everything new freed again
 *  - reserved_blksize 0 gives 0 and touches nothing
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef uint8_t UBYTE; typedef int BOOL;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define MEMF_CLEAR (1UL << 16)
#define MIN_BUFFERS 10
#define MAX_BUFFERS 600
#define NEW_LRU_ENTRIES 5
#define DB(x)
#define CBLK_DIRTY_PENDING 2
#define RESCLUSTER 1
#define AFS_ERROR_LRU_UPDATE_FAIL "update"
#define AFS_WARNING_MEMORY_MASK "memory mask"

struct N  { struct N *next, *prev; };
struct ML { struct N *mlh_Head, *mlh_Tail, *mlh_TailPred; };		/* Exec MinList layout */

struct cachedblock
{
	struct cachedblock *next, *prev;
	ULONG used, blocknr, oldblocknr;
	UBYTE changeflag;
	UBYTE data[4];
};
struct lru_cachedblock { struct lru_cachedblock *next, *prev; struct cachedblock cblk; };
struct DosEnvec { ULONG de_NumBuffers, de_BufMemType, de_Mask; };

typedef struct globaldata
{
	struct
	{
		struct ML LRUqueue, LRUpool;
		LONG poolsize;
		struct lru_cachedblock **LRUarray;
		UWORD reserved_blksize;
	} glob_lrudata;
	ULONG locknr;
	struct DosEnvec *dosenvec;
} globaldata;

#define ISLOCKED(blk) ((blk)->used == g->locknr)
#define MinRemove(n) mock_remove((struct N *)(n))
#define MinAddHead(l, n) mock_addhead((struct ML *)(l), (struct N *)(n))
#define HeadOf(l) ((void *)((l)->mlh_Head))
#define IsMinListEmpty(x) (((x)->mlh_TailPred) == (struct N *)(x))

static void ml_init(struct ML *l) { l->mlh_Head = (struct N *)&l->mlh_Tail; l->mlh_Tail = NULL; l->mlh_TailPred = (struct N *)l; }
static void mock_addhead(struct ML *l, struct N *n)
{
	n->next = l->mlh_Head; n->prev = (struct N *)l;
	l->mlh_Head->prev = n; l->mlh_Head = n;
}
static void mock_remove(struct N *n) { n->prev->next = n->next; n->next->prev = n->prev; }

/* ---- mock allocator with a registry ---- */
#define REG 2000
static struct { void *p; size_t size; ULONG flags; int live; } reg[REG];
static int nreg, livecount, badfree, nallocs, fail_at, live_limit, nflush, nmaskwarn, nupdatefail;

static void *AllocVec(ULONG size, ULONG flags)
{
	void *p;
	nallocs++;
	if (fail_at && nallocs == fail_at)
		return NULL;
	if (live_limit && livecount >= live_limit)
		return NULL;
	if (nreg >= REG)			/* registry full: unbounded growth looks like exhausted memory */
		return NULL;
	p = calloc(1, size);
	reg[nreg].p = p; reg[nreg].size = size; reg[nreg].flags = flags; reg[nreg].live = 1;
	nreg++; livecount++;
	return p;
}
static void FreeVec(void *p)
{
	int i;
	if (!p)
		return;
	for (i = 0; i < nreg; i++)
		if (reg[i].p == p && reg[i].live)
		{
			reg[i].live = 0; livecount--;
			return;				/* the memory is kept so a stale use is not a crash here */
		}
	badfree++;
}
static void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }
static int is_live(void *p, size_t *size, ULONG *flags)
{
	int i;
	for (i = 0; i < nreg; i++)
		if (reg[i].p == p && reg[i].live)
		{
			if (size) *size = reg[i].size;
			if (flags) *flags = reg[i].flags;
			return 1;
		}
	return 0;
}

static void FlushBlock(struct cachedblock *b, globaldata *g) { nflush++; b->next = b->prev = NULL; b->used = 0; b->changeflag = 0; }
static void UpdateDatestamp(struct cachedblock *b, globaldata *g) {}
static ULONG RawWrite(UBYTE *d, ULONG n, ULONG blk, globaldata *g) { return 0; }
static void ResToBeFreed(ULONG blk, globaldata *g) {}
static long ErrorMsg(const char *m, void *a, globaldata *g)
{
	if (m == AFS_WARNING_MEMORY_MASK) nmaskwarn++;
	if (m == AFS_ERROR_LRU_UPDATE_FAIL) nupdatefail++;
	return 0;
}

#include "gen/EvictLRU.c"                  /* the REAL functions from ../../lru.c */
#include "gen/SetLRUSize.c"
#include "gen/ResizeLRU.c"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata G;
static struct DosEnvec ENV;
#define BLKSIZE 512
#define ENTRYSIZE (sizeof(struct lru_cachedblock) + BLKSIZE)

static void setup(void)
{
	memset(&G, 0, sizeof G);
	memset(&ENV, 0, sizeof ENV);
	ml_init(&G.glob_lrudata.LRUqueue);
	ml_init(&G.glob_lrudata.LRUpool);
	G.glob_lrudata.reserved_blksize = BLKSIZE;
	G.locknr = 1;
	G.dosenvec = &ENV;
	ENV.de_BufMemType = 4;
	ENV.de_Mask = 0xFFFFFFFFUL;
	memset(reg, 0, sizeof reg);
	nreg = livecount = badfree = nallocs = fail_at = live_limit = nflush = nmaskwarn = nupdatefail = 0;
}

static int listlen(struct ML *l, struct lru_cachedblock **arr, LONG count, int *bad)
{
	struct N *n;
	int len = 0, i, found;
	for (n = l->mlh_Head; n->next; n = n->next)
	{
		found = 0;
		for (i = 0; i < count; i++)
			found += arr[i] == (struct lru_cachedblock *)n;
		if (found != 1)
			(*bad)++;
		len++;
		if (len > 5000) { (*bad)++; break; }
	}
	return len;
}

/* the pool is consistent with the allocator, the array and both lists */
static int consistent(const char *what, LONG count)
{
	LONG i, j, c = G.glob_lrudata.poolsize;
	struct lru_cachedblock **arr = G.glob_lrudata.LRUarray;
	int bad = 0, ok = 1, nq, np;
	size_t size;
	ULONG flags;

	if (c != count || (LONG)ENV.de_NumBuffers != count)
	{
		printf("  FAIL: %s: poolsize %ld, de_NumBuffers %ld, expected %ld\n", what, (long)c, (long)ENV.de_NumBuffers, (long)count);
		ok = 0;
	}
	if (count && !arr)
	{
		printf("  FAIL: %s: no LRUarray for %ld buffers\n", what, (long)count);
		fails++;
		return 0;
	}
	for (i = 0; i < count; i++)
	{
		if (!arr[i] || !is_live(arr[i], &size, &flags) || size != ENTRYSIZE || flags != (ENV.de_BufMemType | MEMF_CLEAR))
			bad++;
		for (j = 0; j < i; j++)
			if (arr[i] == arr[j])
				bad++;
	}
	if (arr)
	{
		if (!is_live(arr, &size, &flags) || flags != MEMF_CLEAR)
			bad++;
		else
			for (i = count; i < (LONG)(size / sizeof *arr); i++)
				if (arr[i])
					bad++;		/* stale entries behind the used part */
	}
	nq = listlen(&G.glob_lrudata.LRUqueue, arr, count, &bad);
	np = listlen(&G.glob_lrudata.LRUpool, arr, count, &bad);
	if (nq + np != count)
		bad++;
	if (livecount != (count ? count + 1 : (arr ? 1 : 0)))
		bad++;
	if (bad || badfree)
	{
		printf("  FAIL: %s: %d inconsistencies (queue %d, pool %d, live blocks %d, bad frees %d)\n", what, bad, nq, np, livecount, badfree);
		ok = 0;
	}
	if (!ok)
		fails++;
	return ok;
}

static void expect_resize(const char *name, LONG request, LONG want)
{
	LONG r = ResizeLRU(&G, request);
	CHECK(r == want, "%s: ResizeLRU(%ld) returned %ld, expected %ld", name, (long)request, (long)r, (long)want);
	consistent(name, want);
}

/* move the first n pool entries into the queue (as cache blocks in use) */
static void use_blocks(int n, int locked, int linked, int pending)
{
	int i;
	for (i = 0; i < n && !IsMinListEmpty(&G.glob_lrudata.LRUpool); i++)
	{
		struct lru_cachedblock *b = HeadOf(&G.glob_lrudata.LRUpool);
		MinRemove(b);
		MinAddHead(&G.glob_lrudata.LRUqueue, b);
		b->cblk.used = locked ? G.locknr : 0;
		b->cblk.next = linked ? &b->cblk : NULL;
		b->cblk.changeflag = pending ? CBLK_DIRTY_PENDING : 0;
		b->cblk.blocknr = 1000 + i;
	}
}

static void test_clamps(void)
{
	LONG i;

	setup();
	CHECK(ResizeLRU(&G, 30) == 30, "grow from nothing");
	consistent("grow to 30", 30);

	setup();
	expect_resize("above MAX from empty", MAX_BUFFERS + 1, MAX_BUFFERS);
	CHECK(livecount == MAX_BUFFERS + 1, "above MAX: %d live blocks, expected %d", livecount, MAX_BUFFERS + 1);
	expect_resize("repeat above MAX", MAX_BUFFERS + 1, MAX_BUFFERS);
	expect_resize("far above MAX", 1000000, MAX_BUFFERS);
	expect_resize("0x7fffffff", 0x7FFFFFFF, MAX_BUFFERS);
	expect_resize("exactly MAX", MAX_BUFFERS, MAX_BUFFERS);
	expect_resize("MAX-1 shrinks by one", MAX_BUFFERS - 1, MAX_BUFFERS - 1);
	expect_resize("MAX again", MAX_BUFFERS, MAX_BUFFERS);

	/* huge request while memory runs out soon: the clamp keeps it within what is allowed */
	setup();
	live_limit = MAX_BUFFERS + 10;
	expect_resize("huge request, small memory", 100000, MAX_BUFFERS);

	setup();
	expect_resize("below MIN: MIN-1", MIN_BUFFERS - 1, MIN_BUFFERS);
	expect_resize("below MIN: 0", 0, MIN_BUFFERS);
	expect_resize("below MIN: negative", -100, MIN_BUFFERS);
	expect_resize("exactly MIN", MIN_BUFFERS, MIN_BUFFERS);
	expect_resize("MIN+1", MIN_BUFFERS + 1, MIN_BUFFERS + 1);
	expect_resize("shrink below MIN", 3, MIN_BUFFERS);
	setup();
	expect_resize("empty, below MIN", 3, MIN_BUFFERS);

	/* the way AllocLRU grows: poolsize + NEW_LRU_ENTRIES, until no growth is reported */
	setup();
	expect_resize("start", 30, 30);
	for (i = 0; i < 500; i++)
	{
		LONG pool = G.glob_lrudata.poolsize, r;
		int before = nallocs;
		r = ResizeLRU(&G, pool + NEW_LRU_ENTRIES);
		if (pool + NEW_LRU_ENTRIES <= MAX_BUFFERS)
			CHECK(r == pool + NEW_LRU_ENTRIES, "AllocLRU style growth %ld -> %ld", (long)pool, (long)r);
		else
			CHECK(r == MAX_BUFFERS, "growth near the limit %ld -> %ld, expected %d", (long)pool, (long)r, MAX_BUFFERS);
		if (pool == MAX_BUFFERS)
		{
			CHECK(r <= pool, "at MAX_BUFFERS ResizeLRU must report no growth (returned %ld)", (long)r);
			CHECK(nallocs == before, "at MAX_BUFFERS nothing may be allocated");
			break;
		}
		CHECK(r > pool, "growth stalled at %ld", (long)pool);
		if (r <= pool)
			break;
	}
	CHECK(G.glob_lrudata.poolsize == MAX_BUFFERS && i < 500, "the pool never reached MAX_BUFFERS, or took too many steps (%ld)", (long)i);
	consistent("AllocLRU style growth", MAX_BUFFERS);
	CHECK(livecount == MAX_BUFFERS + 1, "cache grew past MAX_BUFFERS (%d live blocks)", livecount);
	expect_resize("more calls at MAX", MAX_BUFFERS + NEW_LRU_ENTRIES, MAX_BUFFERS);
	expect_resize("more calls at MAX", MAX_BUFFERS + 1, MAX_BUFFERS);

	/* just below the limit: one call grows to exactly MAX_BUFFERS */
	setup();
	expect_resize("599", MAX_BUFFERS - 1, MAX_BUFFERS - 1);
	CHECK(ResizeLRU(&G, MAX_BUFFERS - 1 + NEW_LRU_ENTRIES) == MAX_BUFFERS, "599 + 5 must stop at %d", MAX_BUFFERS);
	consistent("599 + 5", MAX_BUFFERS);

	/* without a block size there is nothing to do */
	setup();
	G.glob_lrudata.reserved_blksize = 0;
	CHECK(ResizeLRU(&G, 50) == 0 && nallocs == 0 && G.glob_lrudata.poolsize == 0 && !G.glob_lrudata.LRUarray, "reserved_blksize 0 must return 0 and allocate nothing");
	CHECK(ResizeLRU(&G, 1000) == 0 && nallocs == 0, "reserved_blksize 0 must return 0 for any request");

	/* memory mask warning: once per grow call */
	setup();
	ENV.de_Mask = 0;
	expect_resize("masked grow", 30, 30);
	CHECK(nmaskwarn == 1, "memory mask warning given %d times, expected once", nmaskwarn);
}

static void test_shrink(void)
{
	setup();
	expect_resize("grow 100", 100, 100);
	expect_resize("same size", 100, 100);
	expect_resize("shrink to 40", 40, 40);
	CHECK(livecount == 41, "shrink to 40: %d live blocks, expected 41", livecount);
	expect_resize("shrink to 3 -> MIN", 3, MIN_BUFFERS);
	CHECK(livecount == MIN_BUFFERS + 1, "shrink to MIN: %d live blocks", livecount);
	expect_resize("grow again", 25, 25);
	expect_resize("shrink to 24", 24, 24);

	/* queue entries are evicted when the pool is empty: 50 blocks, 5 in the pool */
	setup();
	expect_resize("grow 50", 50, 50);
	use_blocks(45, 0, 1, 0);
	expect_resize("shrink 50 -> 20 over pool and queue", 20, 20);
	CHECK(nflush == 25, "%d blocks flushed, expected 25 (the pool entries need no flush)", nflush);
	expect_resize("shrink -> MIN", 1, MIN_BUFFERS);

	/* locked, unlinked and pending blocks are not evicted: shrinking stops early */
	setup();
	expect_resize("grow 40", 40, 40);
	use_blocks(10, 1, 1, 0);		/* 10 locked */
	use_blocks(5, 0, 0, 0);			/* 5 not linked into a volume list */
	use_blocks(5, 0, 1, 1);			/* 5 with a pending dirty state */
	use_blocks(5, 0, 1, 0);			/* 5 evictable */
	/* 15 entries stay in the pool, 25 are in the queue, 5 of those can be evicted */
	expect_resize("shrink 40 -> 10 stops early", 10, 20);
	CHECK(nflush == 5, "%d blocks flushed, expected the 5 evictable ones", nflush);
	{
		int bad = 0;
		CHECK(listlen(&G.glob_lrudata.LRUqueue, G.glob_lrudata.LRUarray, 20, &bad) == 20 && listlen(&G.glob_lrudata.LRUpool, G.glob_lrudata.LRUarray, 20, &bad) == 0, "the 20 blocked blocks must all stay in the queue");
	}
	expect_resize("nothing more can go", 10, 20);
	CHECK(nflush == 5, "blocked blocks must not be flushed (%d flushes)", nflush);

	/* everything locked and no pool entry left: nothing can go */
	setup();
	expect_resize("grow 30", 30, 30);
	use_blocks(30, 1, 1, 0);
	expect_resize("all locked", 12, 30);
	CHECK(nflush == 0, "locked blocks flushed");
}

static void test_failures(void)
{
	LONG from, i;
	struct lru_cachedblock **oldarr;
	struct lru_cachedblock *copy[64];
	int allocs_needed;

	/* grow 20 -> 40 with the Nth allocation failing, N = 1 (the array) .. 21 (last entry) .. 22 (none) */
	for (from = 0; from <= 20; from += 20)
	{
		for (i = 1; i <= (40 - from) + 2; i++)
		{
			char name[80];
			int qlen, plen, bad = 0;

			setup();
			if (from)
			{
				expect_resize("prepare", from, from);
				use_blocks(from / 2, 0, 1, 0);
			}
			oldarr = G.glob_lrudata.LRUarray;
			memcpy(copy, oldarr ? oldarr : copy, from * sizeof copy[0]);
			qlen = listlen(&G.glob_lrudata.LRUqueue, G.glob_lrudata.LRUarray, from, &bad);
			plen = listlen(&G.glob_lrudata.LRUpool, G.glob_lrudata.LRUarray, from, &bad);
			allocs_needed = 1 + (40 - from);
			nallocs = 0;
			fail_at = i;
			sprintf(name, "grow %ld->40, allocation %ld fails", (long)from, (long)i);
			if (i <= allocs_needed)
			{
				LONG r = ResizeLRU(&G, 40);
				CHECK(r == 0, "%s: returned %ld, expected 0", name, (long)r);
				CHECK(G.glob_lrudata.LRUarray == oldarr, "%s: array replaced", name);
				CHECK(!from || memcmp(copy, oldarr, from * sizeof copy[0]) == 0, "%s: array contents changed", name);
				CHECK(listlen(&G.glob_lrudata.LRUqueue, G.glob_lrudata.LRUarray, from, &bad) == qlen && listlen(&G.glob_lrudata.LRUpool, G.glob_lrudata.LRUarray, from, &bad) == plen && !bad, "%s: lists changed", name);
				consistent(name, from);
				/* the pool still works after the failure */
				fail_at = 0;
				CHECK(ResizeLRU(&G, 40) == 40, "%s: retry failed", name);
				consistent("retry after failure", 40);
			}
			else
			{
				LONG r = ResizeLRU(&G, 40);
				CHECK(r == 40, "%s: nothing fails, returned %ld", name, (long)r);
				consistent(name, 40);
			}
		}
	}

	/* failing grow at the limit: the clamped request is what is allocated */
	setup();
	expect_resize("prepare 590", 590, 590);
	nallocs = 0;
	fail_at = 6;				/* array, then the 5th new entry of 10 */
	CHECK(ResizeLRU(&G, 100000) == 0, "grow 590 -> MAX with a failure must return 0");
	consistent("failed grow at the limit", 590);
	fail_at = 0;
	expect_resize("retry up to MAX", 100000, MAX_BUFFERS);
}

int main(void)
{
	test_clamps();
	test_shrink();
	test_failures();

	if (fails) { printf("  %d check(s) failed\n", fails); return 1; }
	printf("  PASS: ResizeLRU clamps to MIN/MAX_BUFFERS, reports the size, stays consistent on shrink and failed grow\n");
	return 0;
}
