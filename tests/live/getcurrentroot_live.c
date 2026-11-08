/* LIVE @src volume.c @func CalculateBlockSize @func GetCurrentRoot @func CheckCurrentVolumeBack
 *
 * GetCurrentRoot is the side-effect-free wrapper around ProbeRootBlock: the
 * probe runs with the error requesters off, may lower the logical block size
 * to 512, and may leave an error-enforced softprotect behind - none of that may
 * leak into the volume that is still current (and maybe dirty). Lifts the
 * checkout's real GetCurrentRoot, CheckCurrentVolumeBack and
 * CalculateBlockSize; ProbeRootBlock is a mock that clobbers exactly those
 * things.
 *
 *  - g->blocksize AND the derived shift/phys/directsize fields are restored
 *    (through CalculateBlockSize), g->softprotect, g->protectkey are restored,
 *    g->ErrorMsg is _NormalErrorMsg again - for a found disk and for no disk;
 *  - *newbs receives the block size of the disk that was found (what the probe
 *    left in g), also when that is bigger than the restored one; newbs may be
 *    NULL; return value and rootblock are passed through;
 *  - CheckCurrentVolumeBack: with no current volume any readable disk is "back"
 *    and SameDisk is not consulted (volume is NULL: no crash); with a volume
 *    the disk must be the same one (SameDisk(volume->rootblk, rootblock));
 *    an unreadable disk is FALSE without calling SameDisk; the probed rootblock
 *    buffer is freed in every case.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef int BOOL; typedef void *APTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define ACCESS_DETECT 1             /* keeps the access-mode block of CalculateBlockSize out */
#define ENTER(x)

struct rootblock { ULONG id; };
struct dosenvec { ULONG de_SizeBlock, de_SectorPerBlock; };
struct volumedata { struct rootblock *rootblk; };

typedef struct globaldata
{
	struct dosenvec *dosenvec;
	struct volumedata *currentvolume;
	BYTE softprotect;
	ULONG protectkey;
	LONG (*ErrorMsg)(CONST_STRPTR, APTR, ULONG, struct globaldata *);
	ULONG blocksize, blocksize_phys;
	UWORD blockshift, blocklogshift;
	ULONG directsize;
} globaldata;

#define BLOCKSIZE (g->blocksize)
#define FreeBufmem(mem, g) FreeBufmemMock(mem, g)

static LONG NoErrorMsg(CONST_STRPTR m, APTR a, ULONG d, globaldata *g) { return 0; }
LONG _NormalErrorMsg(CONST_STRPTR m, APTR a, ULONG d, globaldata *g) { return 1; }
static void SetPartitionLimits(globaldata *g) { (void)g; }

/* the real CalculateBlockSize, behind a call log */
#define CalculateBlockSize CalculateBlockSize_real
#include "gen/CalculateBlockSize.c"
#undef CalculateBlockSize
static int ncbs;
static ULONG cbs_last_spb, cbs_last_bs;
void CalculateBlockSize(globaldata *g, ULONG spb, ULONG blocksize)
{
	ncbs++; cbs_last_spb = spb; cbs_last_bs = blocksize;
	CalculateBlockSize_real(g, spb, blocksize);
}

/* allocator: live buffer count */
static struct rootblock *live[4];
static int nlive, nfree_bad;
static void *AllocBufmemR(ULONG size, globaldata *g)
{
	struct rootblock *p = calloc(1, 512);
	live[nlive++] = p;
	return p;
}
static void FreeBufmemMock(void *p, globaldata *g)
{
	int i;
	for (i = 0; i < nlive; i++)
		if (live[i] == p) { live[i] = live[--nlive]; free(p); return; }
	nfree_bad++;
}

/* ProbeRootBlock mock: a probe that leaves everything clobbered */
static BOOL probe_result;
static ULONG probe_leaves_bs;             /* block size the probe leaves in g */
static int nprobe;
static BOOL ProbeRootBlock(struct rootblock **rootblock, globaldata *g)
{
	nprobe++;
	g->ErrorMsg = NoErrorMsg;
	CalculateBlockSize(g, 0, probe_leaves_bs);
	g->softprotect = (g->softprotect == 1) ? 0 : 1;
	g->protectkey = (g->protectkey == ~0u) ? 0 : ~0u;
	*rootblock = probe_result ? AllocBufmemR(512, g) : NULL;
	return probe_result;
}

/* SameDisk mock */
static BOOL same_result;
static int nsame;
static struct rootblock *same_a;
static int same_b_live;
static BOOL SameDisk(struct rootblock *a, struct rootblock *b)
{
	int i;
	nsame++; same_a = a; same_b_live = 0;
	for (i = 0; i < nlive; i++)
		if (live[i] == b) same_b_live = 1;
	return same_result;
}

#include "gen/GetCurrentRoot.c"             /* the REAL functions from ../../volume.c */
#include "gen/CheckCurrentVolumeBack.c"

static int failures;
static void on_segv(int sig)
{
	static const char m[] = "  FAIL: crash: NULL newbs or NULL current volume dereferenced\n";
	(void)sig;
	write(1, m, sizeof m - 1);
	_exit(1);
}
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct dosenvec env = { 128, 2 };       /* 512 byte sectors, 2 per block */
static globaldata g;

