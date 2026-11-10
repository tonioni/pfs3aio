/* LIVE @src lru.c @func EvictLRU @func AllocLRU
 *
 * Proof of the AllocLRU/EvictLRU fixes. Lifts the checkout's real EvictLRU and
 * AllocLRU (ResizeLRU, the list primitives and FlushBlock are mocks that
 * follow the real contracts) and asserts CORRECT behaviour:
 *
 *  1. A block just handed out by AllocLRU is still unlocked and not linked into
 *     a volume list. A second AllocLRU (pool empty, all other blocks locked)
 *     must not evict it: FlushBlock would MinRemove it through NULL links, and
 *     it would be handed out twice. It must grow the cache instead.
 *  2. Normal eviction of an unlocked, linked block still works.
 *  3. With the cache at MAX_BUFFERS and every block locked (a packet that
 *     touches more blocks than that, such as freeing a large file) AllocLRU
 *     must still grow the cache: MAX_BUFFERS limits the configured size, not
 *     what a packet needs.
 *  4. When the cache cannot grow any more (memory is exhausted) AllocLRU must
 *     give up and return NULL instead of looping forever.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef uint8_t UBYTE; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAXB 600
#define NEW_LRU_ENTRIES 5

struct N  { struct N *next, *prev; };
struct ML { struct N *mlh_Head, *mlh_Tail, *mlh_TailPred; };         /* Exec list layout */

struct cachedblock {
    struct cachedblock *next, *prev;                     /* volume list links */
    ULONG used, blocknr, oldblocknr;
    UBYTE changeflag;
    UBYTE data[4];
};
struct lru_cachedblock { struct lru_cachedblock *next, *prev; struct cachedblock cblk; };

typedef struct globaldata {
    struct {
        struct ML LRUqueue, LRUpool;
        ULONG poolsize;
        struct lru_cachedblock **LRUarray;
        UWORD reserved_blksize;
    } glob_lrudata;
    ULONG locknr;
} globaldata;

#define DB(x)
#define ENTER(x)
#define CBLK_DIRTY_PENDING 2
#define RESCLUSTER 1
#define AFS_ERROR_LRU_UPDATE_FAIL "update"
#define AFS_ERROR_OUT_OF_BUFFERS "buffers"
#define ISLOCKED(blk) ((blk)->used == g->locknr)
#define MinRemove(n) mock_remove((struct N *)(n))
#define MinAddHead(l, n) mock_addhead((struct ML *)(l), (struct N *)(n))
#define HeadOf(l) ((void *)((l)->mlh_Head))
#define IsMinListEmpty(x) (((x)->mlh_TailPred) == (struct N *)(x))
#define NormalErrorMsg(a, b, c) (noutofbuffers++)

static int noutofbuffers, nflushnull, nresize;
static ULONG memmax;                                     /* buffers memory allows, 0 = no limit */

static void ml_init(struct ML *l) { l->mlh_Head = (struct N *)&l->mlh_Tail; l->mlh_Tail = NULL; l->mlh_TailPred = (struct N *)l; }
static void mock_addhead(struct ML *l, struct N *n)
{
    n->next = l->mlh_Head; n->prev = (struct N *)l;
    l->mlh_Head->prev = n; l->mlh_Head = n;
}
static void mock_remove(struct N *n) { n->prev->next = n->next; n->next->prev = n->prev; }

static void FlushBlock(struct cachedblock *b, globaldata *g)
{
    if (!b->next) nflushnull++;                          /* real MinRemove would write via NULL */
    memset(b, 0, sizeof(*b));
}
static void UpdateDatestamp(struct cachedblock *b, globaldata *g) {}
static ULONG RawWrite(UBYTE *d, ULONG n, ULONG blk, globaldata *g) { return 0; }
static void ResToBeFreed(ULONG blk, globaldata *g) {}
static long ErrorMsg(const char *m, void *a, globaldata *g) { return 0; }

/* SetLRUSize contract: grows by the requested amount (clamped to limit) and
 * returns the resulting buffer count, so an unchanged count means "no growth".
 * Growing is all or nothing and returns 0 when memory runs out.
 */
