/* LIVE @src init.c @func DoPostponed
 *
 * The postponed-operation replay at mount time (DoPostponed) works on a
 * "blocks done" counter read from disk (rootblock extension, tobedone).
 * A corrupt counter must neither wrap the free-block counters nor stop the
 * volume from mounting with a trustworthy state. Lifts the real DoPostponed
 * (init.c); everything it calls (GetAnodeChain, FreeBlocksAC,
 * DetachAnodeChain, FreeAnodesInChain) is a mock that records its calls.
 * Proven:
 *   - PP_FREEBLOCKS_KEEP with argument3 > clean_blocksfree (boundary: ==
 *     is accepted, +1 and 0xFFFFFFFF are not): the volume is flagged
 *     metadata_error + dirty, clean_blocksfree/alloc_available are left
 *     alone, the operation is not cleared and no anodechain is taken or
 *     freed (the check comes before GetAnodeChain);
 *   - a valid value decrements clean_blocksfree and alloc_available, calls
 *     FreeBlocksAC(keepanodes), detaches the chain, clears the operation;
 *   - alloc_available saturates at 0 when argument3 >= alloc_available
 *     (== , +1, far above) and is exact just below that;
 *   - failure paths (no anodechain, FreeBlocksAC fails) flag the volume and
 *     never leak the attached chain; the other operations keep working.
 *
 * The early checks in Initialize (short DosEnvec, bad SizeBlock, MaxTransfer
 * default, allocator pointers, AddToFSResource guard) are in
 * initialize_live.c.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t ULONG; typedef uint8_t UBYTE; typedef int BOOL;
#define TRUE 1
#define FALSE 0

#define PP_FREEBLOCKS_FREE 1
#define PP_FREEBLOCKS_KEEP 2
#define PP_FREEANODECHAIN 3

struct postponed_op {
    ULONG operation_id;
    ULONG argument1;
    ULONG argument2;
    ULONG argument3;
};
struct crootblockextension { struct { struct postponed_op tobedone; } blk; };
struct volumedata { struct crootblockextension *rblkextension; BOOL metadata_error; };
struct anodechain { int id; };
enum freeblocktype { keepanodes, freeanodes };

typedef struct globaldata {
    struct { ULONG clean_blocksfree, alloc_available; } glob_allocdata;
    BOOL dirty;
} globaldata;
#define alloc_data (g->glob_allocdata)

/* mocks: record every call */
static struct anodechain chain_obj = { 1 };
static int get_calls, get_fail, detach_calls, free_calls, free_fail, fac_calls, fac_fail;
static ULONG get_arg, free_size, fac_arg;
static struct anodechain *free_chain, *detach_chain;
static enum freeblocktype free_type;

static struct anodechain *GetAnodeChain(ULONG anodenr, globaldata *g)
{
    get_calls++; get_arg = anodenr;
    return get_fail ? NULL : &chain_obj;
}
static BOOL FreeBlocksAC(struct anodechain *achain, ULONG size, enum freeblocktype t, globaldata *g)
{
    free_calls++; free_chain = achain; free_size = size; free_type = t;
    return !free_fail;
}
static void DetachAnodeChain(struct anodechain *chain, globaldata *g)
{
    detach_calls++; detach_chain = chain;
}
static BOOL FreeAnodesInChain(ULONG anodenr, globaldata *g)
{
    fac_calls++; fac_arg = anodenr;
    return !fac_fail;
}

#include "gen/DoPostponed.c"                 /* the REAL function from ../../init.c */

static int fails;
static void check(int ok, const char *what)
{
    if (!ok) { printf("  BAD   %s\n", what); fails++; }
}

static struct crootblockextension ext;
static struct volumedata vol;
static globaldata gdata;
static globaldata *g = &gdata;

static void setup(ULONG op, ULONG a1, ULONG a2, ULONG a3, ULONG clean, ULONG avail)
{
    memset(&ext, 0, sizeof ext);
    memset(&vol, 0, sizeof vol);
    memset(&gdata, 0, sizeof gdata);
    get_calls = get_fail = detach_calls = free_calls = free_fail = fac_calls = fac_fail = 0;
    free_chain = detach_chain = NULL;
    ext.blk.tobedone.operation_id = op;
    ext.blk.tobedone.argument1 = a1;
    ext.blk.tobedone.argument2 = a2;
    ext.blk.tobedone.argument3 = a3;
    vol.rblkextension = &ext;
    g->glob_allocdata.clean_blocksfree = clean;
    g->glob_allocdata.alloc_available = avail;
}

