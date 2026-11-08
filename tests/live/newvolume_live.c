/* LIVE @src volume.c @func CalculateBlockSize @func SwitchBlockSize @func NewVolume
 *
 * Lifts the checkout's real NewVolume, SwitchBlockSize and CalculateBlockSize.
 * GetCurrentRoot (the probe), the volume insert/remove sequences, InitLRU, the
 * data cache allocator, the dos list lock and the allocator are mocks that
 * record what happened (an event log, plus the data cache and block size they
 * saw when they were called).
 *
 * SwitchBlockSize
 *  - switches the logical block size and resizes the data cache for it (the
 *    cache is (re)initialized with the NEW size already in force) -> TRUE;
 *  - if the new data cache cannot be allocated the old geometry is restored AND
 *    the data cache is rebuilt for it -> FALSE (also if that rebuild fails).
 *
 * NewVolume
 *  - on entry only the CLEAN data cache slots are dropped (blocknr = 0); dirty
 *    slots keep their block number, they hold user data of the old volume that
 *    the removal still has to write; slots beyond dc.size are not touched, a
 *    missing cache (ref == NULL) is safe; the removal of the old volume sees the
 *    dirty slots intact and the OLD block size;
 *  - a disk with another block size than the current one makes NewVolume call
 *    SwitchBlockSize, after the old volume is gone and before InitLRU/insert;
 *    equal block size: SwitchBlockSize (and so the data cache) is not touched;
 *  - when SwitchBlockSize fails the volume is NOT inserted (no InitLRU, no
 *    DiskInsertSequence), the probed rootblock is freed, the block size is back
 *    at the old value, the disk is reported as not-DOS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef int BOOL; typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define ACCESS_DETECT 1             /* keeps the access-mode block of CalculateBlockSize out */
#define ENTER(x)
#define EXIT(x)
#define ID_NOT_REALLY_DOS 0x4E444F53L
#define LDB_VOLUMES 3
#define LDB_WRITE 1
#define LDF_VOLUMES (1L << LDB_VOLUMES)
#define LDF_WRITE (1L << LDB_WRITE)

struct reftable { ULONG blocknr; UBYTE dirty; UBYTE pad; };
struct diskcache { struct reftable *ref; UBYTE *data; UWORD size, mask, roving; };
struct rootblock { UWORD reserved_blksize; };
struct volumedata { struct rootblock *rootblk; BOOL metadata_error; };
struct dosenvec { ULONG de_SizeBlock, de_SectorPerBlock; };

typedef struct globaldata
{
	struct dosenvec *dosenvec;
	struct diskcache dc;
	struct volumedata *currentvolume;
	BYTE softprotect;
	ULONG protectkey;
	ULONG disktype;
	BOOL dirty;
	ULONG blocksize, blocksize_phys;
	UWORD blockshift, blocklogshift;
	ULONG directsize;
} globaldata;

#define BLOCKSIZE (g->blocksize)
#define FreeBufmem(mem, g) FreeBufmemMock(mem, g)

/* ---- event log ------------------------------------------------------ */
static char evlog[64];
static void ev(char c) { size_t n = strlen(evlog); if (n < sizeof evlog - 1) evlog[n] = c; }

#define NSLOT 5
static ULONG snapshot[NSLOT];
static void snap(ULONG *dst, globaldata *g)
{
	int i;
	for (i = 0; i < NSLOT; i++) dst[i] = g->dc.ref ? g->dc.ref[i].blocknr : 0xdead;
}

/* ---- knobs ---------------------------------------------------------- */
static BOOL k_changed, k_lock_ok, k_found, k_same, k_remove_keeps, k_lru_ok, k_ins_ok;
static ULONG k_newbs;
static int k_initdc_fail;            /* bit n set: the (n+1)th InitDataCache call fails */

/* ---- state seen by mocks -------------------------------------------- */
static int ninitdc, nlive_rb, nfree_bad, ninputev, last_inserted;
static ULONG initdc_bs[4], remove_bs, lru_bs, lru_arg, ins_bs;
static ULONG snap_probe[NSLOT], snap_remove[NSLOT];
static struct rootblock rootbuf = { 1024 };
static struct volumedata newvol, oldvol;
static struct rootblock *ins_rb;