static LONG SetLRUSize(globaldata *g, LONG requested, LONG limit)
{
    nresize++;
    if (requested > limit) requested = limit;
    if (memmax && (ULONG)requested > memmax) return 0;
    while ((LONG)g->glob_lrudata.poolsize < requested) {
        struct lru_cachedblock *n = calloc(1, sizeof(*n));
        mock_addhead(&g->glob_lrudata.LRUpool, (struct N *)n);
        g->glob_lrudata.poolsize++;
    }
    return g->glob_lrudata.poolsize;
}

/* A tree that still has the cap calls ResizeLRU from AllocLRU; it is the same
 * request limited to MAXB, so the test fails there instead of not building.
 */
#define ResizeLRU(g, r) SetLRUSize((g), (r), MAXB)

#include "gen/EvictLRU.c"                    /* the REAL functions from lru.c */
#include "gen/AllocLRU.c"

static struct lru_cachedblock *mk(globaldata *g, int locked, int linked)
{
    struct lru_cachedblock *b = calloc(1, sizeof(*b));
    b->cblk.used = locked ? g->locknr : 0;
    b->cblk.next = linked ? &b->cblk : NULL;
    b->cblk.blocknr = 100;
    mock_addhead(&g->glob_lrudata.LRUqueue, (struct N *)b);
    return b;
}
static void setup(globaldata *g, int poolsize)
{
    static struct lru_cachedblock *dummy[1];
    memset(g, 0, sizeof(*g));
    ml_init(&g->glob_lrudata.LRUqueue);
    ml_init(&g->glob_lrudata.LRUpool);
    g->glob_lrudata.LRUarray = dummy;
    g->glob_lrudata.poolsize = poolsize;
    g->locknr = 1;
    noutofbuffers = nflushnull = nresize = 0;
    memmax = 0;
}
static void onalarm(int s)
{
    static const char m[] = "  FAIL: AllocLRU does not terminate\n";
    (void)s; fflush(stdout); write(1, m, sizeof(m) - 1); _exit(1);
}

int main(void)
{
    globaldata g;
    struct lru_cachedblock *a, *d;
    struct cachedblock *r;
    int bad = 0;

    signal(SIGALRM, onalarm);
    alarm(3);

    /* 1: queue head..tail = A (fresh, unlocked, unlinked), B, C (locked) */
    setup(&g, 3);
    mk(&g, 1, 1); mk(&g, 1, 1); a = mk(&g, 0, 0);
    r = AllocLRU(&g);
    printf("  fresh block busy: ret=%s flush-of-unlinked=%d resizes=%d poolsize=%u\n",
        r == &a->cblk ? "SAME block (handed out twice)" : r ? "other block" : "NULL",
        nflushnull, nresize, g.glob_lrudata.poolsize);
    if (nflushnull || !r || r == &a->cblk || g.glob_lrudata.poolsize != 8 || noutofbuffers) bad = 1;

    /* 2: an unlocked linked block is still evicted, without growing */
    setup(&g, 2);
    mk(&g, 1, 1); d = mk(&g, 0, 1);
    memset(&d->cblk.data, 0, sizeof(d->cblk.data));
    r = AllocLRU(&g);
    printf("  normal eviction: ret=%s resizes=%d\n", r == &d->cblk ? "victim" : r ? "other block" : "NULL", nresize);
    if (r != &d->cblk || nflushnull || nresize || noutofbuffers) bad = 1;

    /* 3: cache at MAX_BUFFERS, everything locked: it must grow past it */
    setup(&g, MAXB);
    mk(&g, 1, 1); mk(&g, 1, 1);
    r = AllocLRU(&g);
    printf("  cache at MAX_BUFFERS, all locked: ret=%s poolsize=%u error messages=%d\n",
        r ? "block" : "NULL", g.glob_lrudata.poolsize, noutofbuffers);
    if (!r || g.glob_lrudata.poolsize != MAXB + NEW_LRU_ENTRIES || noutofbuffers) bad = 1;

    /* 4: memory exhausted, everything locked: must fail, not loop */
    setup(&g, MAXB);
    memmax = MAXB;
    mk(&g, 1, 1); mk(&g, 1, 1);
    r = AllocLRU(&g);
    printf("  cache cannot grow: ret=%s resizes=%d error messages=%d\n", r ? "block" : "NULL", nresize, noutofbuffers);
    if (r || noutofbuffers < 1 || nresize > 8) bad = 1;

    if (bad) { printf("  FAIL\n"); return 1; }
    printf("  PASS\n");
    return 0;
}
