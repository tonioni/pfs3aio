/* LIVE @src volume.c @func IsRBBlock @func CheckBitmapIndex @func CalculateBlockSize @func ProbeRootBlock
 *
 * ProbeRootBlock (the body of the old GetCurrentRoot) decides whether the disk
 * in the drive is a mountable PFS disk. Lifts the checkout's real
 * ProbeRootBlock, IsRBBlock, CheckBitmapIndex and CalculateBlockSize; the raw
 * disk (one image per block size), the allocator and the exec/trackdisk calls
 * are mocks that record what happened.
 *
 *  - a rootblock without MODE_HARDDISK, with rblkcluster 0 or 522 is rejected:
 *    FALSE, no rootblock handed out, every buffer freed, disktype NDOS; MODE_HARDDISK
 *    (alone or with other modes), rblkcluster 1 and 521 are accepted;
 *  - de_SectorPerBlock > 1: when the configured-size read finds no PFS boot/root
 *    block the probe retries ONCE with 512-byte blocks (CalculateBlockSize(g,1,0));
 *    the retry that also fails is a rejection - it does not retry again, does not
 *    fall through as if the disk were valid, and the probe itself never touches
 *    the data cache (NewVolume resizes it later);
 *  - with de_SectorPerBlock <= 1 there is no retry;
 *  - the probe starts with CalculateBlockSize(g,0,0): a block size left over from
 *    an earlier probe (512) must not be used for the first read or the buffers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef int BOOL; typedef void *APTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define ENTER(x)
#define DB(x)
#define EXIT(x)
#define ACCESS_DETECT 1             /* keeps the access-mode block of CalculateBlockSize out */
#define LARGE_FILE_SIZE 1
#define DNSIZE 32
#define MAXBITMAPINDEX 103
#define MAXSMALLBITMAPINDEX 4
#define MAXSMALLINDEXNR 98

/* real values from dos/dos.h, devices/trackdisk.h and blocks.h */
#define ID_WRITE_PROTECTED 80
#define ID_VALIDATING 81
#define ID_VALIDATED 82
#define ID_NO_DISK_PRESENT (-1)
#define ID_UNREADABLE_DISK 0x42414400L
#define ID_NOT_REALLY_DOS 0x4E444F53L
#define TDERR_DiskChanged 29
#define CMD_NONSTD 9
#define TD_CHANGESTATE (CMD_NONSTD + 5)
#define TD_PROTSTATUS (CMD_NONSTD + 6)
#define ID_PFS_DISK  0x50465301L
#define ID_PFS2_DISK 0x50465302L
#define BOOTBLOCK1 0
#define ROOTBLOCK 2
#define MODE_HARDDISK 1
#define MODE_SPLITTED_ANODES 2
#define MODE_DIR_EXTENSION 4
#define MODE_DELDIR 8
#define MODE_SIZEFIELD 16
#define MODE_EXTENSION 32
#define MODE_LARGEFILE 2048

/* the real rootblock (blocks.h), exactly 512 bytes */
typedef struct rootblock
{
	LONG disktype;
	ULONG options;
	ULONG datestamp;
	UWORD creationday, creationminute, creationtick, protection;
	UBYTE diskname[DNSIZE];
	ULONG lastreserved;
	ULONG firstreserved;
	ULONG reserved_free;
	UWORD reserved_blksize;
	UWORD rblkcluster;
	ULONG blocksfree, alwaysfree, roving_ptr, deldir, disksize, extension, not_used;
	union
	{
		struct { ULONG bitmapindex[MAXSMALLBITMAPINDEX + 1]; ULONG indexblocks[MAXSMALLINDEXNR + 1]; } small;
		struct { ULONG bitmapindex[MAXBITMAPINDEX + 1]; } large;
	} idx;
} rootblock_t;

struct dosenvec { ULONG de_SizeBlock, de_SectorPerBlock; };
struct DriveGeometry { ULONG dg_TotalSectors; };
struct IORequest { UWORD io_Command; ULONG io_Actual; };
struct IOExtTD { struct IORequest iotd_Req; };

typedef struct globaldata
{
	struct dosenvec *dosenvec;
	struct DriveGeometry *geom;
	struct IOExtTD *request;
	BOOL removable, trackdisk;
	ULONG disktype, diskstate;
	LONG (*ErrorMsg)(CONST_STRPTR, APTR, ULONG, struct globaldata *);
	ULONG blocksize, blocksize_phys;
	UWORD blockshift, blocklogshift;
	ULONG directsize;
} globaldata;

