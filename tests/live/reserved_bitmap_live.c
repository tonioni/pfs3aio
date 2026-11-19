/* LIVE @src allocation.c @func InitAllocation @func AllocReservedBlock @func ReservedBit @func FreeReservedBlock
 * @func ApplyReservedToBeFreed @func UndoReservedToBeFreed
 * Exercise the real initializer and bitmap allocator with Amiga-width types.
 * Invalid metadata must leave allocation disabled, not merely report an error.
 * The queued reserved frees go into the bitmap before the root write and come
 * back out when it fails.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "live.h"
#include "pfs3disk.h"

#define VOID void
#define ROOTBLOCK 2
#define LONGS_PER_BMB 253
#define TBF_CACHE_SIZE 8
#define AFS_ERROR_ANODE_INIT 1
#define AFS_ERROR_DNV_ALLOC_INFO 2
#define ErrorMsg(...) (++errors)
#define FreeMem(p, size) free(p)
#define ENTER(...)
#define EXIT(...)
#define DB(...)

struct extension { struct { ULONG reserved_roving, rovingbit; } blk; };
struct volumedata {
    rootblock_t *rootblk;
    struct extension *rblkextension;
    ULONG numblocks, rescluster, bytesperblock;
    BOOL metadata_error, rootblockchangeflag;
};
typedef struct {
    rootblock_t *rootblock;
    struct volumedata *currentvolume;
    BOOL harddiskmode, supermode, dirty;
} globaldata;
static struct { UWORD indexperblock; } andata = { 250 };
static struct {
    ULONG clean_blocksfree, alloc_available, longsperbmb, no_bmb;
    ULONG bitmapstart, tobefreed[TBF_CACHE_SIZE * 2];
    ULONG tobefreed_index, tbf_resneed, res_roving, rovingbit, numreserved;
    ULONG *reservedtobefreed, rtbf_size, rtbf_index, res_alert;
    bitmapblock_t *res_bitmap;
} alloc_data;
static unsigned errors;

#include "gen/InitAllocation.c"
#include "gen/AllocReservedBlock.c"
#include "gen/ReservedBit.c"
#include "gen/FreeReservedBlock.c"
#include "gen/ApplyReservedToBeFreed.c"
#include "gen/UndoReservedToBeFreed.c"

static void setup(globaldata *g, struct volumedata *v, ULONG blocksize,
    ULONG clusters, ULONG reserved)
{
    memset(&alloc_data, 0, sizeof(alloc_data));
    memset(v, 0, sizeof(*v));
    memset(g, 0, sizeof(*g));
    v->rootblk = calloc(clusters, blocksize);
    assert(v->rootblk);
    v->numblocks = 0xffffffffU;
    v->bytesperblock = blocksize;
    v->rescluster = blocksize == 512 ? 2 : 1;
    rootblock_t *r = v->rootblk;
    r->reserved_blksize = blocksize * v->rescluster;
    r->firstreserved = ROOTBLOCK;
    r->lastreserved = ROOTBLOCK + reserved * v->rescluster - 1;
    r->rblkcluster = clusters;
    r->reserved_free = reserved;
    r->blocksfree = 10000;
    r->options = MODE_EXTROVING;
    g->rootblock = r;
    g->currentvolume = v;
    g->harddiskmode = TRUE;
    errors = 0;
}

static void reject(globaldata *g, struct volumedata *v)
{
    /* Simulate the bitmap left by a previous mount. */
    alloc_data.res_bitmap = (bitmapblock_t *)(v->rootblk + 1);
    alloc_data.numreserved = 4000;
    InitAllocation(v, g);
    assert(v->metadata_error && errors == 1);
    assert(!alloc_data.res_bitmap && !alloc_data.numreserved);
    assert(!AllocReservedBlock(g));
    ULONG before = v->rootblk->reserved_free;
    FreeReservedBlock(ROOTBLOCK, g);
    assert(v->rootblk->reserved_free == before);
    free(v->rootblk);
}