static int cleared(void)
{
    struct postponed_op *p = &ext.blk.tobedone;
    return !p->operation_id && !p->argument1 && !p->argument2 && !p->argument3;
}

/* the volume was flagged and nothing was touched: operation still pending */
static void expect_failed(const char *what, ULONG op, ULONG a1, ULONG a2, ULONG a3,
                          ULONG clean, ULONG avail)
{
    char m[160];
    struct postponed_op *p = &ext.blk.tobedone;

    sprintf(m, "%s: metadata_error not set", what);  check(vol.metadata_error == TRUE, m);
    sprintf(m, "%s: g->dirty not set", what);        check(g->dirty == TRUE, m);
    sprintf(m, "%s: clean_blocksfree changed (%u -> %u)", what, clean, alloc_data.clean_blocksfree);
    check(alloc_data.clean_blocksfree == clean, m);
    sprintf(m, "%s: alloc_available changed (%u -> %u)", what, avail, alloc_data.alloc_available);
    check(alloc_data.alloc_available == avail, m);
    sprintf(m, "%s: pending operation was cleared", what);
    check(p->operation_id == op && p->argument1 == a1 && p->argument2 == a2 && p->argument3 == a3, m);
    sprintf(m, "%s: anodechain leaked (get %d, detach %d)", what, get_calls, detach_calls);
    check(get_calls == detach_calls || (get_fail && detach_calls == 0), m);
}

static void keep_reject(const char *what, ULONG a3, ULONG clean, ULONG avail)
{
    char m[160];

    setup(PP_FREEBLOCKS_KEEP, 77, 9, a3, clean, avail);
    DoPostponed(&vol, g);
    expect_failed(what, PP_FREEBLOCKS_KEEP, 77, 9, a3, clean, avail);
    sprintf(m, "%s: chain taken / blocks freed despite the corrupt count", what);
    check(get_calls == 0 && free_calls == 0 && detach_calls == 0, m);
}

