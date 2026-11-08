/* LIVE @src disk.c @func FreeDataCache @func InitDataCache
 *
 * The data cache is two blocks (reference table + data) allocated at mount.
 * If InitDataCache fails half way it used to leave dc.size = 32 with a NULL
 * or dangling ref/data: the next NewVolume loop `for (i < dc.size)` then
 * walked freed memory, and Quit() freed the same blocks again. Lifts the
 * real FreeDataCache and InitDataCache; AllocMemR (with a failure switch),
 * FreeVec (records every free and detects double / foreign frees) and
 * ErrorMsg are mocks. Proven:
 *   - FreeDataCache frees both blocks, NULLs both pointers and resets
 *     dc.size to 0; calling it again (Quit after a failed init) is harmless;
 *   - a successful InitDataCache allocates the reference table (sized for
 *     DATACACHELEN entries, MEMF_CLEAR) and the data blocks (DATACACHELEN *
 *     blocksize, de_BufMemType), sets size/mask/roving and returns TRUE;
 *     a cache from an earlier init is freed first (once, no leak);
 *   - if either allocation fails (ref, data, or both) it returns FALSE
 *     with BOTH blocks freed, ref/data NULL, size 0 and nothing leaked, and
 *     a later successful init works from that state;
 *   - the memory-mask warning (AFS_WARNING_MEMORY_MASK) stays non-fatal: the
 *     cache is kept and TRUE is returned; without a mask problem there is no
 *     message.
 * (The real AllocMemR retries until memory is found, so the NULL case is a
 * defensive path; the test drives it through the mock.)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uint8_t UBYTE; typedef int BOOL;
typedef uintptr_t IPTR; typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC (1UL << 0)
#define MEMF_CHIP (1UL << 1)
#define MEMF_CLEAR (1UL << 16)

#define DATACACHELEN 32
#define DATACACHEMASK (DATACACHELEN - 1)
struct reftable { ULONG blocknr; UBYTE dirty; UBYTE pad; };
struct diskcache { struct reftable *ref; UBYTE *data; UWORD size, mask, roving; };
struct dosenvec { ULONG de_BufMemType, de_Mask; };
typedef struct globaldata {
    struct diskcache dc; struct dosenvec *dosenvec; ULONG blocksize;
} globaldata;
#define BLOCKSIZE (g->blocksize)
static const UBYTE AFS_WARNING_MEMORY_MASK[] = "memory mask";

/* allocation mock: every block is tracked, frees are validated */
#define MAXBLK 16
static struct { void *p; ULONG size, flags; int live; } blk[MAXBLK];
static int nblk, alloc_calls, fail_mask, frees, null_frees, bad_frees, errmsgs;
static const void *last_err;

static void *AllocMemR(ULONG size, ULONG flags, globaldata *g)
{
    int idx = alloc_calls++;

    if (fail_mask & (1 << idx))
        return NULL;
    blk[nblk].p = calloc(1, size + 64);
    blk[nblk].size = size; blk[nblk].flags = flags; blk[nblk].live = 1;
    return blk[nblk++].p;
}
static void mock_free(void *p)
{
    int i;

    if (!p) { null_frees++; return; }
    for (i = 0; i < nblk; i++)
        if (blk[i].p == p) {
            if (!blk[i].live) bad_frees++;      /* double free */
            blk[i].live = 0; frees++;
            return;
        }
    bad_frees++;                                /* never allocated */
}
#define FreeVec(m) mock_free(m)

static long ErrorMsg(const UBYTE *msg, void *arg, globaldata *g)
{
    errmsgs++; last_err = msg;
    return 0;
}

#include "gen/FreeDataCache.c"               /* the REAL functions from ../../disk.c */
#include "gen/InitDataCache.c"

static int fails;
static void check(int ok, const char *what)
{
    if (!ok) { printf("  BAD   %s\n", what); fails++; }
}

static int live_blocks(void)
{
    int i, n = 0;

    for (i = 0; i < nblk; i++) n += blk[i].live;
    return n;
}

static struct dosenvec env;
static globaldata gdata;
static globaldata *g = &gdata;

static void reset(ULONG mask)
{
    memset(blk, 0, sizeof blk);
    nblk = alloc_calls = fail_mask = frees = null_frees = bad_frees = errmsgs = 0;
    last_err = NULL;
    memset(&gdata, 0, sizeof gdata);
    env.de_BufMemType = MEMF_CHIP | MEMF_PUBLIC;
    env.de_Mask = mask;
    gdata.dosenvec = &env;
    gdata.blocksize = 1024;
}

/* the state NewVolume relies on: no cache, nothing to walk */
static void expect_no_cache(const char *what)
{
    char m[120];

    sprintf(m, "%s: ref/data pointers not NULL", what);
    check(g->dc.ref == NULL && g->dc.data == NULL, m);
    sprintf(m, "%s: dc.size %u would let NewVolume walk freed memory", what, g->dc.size);
    check(g->dc.size == 0, m);
    sprintf(m, "%s: %d block(s) leaked", what, live_blocks());
    check(live_blocks() == 0, m);
    sprintf(m, "%s: double/foreign free (%d)", what, bad_frees);
    check(bad_frees == 0, m);
}