int main(void)
{
    globaldata g;
    struct volumedata v;
    assert(sizeof(rootblock_t) == 512 && sizeof(bitmapblock_t) == 12);

    /* A 1024-byte root has room for exactly 4000 reserved-block bits. */
    setup(&g, &v, 512, 2, 4001);
    reject(&g, &v);
    setup(&g, &v, 512, 1, 32); /* no room for a bitmap header */
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.rootblk->lastreserved = 1;
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.rootblk->firstreserved = 0;
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.numblocks = v.rootblk->lastreserved;
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.rootblk->lastreserved--;
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.rescluster = 0;
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.rootblk->reserved_free++;
    reject(&g, &v);
    setup(&g, &v, 4096, 521, (4096 * 521 - 524) / 4 * 32 + 1);
    reject(&g, &v);
    setup(&g, &v, 512, 2, 4000);
    v.rootblk->lastreserved = 0xfffffffeU;
    reject(&g, &v);

    for (ULONG bs = 512; bs <= 4096; bs *= 2) {
        ULONG clusters = bs == 512 ? 2 : 1;
        ULONG bits = (bs * clusters - 524) / 4 * 32;
        setup(&g, &v, bs, clusters, bits);
        InitAllocation(&v, &g);
        assert(!v.metadata_error && !errors);
        assert(alloc_data.numreserved == bits);
        alloc_data.res_bitmap->bitmap[bits / 32 - 1] = 1;
        ULONG block = AllocReservedBlock(&g);
        assert(block == v.rootblk->lastreserved - v.rescluster + 1);
        assert(v.rootblk->reserved_free == bits - 1);
        FreeReservedBlock(block, &g);
        assert(v.rootblk->reserved_free == bits);
        FreeReservedBlock(block, &g); /* no double free */
        assert(v.rootblk->reserved_free == bits);
        free(v.rootblk);
    }

    /* Partial final words and stale/legacy rover values remain supported. */
    for (unsigned legacy = 0; legacy != 2; ++legacy) {
        setup(&g, &v, 512, 2, 3999);
        struct extension ext = { { 0xffffffffU, 0 } };
        v.rblkextension = &ext;
        if (legacy)
            v.rootblk->options = 0;
        InitAllocation(&v, &g);
        assert(!v.metadata_error && !alloc_data.res_roving);
        alloc_data.res_bitmap->bitmap[124] = 3;
        assert(AllocReservedBlock(&g) == ROOTBLOCK + 3998 * 2);
        assert(!AllocReservedBlock(&g)); /* ignore padding bit */
        free(v.rootblk);
    }
    /* Queued frees: applied for the root write, undone when it fails. */
    {
        setup(&g, &v, 512, 2, 4000);
        InitAllocation(&v, &g);
        memset(alloc_data.res_bitmap->bitmap, 0xff, 4000 / 8);
        ULONG a = AllocReservedBlock(&g), b = AllocReservedBlock(&g);
        ULONG c = AllocReservedBlock(&g), d = AllocReservedBlock(&g);
        assert(a && b && c && d && a != b && b != c && c != d);
        FreeReservedBlock(c, &g); /* queued, but already free */
        ULONG queue[] = { a, b, a, 0, a + 1, c, d };
        ULONG nfree = v.rootblk->reserved_free, mask, *word;
        alloc_data.reservedtobefreed = queue;
        alloc_data.rtbf_index = 6;
        ApplyReservedToBeFreed(&g);
        assert(v.rootblk->reserved_free == nfree + 2);
        assert(queue[0] == a && queue[1] == b);
        assert(!queue[2] && !queue[3] && !queue[4] && !queue[5]);
        UndoReservedToBeFreed(&g);
        assert(v.rootblk->reserved_free == nfree && alloc_data.rtbf_index == 6);
        word = ReservedBit(a, &mask, &g);
        assert(word && !(*word & mask));
        word = ReservedBit(b, &mask, &g);
        assert(word && !(*word & mask));
        word = ReservedBit(c, &mask, &g);
        assert(word && (*word & mask)); /* stays free */
        alloc_data.rtbf_index = 7; /* d queued before the retry */
        ApplyReservedToBeFreed(&g);
        assert(v.rootblk->reserved_free == nfree + 3);
        ULONG freed[] = { a, b, d };
        for (int i = 0; i < 3; i++) {
            word = ReservedBit(freed[i], &mask, &g);
            assert(word && (*word & mask));
        }
        alloc_data.reservedtobefreed = NULL;
        free(v.rootblk);
    }
    puts("  PASS: reserved bitmap capacity, geometry, allocation, rover bounds and queued frees");
    return 0;
}