static void keep_ok(const char *what, ULONG a3, ULONG clean, ULONG avail,
                    ULONG want_clean, ULONG want_avail)
{
    char m[160];

    setup(PP_FREEBLOCKS_KEEP, 77, 9, a3, clean, avail);
    DoPostponed(&vol, g);
    sprintf(m, "%s: clean_blocksfree %u, want %u", what, alloc_data.clean_blocksfree, want_clean);
    check(alloc_data.clean_blocksfree == want_clean, m);
    sprintf(m, "%s: alloc_available %u, want %u", what, alloc_data.alloc_available, want_avail);
    check(alloc_data.alloc_available == want_avail, m);
    sprintf(m, "%s: rejected a valid count", what);
    check(!vol.metadata_error && !g->dirty, m);
    sprintf(m, "%s: FreeBlocksAC(keepanodes, argument2) not called once", what);
    check(free_calls == 1 && free_chain == &chain_obj && free_size == 9 && free_type == keepanodes, m);
    sprintf(m, "%s: GetAnodeChain(argument1) / Detach not once", what);
    check(get_calls == 1 && get_arg == 77 && detach_calls == 1 && detach_chain == &chain_obj, m);
    sprintf(m, "%s: operation not cleared", what);
    check(cleared(), m);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* corrupt "blocks done" count: must not wrap clean_blocksfree */
    keep_reject("KEEP a3=clean+1",   6, 5, 100);
    keep_reject("KEEP a3=0xFFFFFFFF", 0xFFFFFFFFu, 5, 100);
    keep_reject("KEEP a3>clean, clean=0", 1, 0, 100);
    keep_reject("KEEP a3=0x80000000", 0x80000000u, 0x7FFFFFFFu, 0x7FFFFFFFu);

    /* valid counts, incl. the boundary a3 == clean_blocksfree */
    keep_ok("KEEP a3==clean", 5, 5, 100, 0, 95);
    keep_ok("KEEP a3<clean",  3, 5, 100, 2, 97);
    keep_ok("KEEP a3=0",      0, 5, 100, 5, 100);

    /* alloc_available never wraps below 0 */
    keep_ok("KEEP a3==avail",   7, 50, 7, 43, 0);
    keep_ok("KEEP a3==avail+1", 8, 50, 7, 42, 0);
    keep_ok("KEEP a3>>avail",  40, 50, 7, 10, 0);
    keep_ok("KEEP a3==avail-1", 6, 50, 7, 44, 1);
    keep_ok("KEEP avail=0",     4, 50, 0, 46, 0);

    /* failure paths: flagged, chain released */
    setup(PP_FREEBLOCKS_KEEP, 77, 9, 3, 50, 50);
    get_fail = 1;
    DoPostponed(&vol, g);
    expect_failed("KEEP no anodechain", PP_FREEBLOCKS_KEEP, 77, 9, 3, 50, 50);
    check(free_calls == 0, "KEEP no anodechain: FreeBlocksAC called");

    setup(PP_FREEBLOCKS_KEEP, 77, 9, 3, 50, 50);
    free_fail = 1;
    DoPostponed(&vol, g);
    check(vol.metadata_error && g->dirty, "KEEP FreeBlocksAC fails: volume not flagged");
    check(get_calls == 1 && detach_calls == 1 && detach_chain == &chain_obj,
          "KEEP FreeBlocksAC fails: chain not detached");
    check(ext.blk.tobedone.operation_id == PP_FREEBLOCKS_KEEP, "KEEP FreeBlocksAC fails: operation cleared");

    /* the other operations */
    setup(PP_FREEBLOCKS_FREE, 77, 9, 3, 50, 50);
    DoPostponed(&vol, g);
    check(!vol.metadata_error && !g->dirty && cleared(), "FREE ok: flagged or not cleared");
    check(free_calls == 1 && free_type == freeanodes && free_size == 9 && free_chain == &chain_obj &&
          detach_calls == 1 && get_arg == 77, "FREE ok: wrong calls");
    check(alloc_data.clean_blocksfree == 50 && alloc_data.alloc_available == 50, "FREE ok: counters touched");

    setup(PP_FREEBLOCKS_FREE, 77, 9, 3, 50, 50);
    free_fail = 1;
    DoPostponed(&vol, g);
    check(vol.metadata_error && g->dirty && detach_calls == 1 &&
          ext.blk.tobedone.operation_id == PP_FREEBLOCKS_FREE, "FREE fails: not flagged / chain leaked");

    setup(PP_FREEBLOCKS_FREE, 77, 9, 3, 50, 50);
    get_fail = 1;
    DoPostponed(&vol, g);
    check(vol.metadata_error && g->dirty && free_calls == 0 && detach_calls == 0, "FREE no chain: not flagged");

    setup(PP_FREEANODECHAIN, 1234, 0, 0, 50, 50);
    DoPostponed(&vol, g);
    check(!vol.metadata_error && cleared() && fac_calls == 1 && fac_arg == 1234, "ANODECHAIN ok");
    setup(PP_FREEANODECHAIN, 1234, 0, 0, 50, 50);
    fac_fail = 1;
    DoPostponed(&vol, g);
    check(vol.metadata_error && g->dirty && ext.blk.tobedone.operation_id == PP_FREEANODECHAIN,
          "ANODECHAIN fails: not flagged");

    /* nothing pending / no extension */
    setup(0, 0, 0, 0, 50, 50);
    DoPostponed(&vol, g);
    check(!vol.metadata_error && !g->dirty && get_calls == 0 && free_calls == 0 && fac_calls == 0,
          "no pending operation: something happened");
    setup(PP_FREEBLOCKS_KEEP, 77, 9, 3, 50, 50);
    vol.rblkextension = NULL;
    DoPostponed(&vol, g);
    check(!vol.metadata_error && !g->dirty && get_calls == 0 && alloc_data.clean_blocksfree == 50,
          "no rootblock extension: something happened");

    printf("== init_checks (DoPostponed): %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