static void SetPartitionLimits(globaldata *g) { (void)g; }

#include "gen/CalculateBlockSize.c"

static BOOL InitDataCache(globaldata *g)
{
	if (ninitdc < 4) initdc_bs[ninitdc] = g->blocksize;
	ev('D');
	return !(k_initdc_fail & (1 << ninitdc++));
}
/* what the real FlushDataCache does: forget every slot, dirty or not */
static void FlushDataCache(globaldata *g)
{
	int i;
	for (i = 0; i < g->dc.size; i++) g->dc.ref[i].blocknr = 0;
}
static void FreeBufmemMock(void *p, globaldata *g) { ev('F'); if (p == &rootbuf && nlive_rb) nlive_rb--; else nfree_bad++; }
static BOOL UpdateChangeCount(globaldata *g) { return k_changed; }
static void *AttemptLockDosList(ULONG flags) { ev('K'); return k_lock_ok ? (void *)1 : NULL; }
static void UnLockDosList(ULONG flags) { ev('U'); }
static void UpdateAndMotorOff(globaldata *g) { ev('M'); }
static void CreateInputEvent(BOOL inserted, globaldata *g) { ev('E'); ninputev++; last_inserted = inserted; }
static BOOL GetCurrentRoot(struct rootblock **rb, ULONG *newbs, globaldata *g)
{
	ev('P');
	snap(snap_probe, g);
	*rb = NULL;
	if (k_found) { *rb = &rootbuf; nlive_rb = 1; *newbs = k_newbs; }
	return k_found;
}
static BOOL SameDisk(struct rootblock *a, struct rootblock *b) { return k_same; }
static void DiskRemoveSequence(globaldata *g)
{
	ev('R');
	remove_bs = g->blocksize;
	snap(snap_remove, g);
	if (!k_remove_keeps)
		g->currentvolume = NULL;
}
static BOOL InitLRU(globaldata *g, UWORD resblksize) { ev('L'); lru_bs = g->blocksize; lru_arg = resblksize; return k_lru_ok; }
static BOOL DiskInsertSequence(struct rootblock *rb, globaldata *g)
{
	ev('I');
	ins_bs = g->blocksize; ins_rb = rb;
	if (!k_ins_ok) return FALSE;
	g->currentvolume = &newvol;
	nlive_rb = 0;                     /* the volume owns the rootblock now */
	return TRUE;
}

#include "gen/SwitchBlockSize.c"            /* the REAL functions from ../../volume.c */
#include "gen/NewVolume.c"

/* ---- harness --------------------------------------------------------- */
static int failures;
static void on_segv(int sig)
{
	static const char m[] = "  FAIL: crash: data cache or other state dereferenced when absent\n";
	(void)sig;
	write(1, m, sizeof m - 1);
	_exit(1);
}
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct dosenvec env = { 128, 2 };
static struct reftable ref[NSLOT];
static globaldata g;

static void reset(ULONG bs, int have_old)
{
	memset(&g, 0, sizeof g);
	g.dosenvec = &env;
	CalculateBlockSize(&g, 0, bs);
	/* clean 10, dirty 20, clean 30, dirty 40, and a clean slot outside dc.size */
	memset(ref, 0, sizeof ref);
	ref[0].blocknr = 10; ref[1].blocknr = 20; ref[1].dirty = 1;
	ref[2].blocknr = 30; ref[3].blocknr = 40; ref[3].dirty = 1;
	ref[4].blocknr = 99;
	g.dc.ref = ref; g.dc.size = 4;
	g.currentvolume = have_old ? &oldvol : NULL;
	memset(evlog, 0, sizeof evlog);
	ninitdc = ninputev = nfree_bad = 0; nlive_rb = 0;
	remove_bs = lru_bs = lru_arg = ins_bs = 0; ins_rb = NULL;
	memset(snap_probe, 0, sizeof snap_probe); memset(snap_remove, 0, sizeof snap_remove);
	k_changed = TRUE; k_lock_ok = TRUE; k_found = FALSE; k_same = FALSE; k_remove_keeps = FALSE;
	k_lru_ok = TRUE; k_ins_ok = TRUE; k_newbs = bs; k_initdc_fail = 0;
	newvol.metadata_error = 0; oldvol.metadata_error = 0;
}