#define BLOCKSIZE (g->blocksize)
#define BLOCKSHIFT (g->blockshift)
#define FreeBufmem(mem, g) FreeBufmemMock(mem, g)

/* ---- mocks ---------------------------------------------------------- */
static LONG NoErrorMsg(CONST_STRPTR m, APTR a, ULONG d, globaldata *g) { return 0; }
LONG _NormalErrorMsg(CONST_STRPTR m, APTR a, ULONG d, globaldata *g) { return 1; }

static void SetPartitionLimits(globaldata *g) { (void)g; }
static int ninputev, last_inserted;
static void CreateInputEvent(BOOL inserted, globaldata *g) { ninputev++; last_inserted = inserted; }
static int ngeom, nextgeom, ninitdc;
static void GetDriveGeometry(globaldata *g) { ngeom++; }
static void GetExtBlockGeometry(struct rootblock *rb, ULONG n, globaldata *g) { nextgeom++; }
static BOOL UpdateChangeCount(globaldata *g) { return 0; }
static BYTE DoIO(struct IORequest *r) { r->io_Actual = 0; return 0; }
static BOOL detectaccessmode(UBYTE *b, globaldata *g) { return TRUE; }
static BOOL InitDataCache(globaldata *g) { ninitdc++; return TRUE; }

/* recording CalculateBlockSize: the real one, behind a counter */
#define CalculateBlockSize CalculateBlockSize_real
#include "gen/CalculateBlockSize.c"
#undef CalculateBlockSize
static int ncbs_cfg, ncbs_512, ncbs_other;
void CalculateBlockSize(globaldata *g, ULONG spb, ULONG blocksize)
{
	if (!spb && !blocksize) ncbs_cfg++;
	else if (spb == 1 && !blocksize) ncbs_512++;
	else ncbs_other++;
	CalculateBlockSize_real(g, spb, blocksize);
}

/* allocator: remembers live buffers and the requested sizes. Like a real heap
 * it hands the most recently freed block out again WITHOUT clearing it, so a
 * zero-sized request sees the stale root block that was just freed.
 */
#define MAXLIVE 16
#define POOLCAP 4096
static void *live[MAXLIVE];
static void *pooled;
static int nlive, nalloc, nfree_bad;
static ULONG alloc_size[8];
static void *AllocBufmemR(ULONG size, globaldata *g)
{
	void *p;
	if (nalloc < 8) alloc_size[nalloc] = size;
	nalloc++;
	if (pooled && size <= POOLCAP) { p = pooled; pooled = NULL; }
	else p = calloc(1, size < POOLCAP ? POOLCAP : size);
	live[nlive++] = p;
	return p;
}
static void FreeBufmemMock(void *p, globaldata *g)
{
	int i;
	for (i = 0; i < nlive; i++)
		if (live[i] == p)
		{
			live[i] = live[--nlive];
			if (pooled) free(pooled);
			pooled = p;
			return;
		}
	nfree_bad++;                      /* freed something not allocated / twice */
}

/* the raw disk: one image per block size, block n lives at n*bs */
#define IMGSIZE (600 * 1024)
static UBYTE img512[IMGSIZE] __attribute__((aligned(16)));
static UBYTE img1024[IMGSIZE] __attribute__((aligned(16)));
static UBYTE img2048[IMGSIZE] __attribute__((aligned(16)));
static UBYTE *imgfor(ULONG bs) { return bs == 512 ? img512 : bs == 1024 ? img1024 : bs == 2048 ? img2048 : NULL; }

struct rawcall { ULONG bs, blocks, blocknr; };
static struct rawcall raw[16];
static int nraw;
static ULONG RawRead(UBYTE *buf, ULONG blocks, ULONG blocknr, globaldata *g)
{
	UBYTE *img = imgfor(g->blocksize);
	if (nraw < 16) { raw[nraw].bs = g->blocksize; raw[nraw].blocks = blocks; raw[nraw].blocknr = blocknr; }
	nraw++;
	if (!img || (blocknr + blocks) * g->blocksize > IMGSIZE) return 1;
	memcpy(buf, img + (size_t)blocknr * g->blocksize, (size_t)blocks * g->blocksize);
	return 0;
}

#include "gen/IsRBBlock.c"
#include "gen/CheckBitmapIndex.c"
#include "gen/ProbeRootBlock.c"          /* the REAL function from ../../volume.c */