static void init_fail(const char *what, int mask_of_failing_calls)
{
    char m[120];
    BOOL rc;

    reset(0xFFFFFFFFu);
    fail_mask = mask_of_failing_calls;
    rc = InitDataCache(g);
    sprintf(m, "%s: returned TRUE", what);
    check(rc == FALSE, m);
    sprintf(m, "%s: the surviving block was not freed", what);
    check(frees == 2 - __builtin_popcount(mask_of_failing_calls & 3), m);
    expect_no_cache(what);
    sprintf(m, "%s: no warning expected on failure", what);
    check(errmsgs == 0, m);

    /* Quit()/a retry after the failure must work from this state */
    FreeDataCache(g);
    expect_no_cache("FreeDataCache after failed init");
    alloc_calls = 0; fail_mask = 0;
    rc = InitDataCache(g);
    sprintf(m, "%s: retry after failure failed", what);
    check(rc == TRUE && g->dc.ref && g->dc.data && g->dc.size == DATACACHELEN, m);
    FreeDataCache(g);
    expect_no_cache("retry cleanup");
}

int main(void)
{
    BOOL rc;
    void *oldref, *olddata;

    setvbuf(stdout, NULL, _IONBF, 0);

    /* FreeDataCache */
    reset(0xFFFFFFFFu);
    g->dc.ref = AllocMemR(100, 0, g);
    g->dc.data = AllocMemR(200, 0, g);
    g->dc.size = DATACACHELEN;
    FreeDataCache(g);
    check(frees == 2 && null_frees == 0, "FreeDataCache did not free both blocks");
    expect_no_cache("FreeDataCache");
    FreeDataCache(g);
    check(frees == 2 && bad_frees == 0 && g->dc.size == 0, "second FreeDataCache not harmless");

    /* success */
    reset(0xFFFFFFFFu);
    g->dc.size = 7; g->dc.roving = 9;           /* stale values must be replaced */
    rc = InitDataCache(g);
    check(rc == TRUE, "success: not TRUE");
    check(g->dc.ref && g->dc.data && alloc_calls == 2, "success: two blocks not allocated");
    check(g->dc.size == DATACACHELEN && g->dc.mask == DATACACHEMASK && g->dc.roving == 0,
          "success: size/mask/roving");
    check(blk[0].p == g->dc.ref && blk[0].size == DATACACHELEN * sizeof(struct reftable) &&
          (blk[0].flags & MEMF_CLEAR), "success: ref table size/flags");
    check(blk[1].p == g->dc.data && blk[1].size == DATACACHELEN * 1024 &&
          blk[1].flags == (MEMF_CHIP | MEMF_PUBLIC), "success: data size/memory type");
    check(errmsgs == 0 && bad_frees == 0 && live_blocks() == 2, "success: warning / frees / leak");

    /* re-init: the old cache is freed once, the new one is allocated */
    oldref = g->dc.ref; olddata = g->dc.data;
    g->blocksize = 2048;
    alloc_calls = 0;
    rc = InitDataCache(g);
    check(rc == TRUE && frees == 2 && bad_frees == 0 && live_blocks() == 2, "re-init: old cache not freed once");
    check(!blk[0].live && !blk[1].live && blk[0].p == oldref && blk[1].p == olddata, "re-init: wrong blocks freed");
    check(g->dc.ref == blk[2].p && g->dc.data == blk[3].p && blk[3].size == DATACACHELEN * 2048,
          "re-init: new blocks not in use / sized for the new blocksize");
    FreeDataCache(g);
    expect_no_cache("re-init cleanup");

    /* failures: first, second, both allocation(s) */
    init_fail("ref allocation fails", 1);
    init_fail("data allocation fails", 2);
    init_fail("both allocations fail", 3);

    /* failure on re-init: the old cache must not survive as a dangling pointer */
    reset(0xFFFFFFFFu);
    check(InitDataCache(g) == TRUE, "re-init failure: setup");
    alloc_calls = 0; fail_mask = 2;
    rc = InitDataCache(g);
    check(rc == FALSE, "re-init failure: returned TRUE");
    expect_no_cache("re-init failure");
    check(frees == 3, "re-init failure: old and surviving blocks not all freed once");

    /* memory mask: warning only */
    reset(0);                                   /* every address violates the mask */
    rc = InitDataCache(g);
    check((blk[1].p && ((IPTR)blk[1].p & 0xFFFFFFFFu) != 0), "test setup: data address low bits zero");
    check(rc == TRUE, "mask warning made InitDataCache fail");
    check(errmsgs == 1 && last_err == AFS_WARNING_MEMORY_MASK, "mask warning not issued once");
    check(g->dc.ref && g->dc.data && g->dc.size == DATACACHELEN && live_blocks() == 2 && frees == 0,
          "mask warning dropped the cache");
    FreeDataCache(g);
    expect_no_cache("mask warning cleanup");

    reset(0xFFFFFFFFu);                         /* nothing violates the mask */
    rc = InitDataCache(g);
    check(rc == TRUE && errmsgs == 0, "no mask problem: warning issued");
    FreeDataCache(g);

    printf("== datacache: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