static int dc_is(ULONG a, ULONG b, ULONG c, ULONG d, ULONG e)
{
	return ref[0].blocknr == a && ref[1].blocknr == b && ref[2].blocknr == c &&
		ref[3].blocknr == d && ref[4].blocknr == e;
}
static int snap_is(const ULONG *s, ULONG a, ULONG b, ULONG c, ULONG d, ULONG e)
{
	return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e;
}

static void say(const char *what) { printf("  %-46s log=%-8s bs=%u\n", what, evlog, (unsigned)g.blocksize); }

int main(void)
{
	BOOL r;

	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGSEGV, on_segv);

	/* --- SwitchBlockSize -------------------------------------------------- */
	reset(1024, 0);
	r = SwitchBlockSize(512, 1024, &g);
	say("switch 1024 -> 512, data cache ok");
	check(r == TRUE && g.blocksize == 512 && g.blockshift == 9, "success: TRUE and the new size kept");
	check(ninitdc == 1 && initdc_bs[0] == 512, "data cache allocated once, for the new size");

	reset(512, 0);
	r = SwitchBlockSize(2048, 512, &g);
	say("switch 512 -> 2048");
	check(r == TRUE && g.blocksize == 2048 && g.blockshift == 11 && g.directsize == (16 * 1024 >> 11), "bigger size, derived fields follow");

	reset(1024, 0);
	k_initdc_fail = 1;                       /* the first (new size) allocation fails */
	r = SwitchBlockSize(512, 1024, &g);
	say("switch fails: data cache for new size");
	check(r == FALSE, "FALSE");
	check(g.blocksize == 1024 && g.blockshift == 10 && g.blocklogshift == 1 && g.directsize == (16 * 1024 >> 10),
		"geometry restored to the old size");
	check(ninitdc == 2 && initdc_bs[0] == 512 && initdc_bs[1] == 1024, "data cache rebuilt for the old size");

	reset(1024, 0);
	k_initdc_fail = 3;                       /* the rebuild fails as well */
	r = SwitchBlockSize(2048, 1024, &g);
	check(r == FALSE && g.blocksize == 1024 && ninitdc == 2, "failed rebuild: still FALSE, old size");

	/* --- NewVolume: data cache on entry --------------------------------- */
	reset(1024, 0);
	k_changed = FALSE;
	NewVolume(FALSE, &g);
	say("no change, not forced: nothing happens");
	check(evlog[0] == 0 && dc_is(10, 20, 30, 40, 99), "returns before anything is touched");

	reset(1024, 0);
	k_lock_ok = FALSE;
	NewVolume(TRUE, &g);
	say("dos list busy: nothing happens");
	check(strcmp(evlog, "K") == 0 && dc_is(10, 20, 30, 40, 99), "data cache untouched");

	reset(1024, 0);
	NewVolume(TRUE, &g);
	say("no disk: clean slots dropped, dirty kept");
	check(dc_is(0, 20, 0, 40, 99), "clean slots 0, dirty slots keep their block, slot outside dc.size untouched");
	check(snap_is(snap_probe, 0, 20, 0, 40, 99), "already done when the probe runs");
	check(strcmp(evlog, "KPUM") == 0, "event order");

	reset(1024, 1);
	NewVolume(TRUE, &g);
	say("disk removed: removal sees the dirty data");
	check(strcmp(evlog, "KPRUM") == 0, "event order");
	check(snap_is(snap_remove, 0, 20, 0, 40, 99), "DiskRemoveSequence finds the dirty slots intact");
	check(remove_bs == 1024, "and the old block size");

	reset(1024, 1);
	k_found = TRUE; k_same = TRUE;
	NewVolume(TRUE, &g);
	say("same disk: stale clean slots dropped too");
	check(dc_is(0, 20, 0, 40, 99) && strcmp(evlog, "KPFUM") == 0 && nlive_rb == 0, "probe result freed, cache invalidated");

	reset(1024, 0);
	g.dc.ref = NULL;
	NewVolume(TRUE, &g);
	say("no data cache allocated: no crash");
	check(strcmp(evlog, "KPUM") == 0, "completes");

	reset(1024, 0);
	g.dc.size = 0;
	NewVolume(TRUE, &g);
	check(dc_is(10, 20, 30, 40, 99), "empty cache (size 0): slots untouched");

	/* --- NewVolume: block size of the new disk -------------------------- */
	reset(1024, 0);
	k_found = TRUE; k_newbs = 1024;
	NewVolume(TRUE, &g);
	say("new disk, same block size");
	check(strcmp(evlog, "KPLIUM") == 0, "no SwitchBlockSize: probe, InitLRU, insert");
	check(ninitdc == 0 && g.blocksize == 1024, "data cache not re-allocated");
	check(lru_arg == 1024 && ins_rb == &rootbuf && g.currentvolume == &newvol && nlive_rb == 0, "volume inserted");

	reset(1024, 0);
	k_found = TRUE; k_newbs = 512;
	NewVolume(TRUE, &g);
	say("new disk, 512 byte blocks");
	check(strcmp(evlog, "KPDLIUM") == 0, "SwitchBlockSize before InitLRU and insert");
	check(ninitdc == 1 && initdc_bs[0] == 512 && g.blocksize == 512 && g.blockshift == 9, "switched to the disk's block size");
	check(lru_bs == 512 && ins_bs == 512, "InitLRU and the insert already see the new size");
	check(g.currentvolume == &newvol && ins_rb == &rootbuf, "volume inserted");

	reset(512, 0);
	k_found = TRUE; k_newbs = 2048;
	NewVolume(TRUE, &g);
	say("new disk, 2048 byte blocks");
	check(strcmp(evlog, "KPDLIUM") == 0 && g.blocksize == 2048 && ins_bs == 2048, "also upwards");

	reset(1024, 1);
	k_found = TRUE; k_newbs = 512;
	NewVolume(TRUE, &g);
	say("other disk: old volume removed first");
	check(strcmp(evlog, "KPRDLIUM") == 0, "removal, then switch, then InitLRU and insert");
	check(remove_bs == 1024 && snap_is(snap_remove, 0, 20, 0, 40, 99), "removal still ran with the old geometry and dirty data");
	check(g.blocksize == 512 && g.currentvolume == &newvol, "new volume at its own block size");

	reset(1024, 1);
	k_found = TRUE; k_newbs = 512; k_remove_keeps = TRUE;
	NewVolume(TRUE, &g);
	say("old volume cannot be removed yet");
	check(strcmp(evlog, "KPRFUM") == 0 && g.blocksize == 1024 && ninitdc == 0 && nlive_rb == 0,
		"block size left alone, rootblock freed");

	/* --- NewVolume: SwitchBlockSize fails ------------------------------- */
	reset(1024, 0);
	k_found = TRUE; k_newbs = 512; k_initdc_fail = 1;
	NewVolume(TRUE, &g);
	say("block size switch fails");
	check(strcmp(evlog, "KPDDFEUM") == 0, "no InitLRU, no insert; rootblock freed, input event");
	check(g.blocksize == 1024 && g.blockshift == 10 && ninitdc == 2 && initdc_bs[1] == 1024,
		"old block size and data cache restored");
	check(g.currentvolume == NULL && ins_rb == NULL && lru_arg == 0, "no volume inserted");
	check(nlive_rb == 0 && nfree_bad == 0, "probed rootblock freed exactly once");
	check(g.disktype == ID_NOT_REALLY_DOS && ninputev == 1 && last_inserted == TRUE, "reported as not-DOS");

	reset(1024, 1);
	k_found = TRUE; k_newbs = 2048; k_initdc_fail = 3;
	NewVolume(TRUE, &g);
	say("switch fails, restore fails too");
	check(strcmp(evlog, "KPRDDFEUM") == 0 && g.blocksize == 1024 && g.currentvolume == NULL && nlive_rb == 0,
		"still no insert, rootblock freed, old size");

	if (failures) { printf("  FAIL: %d check(s) wrong\n", failures); return 1; }
	printf("  PASS: NewVolume keeps dirty cache slots, switches the block size, SwitchBlockSize rolls back\n");
	return 0;
}