/* ---- test harness --------------------------------------------------- */
static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct dosenvec env;
static struct DriveGeometry geo;
static struct IOExtTD req;
static globaldata g;
static struct rootblock *rb;

static void wipe(void) { memset(img512, 0, IMGSIZE); memset(img1024, 0, IMGSIZE); memset(img2048, 0, IMGSIZE); }

static void put_boot(ULONG bs)
{
	*(LONG *)imgfor(bs) = ID_PFS_DISK;
}
static void put_root(ULONG bs, LONG disktype, ULONG options, UWORD rblk, UWORD resbs)
{
	struct rootblock *r = (struct rootblock *)(imgfor(bs) + 2 * bs);
	r->disktype = disktype;
	r->options = options;
	r->reserved_blksize = resbs;
	r->rblkcluster = rblk;
	r->firstreserved = 2;
	r->lastreserved = 65;
	r->diskname[0] = 4;
}
static void put_valid(ULONG bs, ULONG options, UWORD rblk)
{
	put_boot(bs);
	put_root(bs, ID_PFS_DISK, options, rblk, 1024);
}

/* spb: de_SectorPerBlock of the mountlist (512-byte sectors) */
static BOOL probe(ULONG spb, ULONG stale_bs)
{
	BOOL res;

	memset(&g, 0, sizeof g);
	env.de_SizeBlock = 128;
	env.de_SectorPerBlock = spb;
	g.dosenvec = &env; g.geom = &geo; g.request = &req;
	g.ErrorMsg = _NormalErrorMsg;
	CalculateBlockSize_real(&g, 0, 0);
	if (stale_bs)
		CalculateBlockSize_real(&g, 0, stale_bs);
	rb = (struct rootblock *)1;
	ncbs_cfg = ncbs_512 = ncbs_other = nraw = nalloc = nfree_bad = ninputev = ninitdc = ngeom = nextgeom = 0;
	nlive = 0;
	pooled = NULL;
	res = ProbeRootBlock(&rb, &g);
	return res;
}

static void accepted(const char *what, BOOL res, int nlive_expected)
{
	printf("  %-46s res=%d bs=%u raw=%d\n", what, res, (unsigned)g.blocksize, nraw);
	check(res != 0 && rb != NULL, "valid disk accepted, rootblock handed out");
	check(nlive == nlive_expected && nfree_bad == 0, "exactly the returned rootblock buffer is still allocated");
	check(g.disktype == ID_PFS_DISK, "disktype ID_PFS_DISK");
	check(g.ErrorMsg == _NormalErrorMsg, "normal error handler restored after the read");
	check(ninputev == 0, "no input event");
	check(ninitdc == 0, "the probe does not touch the data cache");
}

static void rejected(const char *what, BOOL res)
{
	printf("  %-46s res=%d bs=%u raw=%d\n", what, res, (unsigned)g.blocksize, nraw);
	check(res == 0, "returns FALSE");
	check(rb == NULL, "no rootblock handed out");
	check(nlive == 0 && nfree_bad == 0, "every buffer freed exactly once");
	check(g.disktype == ID_NOT_REALLY_DOS, "disktype ID_NOT_REALLY_DOS");
	check(ninputev == 1 && last_inserted == TRUE, "one 'disk inserted' input event");
	check(g.ErrorMsg == _NormalErrorMsg, "normal error handler restored");
	check(ninitdc == 0, "the probe does not touch the data cache");
}

