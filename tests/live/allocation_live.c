/* LIVE @src allocation.c @func InitAllocation
 *
 * Proof that corrupt free-space counters cannot make the allocator wrap or
 * sweep (almost) forever. The real InitAllocation is lifted from allocation.c
 * and driven with a mock volume that has a valid reserved area (the reserved
 * area itself is covered by reserved_bitmap_live.c):
 *  - alloc_available must not wrap when blocksfree < alwaysfree, and the
 *    free counter may not exceed the volume size;
 *  - no_bmb never exceeds what GetBitmapBlock can address, so a failed
 *    sweep ends.
 * Valid volumes must keep the original values.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef uint8_t UBYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define VOID void
#define ROOTBLOCK 2
#define MODE_EXTROVING 0x200
#define TBF_CACHE_SIZE 128
#define MAXBITMAPINDEX 103
#define MAXSMALLBITMAPINDEX 4
#define ENTER(x)
#define EXIT(x)
#define DB(x)

typedef union rootblock
{
	struct
	{
		ULONG options;
		ULONG blocksfree;
		ULONG alwaysfree;
		ULONG firstreserved, lastreserved;
		ULONG reserved_blksize;
		ULONG reserved_free;
		UWORD rblkcluster;
	};
	UBYTE raw[512];
} rootblock_t;

typedef struct bitmapblock
{
	UWORD id;
	UWORD not_used;
	ULONG datestamp;
	ULONG seqnr;
	ULONG bitmap[0];
} bitmapblock_t;

struct volumedata
{
	rootblock_t *rootblk;
	ULONG numblocks;
	ULONG rescluster;
	ULONG bytesperblock;
	struct { struct { ULONG reserved_roving; UWORD rovingbit; } blk; } *rblkextension;
	BOOL rootblockchangeflag;
	BOOL metadata_error;
};

struct allocation_data_s
{
	ULONG clean_blocksfree;
	ULONG alloc_available;
	ULONG longsperbmb;
	ULONG no_bmb;
	ULONG bitmapstart;
	ULONG tobefreed[TBF_CACHE_SIZE][2];
	ULONG tobefreed_index;
	ULONG tbf_resneed;
	struct bitmapblock *res_bitmap;
	ULONG res_roving;
	UWORD rovingbit;
	ULONG numreserved;
	ULONG *reservedtobefreed;
	ULONG rtbf_size;
	ULONG rtbf_index;
	BOOL res_alert;
};

typedef struct globaldata
{
	UBYTE harddiskmode, supermode;
	UWORD blockshift;
	BOOL dirty;
	rootblock_t *rootblock;
	struct volumedata *currentvolume;
	struct allocation_data_s glob_allocdata;
	struct { UWORD indexperblock; } glob_anodedata;
} globaldata;

#define alloc_data (g->glob_allocdata)
#define andata (g->glob_anodedata)
#define BLOCKSHIFT (g->blockshift)
#define LONGS_PER_BMB ((g->rootblock->reserved_blksize/4)-3)

static const char AFS_ERROR_ANODE_INIT[] = "anode init";
static const char AFS_ERROR_DNV_ALLOC_INFO[] = "alloc info";
static void ErrorMsg(const char *msg, void *args, globaldata *g) { (void)msg; (void)args; (void)g; }
static void FreeMem(void *p, ULONG size) { (void)p; (void)size; }

#include "gen/InitAllocation.c"             /* the REAL function from ../../allocation.c */

/* the checks below use the test's own globaldata */
#undef alloc_data
#undef andata
#define alloc_data (G.glob_allocdata)
#define andata (G.glob_anodedata)

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata G;
static struct volumedata V;
static rootblock_t R;

/* rescluster 2 (1024 byte reserved blocks) on a 512 byte block device */
static void setup(ULONG numblocks, ULONG blocksfree, ULONG alwaysfree, ULONG lastreserved, UWORD rblkcluster)
{
	memset(&G, 0, sizeof G);
	memset(&V, 0, sizeof V);
	memset(&R, 0, sizeof R);
	G.harddiskmode = 1;
	G.blockshift = 9;
	andata.indexperblock = 250;
	R.options = 0;
	R.blocksfree = blocksfree;
	R.alwaysfree = alwaysfree;
	R.firstreserved = 2;
	R.lastreserved = lastreserved;
	R.reserved_blksize = 1024;
	R.rblkcluster = rblkcluster;
	V.rootblk = &R;
	V.numblocks = numblocks;
	V.rescluster = 2;
	V.bytesperblock = 512;
	G.rootblock = &R;
	G.currentvolume = &V;
}

int main(void)
{
	ULONG maxbmb = (MAXSMALLBITMAPINDEX + 1) * 250;

	/* valid volume: nothing may change */
	setup(10000, 9000, 450, 129, 2);
	InitAllocation(&V, &G);
	CHECK(!V.metadata_error, "valid: metadata_error set");
	CHECK(alloc_data.clean_blocksfree == 9000, "valid: clean_blocksfree %u", alloc_data.clean_blocksfree);
	CHECK(alloc_data.alloc_available == 8550, "valid: alloc_available %u", alloc_data.alloc_available);
	CHECK(alloc_data.bitmapstart == 130, "valid: bitmapstart %u", alloc_data.bitmapstart);
	CHECK(alloc_data.no_bmb == 2, "valid: no_bmb %u", alloc_data.no_bmb);
	CHECK(alloc_data.numreserved == 64, "valid: numreserved %u", alloc_data.numreserved);

	/* blocksfree below the alwaysfree reserve: must not wrap */
	setup(10000, 100, 450, 129, 2);
	InitAllocation(&V, &G);
	CHECK(alloc_data.alloc_available == 0, "alloc_available wrapped to %u", alloc_data.alloc_available);

	/* more free blocks than the volume has */
	setup(10000, 0xfffffff0u, 450, 129, 2);
	InitAllocation(&V, &G);
	CHECK(alloc_data.clean_blocksfree <= 10000, "clean_blocksfree %u > numblocks", alloc_data.clean_blocksfree);
	CHECK(alloc_data.alloc_available <= 10000, "alloc_available %u > numblocks", alloc_data.alloc_available);

	/* absurd volume size: no_bmb stays within the bitmap index capacity */
	setup(0xfffffff0u, 9000, 450, 129, 2);
	InitAllocation(&V, &G);
	CHECK(alloc_data.no_bmb <= maxbmb, "no_bmb %u exceeds addressable %u", alloc_data.no_bmb, maxbmb);

	if (fails) { printf("  %d check(s) failed\n", fails); return 1; }
	printf("  PASS: corrupt free-space counters are bounded, valid volumes unchanged\n");
	return 0;
}