/* g as the running volume left it: block size bs, soft protection state */
static void setup(ULONG bs, BYTE sp, ULONG pk)
{
	memset(&g, 0, sizeof g);
	g.dosenvec = &env;
	CalculateBlockSize_real(&g, 0, bs);
	g.softprotect = sp;
	g.protectkey = pk;
	g.ErrorMsg = _NormalErrorMsg;
	ncbs = nprobe = nsame = 0;
	nlive = 0; nfree_bad = 0;
}

static void same_geometry(const globaldata *a, const globaldata *b, const char *what)
{
	check(a->blocksize == b->blocksize && a->blocksize_phys == b->blocksize_phys &&
		a->blockshift == b->blockshift && a->blocklogshift == b->blocklogshift &&
		a->directsize == b->directsize, what);
}

static void wrapper(const char *what, ULONG old_bs, BYTE sp, ULONG pk, ULONG probe_bs, BOOL found, int use_newbs)
{
	globaldata before;
	struct rootblock *rb = (struct rootblock *)1;
	ULONG newbs = 0xdead;
	BOOL res;

	setup(old_bs, sp, pk);
	before = g;
	probe_result = found;
	probe_leaves_bs = probe_bs;
	res = GetCurrentRoot(&rb, use_newbs ? &newbs : NULL, &g);

	printf("  %-44s res=%d newbs=%u bs=%u\n", what, res, (unsigned)(use_newbs ? newbs : 0), (unsigned)g.blocksize);
	check(nprobe == 1, "probe ran once");
	check(res == found, "probe result passed through");
	check(found ? (rb != NULL && nlive == 1) : rb == NULL, "rootblock passed through");
	check(g.blocksize == old_bs, "logical block size restored");
	same_geometry(&g, &before, "shift/phys/directsize restored with the block size");
	check(g.softprotect == sp, "softprotect restored");
	check(g.protectkey == pk, "protectkey restored");
	check(g.ErrorMsg == _NormalErrorMsg, "ErrorMsg is the normal handler again");
	if (use_newbs && found)
		check(newbs == probe_bs, "*newbs is the block size of the probed disk");
	if (rb) FreeBufmemMock(rb, &g);
}

static void checkback(const char *what, int have_volume, BOOL found, BOOL same, int want)
{
	struct rootblock voldisk;
	struct volumedata vol = { &voldisk };
	globaldata before;
	BOOL res;

	setup(1024, 0, 0);
	before = g;
	g.currentvolume = have_volume ? &vol : NULL;
	probe_result = found; probe_leaves_bs = 512; same_result = same;
	res = CheckCurrentVolumeBack(&g);

	printf("  %-44s res=%d samedisk calls=%d\n", what, res, nsame);
	check((res != 0) == want, "result");
	check(nprobe == 1, "disk probed once");
	check(nlive == 0 && nfree_bad == 0, "probed rootblock buffer freed (once)");
	check(g.blocksize == 1024 && g.softprotect == 0 && g.protectkey == 0 && g.ErrorMsg == _NormalErrorMsg,
		"probe side effects undone");
	same_geometry(&g, &before, "geometry unchanged");
	if (have_volume && found)
		check(nsame == 1, "SameDisk consulted once");
	else
		check(nsame == 0, "SameDisk not consulted");
}

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGSEGV, on_segv);

	/* --- GetCurrentRoot ------------------------------------------------ */
	wrapper("found disk, probe lowered to 512", 1024, 0, 0, 512, TRUE, 1);
	wrapper("found disk, probe leaves bigger size", 512, 0, 0, 2048, TRUE, 1);
	wrapper("soft protection of the old volume kept", 1024, 1, 0x1234, 512, TRUE, 1);
	wrapper("no disk found", 1024, 0, 0, 512, FALSE, 1);
	wrapper("no disk found, soft protected", 2048, 1, 0x77, 512, FALSE, 1);
	wrapper("newbs NULL accepted", 1024, 1, 0x1234, 512, TRUE, 0);

	/* --- CheckCurrentVolumeBack ----------------------------------------- */
	checkback("no volume, readable disk", 0, TRUE, FALSE, 1);
	checkback("no volume, unreadable disk", 0, FALSE, FALSE, 0);
	checkback("volume, same disk back", 1, TRUE, TRUE, 1);
	checkback("volume, other disk", 1, TRUE, FALSE, 0);
	checkback("volume, unreadable disk", 1, FALSE, TRUE, 0);
	{
		/* argument order: (current volume's root, probed root) */
		struct rootblock voldisk;
		struct volumedata vol = { &voldisk };
		setup(1024, 0, 0);
		g.currentvolume = &vol;
		probe_result = TRUE; probe_leaves_bs = 512; same_result = TRUE;
		CheckCurrentVolumeBack(&g);
		check(same_a == &voldisk && same_b_live, "SameDisk(volume->rootblk, probed rootblock)");
	}

	if (failures) { printf("  FAIL: %d check(s) wrong\n", failures); return 1; }
	printf("  PASS: GetCurrentRoot restores block size/softprotect/handler; CheckCurrentVolumeBack\n");
	return 0;
}
