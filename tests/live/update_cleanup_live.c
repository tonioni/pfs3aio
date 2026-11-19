/* LIVE @src update.c @func CommitRoot
 *
 * Proof that the root write, the commit point, carries the frees of the old
 * reserved locations it stops referencing. The real helper is lifted from
 * update.c and driven with a mock root writer. A second write for them could
 * be lost at a power cut, and pfsdoctor then reports those locations as
 * "not used but allocated". A failed write must take the frees back and keep
 * the queue for the next attempt.
 */
#include <stdio.h>
#include <stdint.h>
#include "live.h"


#define ROOTBLOCK 2

struct rootblock {
    ULONG rblkcluster;
    ULONG reserved_free;
};

struct volumedata {
    struct rootblock *rootblk;
};

typedef struct globaldata {
    struct volumedata *currentvolume;
} globaldata;

struct {
    ULONG rtbf_index;
} alloc_data;

static int sequence, apply_at, undo_at, root_write_at;
static int root_writes, fail_root_write;
static ULONG free_seen_by_write;

static void ApplyReservedToBeFreed(globaldata *g)
{
    apply_at = ++sequence;
    g->currentvolume->rootblk->reserved_free += alloc_data.rtbf_index;
}

static void UndoReservedToBeFreed(globaldata *g)
{
    undo_at = ++sequence;
    g->currentvolume->rootblk->reserved_free -= alloc_data.rtbf_index;
}

static ULONG RawWrite(UBYTE *data, ULONG blocks, ULONG blocknr, globaldata *g)
{
    (void)data;
    root_write_at = ++sequence;
    root_writes++;
    free_seen_by_write = g->currentvolume->rootblk->reserved_free;
    if (blocks != g->currentvolume->rootblk->rblkcluster || blocknr != ROOTBLOCK)
        return 1;
    return fail_root_write ? 1 : 0;
}

#include "gen/CommitRoot.c"  /* the REAL helper from ../../update.c */

static void Reset(void)
{
    sequence = apply_at = undo_at = root_write_at = 0;
    root_writes = fail_root_write = 0;
    free_seen_by_write = 0;
}

int main(void)
{
    struct rootblock root = { .rblkcluster = 6, .reserved_free = 100 };
    struct volumedata volume = { .rootblk = &root };
    globaldata g = { .currentvolume = &volume };

    Reset();
    alloc_data.rtbf_index = 0;
    if (!CommitRoot(&g) || root_writes != 1 || undo_at) {
        printf("  FAIL: root not written once with no queued reserved frees\n");
        return 1;
    }

    Reset();
    alloc_data.rtbf_index = 3;
    if (!CommitRoot(&g) || root_writes != 1 || undo_at ||
        !(apply_at && apply_at < root_write_at) ||
        free_seen_by_write != 103 || root.reserved_free != 103 ||
        alloc_data.rtbf_index != 0) {
        printf("  FAIL: queued reserved frees were not carried by the one root write\n");
        return 1;
    }

    Reset();
    alloc_data.rtbf_index = 2;
    fail_root_write = 1;
    if (CommitRoot(&g) || root_writes != 1 || free_seen_by_write != 105 ||
        !(root_write_at < undo_at) || root.reserved_free != 103 ||
        alloc_data.rtbf_index != 2) {
        printf("  FAIL: failed root write did not take the frees back and keep the queue\n");
        return 1;
    }

    printf("  PASS: the root write carries the queued reserved frees, a failed one takes them back\n");
    return 0;
}