int main(void)
{
	BOOL res;

	setvbuf(stdout, NULL, _IOLBF, 0);

	/* --- the rootblock checks ----------------------------------------- */
	wipe(); put_valid(1024, MODE_HARDDISK | MODE_SPLITTED_ANODES, 3);
	res = probe(2, 0);
	accepted("valid harddisk rootblock, 1024", res, 1);
	check(rb->rblkcluster == 3, "the full rblkcluster buffer is returned");
	check(g.blocksize == 1024 && ncbs_cfg == 1 && ncbs_512 == 0, "configured block size kept, no retry");
	check(nraw == 3 && raw[0].bs == 1024 && raw[0].blocknr == BOOTBLOCK1 && raw[1].blocknr == ROOTBLOCK &&
		raw[2].blocknr == ROOTBLOCK && raw[2].blocks == 3, "reads: boot, root, whole rootblock cluster");
	check(nalloc == 2 && alloc_size[0] == 2048 && alloc_size[1] == 3 * 1024, "buffers: 2 blocks, then rblkcluster blocks");

	wipe(); put_valid(1024, MODE_HARDDISK, 1);
	res = probe(2, 0);
	accepted("MODE_HARDDISK alone, rblkcluster 1", res, 1);

	wipe(); put_valid(1024, MODE_SPLITTED_ANODES, 3);
	res = probe(2, 0);
	rejected("options without MODE_HARDDISK", res);
	check(nraw == 2, "rejected before the rootblock cluster is read");

	wipe(); put_valid(1024, MODE_SPLITTED_ANODES | MODE_DIR_EXTENSION | MODE_DELDIR | MODE_SIZEFIELD, 3);
	res = probe(2, 0);
	rejected("many options, still no MODE_HARDDISK", res);

	wipe(); put_valid(1024, MODE_HARDDISK | MODE_SPLITTED_ANODES | MODE_DIR_EXTENSION | MODE_DELDIR, 3);
	res = probe(2, 0);
	accepted("MODE_HARDDISK with other modes", res, 1);

	wipe(); put_valid(1024, MODE_HARDDISK, 0);
	res = probe(2, 0);
	rejected("rblkcluster 0", res);

	wipe(); put_valid(1024, MODE_HARDDISK, 522);
	res = probe(2, 0);
	rejected("rblkcluster 522", res);
	check(nraw == 2, "the oversized cluster is never read");

	wipe(); put_valid(1024, MODE_HARDDISK, 521);
	res = probe(2, 0);
	accepted("rblkcluster 521 (largest allowed)", res, 1);
	check(nraw == 3 && raw[2].blocks == 521, "the 521 block cluster is read");

	/* --- the 512-byte retry ------------------------------------------- */
	wipe(); put_valid(512, MODE_HARDDISK, 2);
	res = probe(2, 0);
	accepted("no PFS at 1024, valid at 512: retry", res, 1);
	check(g.blocksize == 512 && ncbs_cfg == 1 && ncbs_512 == 1 && ncbs_other == 0, "one retry with CalculateBlockSize(g,1,0)");
	check(nraw == 5 && raw[0].bs == 1024 && raw[1].bs == 1024 && raw[2].bs == 512 && raw[3].bs == 512 &&
		raw[4].bs == 512 && raw[4].blocks == 2, "two reads per size, then the cluster at 512");
	check(nalloc == 2 && alloc_size[0] == 2048 && alloc_size[1] == 2 * 512, "second buffer sized with the new block size");

	wipe(); put_valid(512, MODE_HARDDISK, 2);
	res = probe(4, 0);
	accepted("de_SectorPerBlock 4: retry at 512", res, 1);
	check(g.blocksize == 512 && raw[0].bs == 2048 && ncbs_512 == 1, "from 2048 straight to 512");

	wipe();
	res = probe(1, 0);
	rejected("de_SectorPerBlock 1, no PFS: no retry", res);
	check(ncbs_512 == 0 && nraw == 2 && g.blocksize == 512, "no retry, two reads only");

	wipe();
	res = probe(2, 0);
	rejected("no PFS at either size", res);
	check(ncbs_512 == 1 && nraw == 4 && g.blocksize == 512, "exactly one retry, four reads");

	/* the second pass finds an id-less block that is otherwise a valid root */
	wipe(); put_valid(512, MODE_HARDDISK, 2);
	put_root(512, 0, MODE_HARDDISK, 2, 1024);
	res = probe(2, 0);
	rejected("retry pass fails: not treated as valid", res);
	check(ncbs_512 == 1 && nraw == 4, "no third attempt after the failed retry");

	/* the root block is fine but the boot block is not a PFS boot block */
	wipe(); put_root(512, ID_PFS_DISK, MODE_HARDDISK, 2, 1024);
	res = probe(2, 0);
	rejected("root block without PFS boot block", res);

	/* --- start from the configured block size --------------------------- */
	wipe(); put_valid(1024, MODE_HARDDISK, 2);
	res = probe(2, 512);
	accepted("stale 512 from an earlier probe", res, 1);
	check(raw[0].bs == 1024 && alloc_size[0] == 2048 && g.blocksize == 1024, "first read and buffer use the configured size");
	check(ncbs_cfg == 1 && ncbs_512 == 0, "reset with CalculateBlockSize(g,0,0), no retry needed");

	if (failures) { printf("  FAIL: %d check(s) wrong\n", failures); return 1; }
	printf("  PASS: ProbeRootBlock rootblock checks, 512 retry and block size reset\n");
	return 0;
}
