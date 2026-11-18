/* LIVE @src update.c @func CommitPostRootChanges
 *
 * Proof that a successful primary root commit persists its deferred reserved
 * frees. The real helper is lifted from update.c and driven with a mock root
 * writer. It must apply reserved and user-space free-list commits first, then
 * issue a root-only cleanup write; without that write pfsdoctor reports the old
 * copy-on-write metadata locations as "not used but allocated".
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

static int sequence, reserved_commit_at, free_commit_at, root_write_at;
static int root_writes, fail_root_write;
static ULONG free_seen_by_write;

static void CommitReservedToBeFreed(globaldata *g)
{
    reserved_commit_at = ++sequence;
    g->currentvolume->rootblk->reserved_free += alloc_data.rtbf_index;
    alloc_data.rtbf_index = 0;
}

static void CommitFreeList(globaldata *g)
{
    (void)g;
    free_commit_at = ++sequence;
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

#include "gen/CommitPostRootChanges.c"       /* the REAL helper from ../../update.c */

static void Reset(void)
{
    sequence = reserved_commit_at = free_commit_at = root_write_at = 0;
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
    if (!CommitPostRootChanges(&g) || root_writes != 0) {
        printf("  FAIL: cleanup root written with no deferred reserved frees\n");
        return 1;
    }

    Reset();
    alloc_data.rtbf_index = 3;
    if (!CommitPostRootChanges(&g) || root_writes != 1 ||
        !(reserved_commit_at < free_commit_at && free_commit_at < root_write_at) ||
        free_seen_by_write != 103 || alloc_data.rtbf_index != 0) {
        printf("  FAIL: deferred frees were not committed before one cleanup root write\n");
        return 1;
    }

    Reset();
    alloc_data.rtbf_index = 2;
    fail_root_write = 1;
    if (CommitPostRootChanges(&g) || root_writes != 1 ||
        free_seen_by_write != 105 || alloc_data.rtbf_index != 0) {
        printf("  FAIL: cleanup write failure was not reported after applying safe frees\n");
        return 1;
    }

    printf("  PASS: deferred reserved frees are persisted by one post-commit root write\n");
    return 0;
}
