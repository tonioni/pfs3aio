/* LIVE @src format.c @func MakeBootBlock @func MinNumReserved @func MakeRootBlock @func FDSFormat
 *
 * A format that cannot succeed must not touch the disk. The boot block is the
 * first thing a formatter overwrites; if it is written before the checks have
 * passed, a rejected or failed format still destroys whatever was on the
 * partition (and the old volume's boot block says "PFS" for a disk whose root
 * block is gone). So every failure path of FDSFormat must leave the disk with
 * zero writes, and a rejection of the DosEnvec must also leave the geometry
 * the driver is currently using alone.
 *
 * Everything FDSFormat calls is mocked; the functions above are the driver's
 * real code. A reserved area asked for in KiB (pfsformat RESERVED) is counted
 * in reserved blocks: 1 KiB with 512 byte blocks, 4 KiB with 4096 byte
 * blocks. It is never smaller than the minimum the bitmap of the partition
 * needs, and more than MAXNUMRESERVED blocks are refused with nothing
 * written.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "live.h"
#include "pfs3disk.h"

typedef UBYTE *DSTR;
#define min(a, b) ((a) < (b) ? (a) : (b))
#define ENTER(x)

#define PFS_ERROR_INCONSISTENT 213	/* ERROR_DISK_NOT_VALIDATED, as in struct.h */
#define RESERVED_BUFFER 10	/* as in struct.h */

#define DE_SIZEBLOCK 1
#define DE_SECSPERBLK 4
#define DE_LOWCYL 9

#define BOOTBLOCK1 0
#define ROOTBLOCK 2

/* the build configuration under test */
#define DELDIR 1
#define ACCESS_DETECT 1
#define WARN_EXPERIMENTAL 0
#define LARGE_FILE_SIZE 1

struct DriveGeometry { ULONG dg_SectorSize, dg_TotalSectors; };
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };
typedef struct rootblock rootblock_t;
struct crootblockextension;
typedef struct crootblockextension crootblockextension_t;
struct volumedata {
	struct rootblock *rootblk;
	struct crootblockextension *rblkextension;
	int metadata_error;
};
typedef struct globaldata {
	struct DriveGeometry *geom;
	ULONG *dosenvec;
	LONG softprotect;
	ULONG protectkey;
	ULONG blocksize, blocksize_phys, blockshift, blocklogshift;
	ULONG firstblock, lastblock, firstblocknative, lastblocknative;
	UWORD supermode;
	BOOL dirty, deldirenabled;
	struct volumedata *currentvolume;
	struct rootblock *rootblock;
} globaldata;
#define BLOCKSIZE (g->blocksize)
#define BLOCKSHIFT (g->blockshift)

static globaldata G;
static struct DriveGeometry GEOM;
static ULONG ENV[16];
static struct volumedata VOL;

/* knobs and counters */
static int raw_writes, raw_start, raw_nblocks, updates;
static int detect_ok, bitmap_ok, rootdir_ok, modules_err, rext_ok, cache_ok;
static ULONG deldir_err;
static ULONG want_first, want_last;

static void *AllocBufmem(ULONG size, globaldata *g) { return calloc(1, size); }
static void FreeBufmem(void *p, globaldata *g) { free(p); }
static void DateStamp(struct DateStamp *d) { d->ds_Days = d->ds_Minute = d->ds_Tick = 0; }
static ULONG RawWrite(UBYTE *buf, ULONG nblocks, ULONG start, globaldata *g)
{
	raw_writes++; raw_nblocks = nblocks; raw_start = start;
	return 0;
}
static BOOL detectaccessmode(UBYTE *buf, globaldata *g) { return detect_ok; }
static BOOL InitLRU(globaldata *g, UWORD blocksize) { return TRUE; }
static ULONG made_reserved;	/* the reserved blocks of the last format */
static void MakeReservedBitmap(struct rootblock **rbl, ULONG numreserved, globaldata *g) { made_reserved = numreserved; }
static ULONG default_reserved = 32;	/* what the size of the device gives */
static ULONG CalcNumReserved(globaldata *g, ULONG resblocksize) { return default_reserved; }
static int versions;	/* version requesters shown */
static BOOL requester = TRUE;
static void ShowVersion(globaldata *g) { versions++; }
static void GetDriveGeometry(globaldata *g) {}

/* what the driver's block size and partition limit code would set up */
static void CalculateBlockSize(globaldata *g, ULONG unused, ULONG bs)
{
	g->blocksize = bs;
	g->blockshift = 0;
	while (bs > 1) { g->blockshift++; bs >>= 1; }
	g->blocklogshift = g->blockshift > 9 ? g->blockshift - 9 : 0;
	g->blocksize_phys = g->geom->dg_SectorSize;
	g->firstblock = want_first;
	g->lastblock = want_last;
}
static BOOL InitDataCache(globaldata *g) { return cache_ok; }
/* volume.c's SwitchBlockSize, which only exists in our tree; master calls
 * CalculateBlockSize and InitDataCache directly */
static BOOL SwitchBlockSize(ULONG bs, ULONG oldbs, globaldata *g)
{
	CalculateBlockSize(g, 0, bs);
	if (InitDataCache(g))
		return TRUE;
	CalculateBlockSize(g, 0, oldbs);
	InitDataCache(g);
	return FALSE;
}

static struct volumedata *MakeVolumeData(struct rootblock *rbl, globaldata *g)
{
	memset(&VOL, 0, sizeof(VOL));
	VOL.rootblk = rbl;
	return &VOL;
}
static crootblockextension_t *MakeFormatRBlkExtension(struct rootblock *rbl, globaldata *g)
{
	return rext_ok ? (crootblockextension_t *)&G : NULL;
}
static void InitModules(struct volumedata *v, BOOL format, globaldata *g) { v->metadata_error = modules_err; }
static BOOL MakeBitmap(globaldata *g) { return bitmap_ok; }
static UWORD AllocAnode(ULONG connect, globaldata *g)
{
	static UWORD next;
	return next++ % ANODE_ROOTDIR;
}
static BOOL MakeRootDir(SIPTR *error, globaldata *g)
{
	if (!rootdir_ok) { *error = ERROR_DISK_FULL; return FALSE; }
	return TRUE;
}
static ULONG SetDeldir(int nbr, globaldata *g) { return deldir_err; }
static BOOL UpdateDisk(globaldata *g) { updates++; return TRUE; }
static void FreeVolumeResources(struct volumedata *v, globaldata *g) {}

#include "gen/MakeBootBlock.c"
#include "gen/MinNumReserved.c"
#include "gen/MakeRootBlock.c"
#include "gen/FDSFormat.c"

static int failures;
static void check(int ok, const char *msg)
{
	if (!ok) { printf("  FAIL: %s\n", msg); failures++; }
}

struct spec {
	ULONG sector, total;        /* what the device reports */
	ULONG size_block, secsperblk; /* DosEnvec */
	ULONG first, last;          /* partition limits it ends up with */
	ULONG reservedkib;           /* reserved area asked for, 0 the default */
};

/* a healthy 512-byte block partition of 20000 blocks */
static struct spec good(void)
{
	struct spec s = { 512, 20000, 128, 1, 2, 19999, 0 };
	return s;
}

static void reset(void)
{
	detect_ok = bitmap_ok = rootdir_ok = rext_ok = cache_ok = 1;
	modules_err = 0; deldir_err = 0;
}

static BOOL run(const struct spec *s, const char *name, SIPTR *error)
{
	UBYTE dn[40];
	BOOL rc;

	memset(&G, 0, sizeof(G));
	GEOM.dg_SectorSize = s->sector;
	GEOM.dg_TotalSectors = s->total;
	memset(ENV, 0, sizeof(ENV));
	ENV[DE_SIZEBLOCK] = s->size_block;
	ENV[DE_SECSPERBLK] = s->secsperblk;
	G.geom = &GEOM;
	G.dosenvec = ENV;
	/* the geometry of the volume that is currently mounted */
	G.blocksize = 512; G.blockshift = 9; G.blocksize_phys = 512;
	G.firstblock = 7; G.lastblock = 77;
	want_first = s->first; want_last = s->last;
	raw_writes = updates = versions = 0;
	*error = 0;
	dn[0] = (UBYTE)strlen(name);
	memcpy(dn + 1, name, dn[0]);
	rc = FDSFormat(dn, 0, error, s->reservedkib, requester, &G);
	return rc;
}

static int geometry_untouched(void)
{
	return G.blocksize == 512 && G.blockshift == 9 && G.firstblock == 7 && G.lastblock == 77;
}

/* the format must be refused with `want`, without writing anything */
static void rejected(const char *what, const struct spec *s, SIPTR want, int geometry)
{
	SIPTR error;
	BOOL rc = run(s, "Test", &error);

	printf("  %-38s rc=%d error=%ld  writes=%d updates=%d%s\n", what, rc, (long)error, raw_writes, updates,
		geometry ? (geometry_untouched() ? "  geometry kept" : "  GEOMETRY CHANGED") : "");
	check(rc == DOSFALSE, what);
	check(error == want, "wrong error code");
	check(raw_writes == 0 && updates == 0, "a refused format must not write anything to the disk");
	if (geometry)
		check(geometry_untouched(), "a refused DosEnvec must not change the geometry in use");
}

int main(void)
{
	struct spec s;
	SIPTR error;
	BOOL rc;

	reset();
	s = good();
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d error=%ld  writes=%d (start %d, %d blocks) updates=%d\n", "healthy partition", rc, (long)error,
		raw_writes, raw_start, raw_nblocks, updates);
	check(rc == DOSTRUE, "a healthy partition must format");
	check(raw_writes == 1 && raw_start == BOOTBLOCK1 && raw_nblocks == 2, "the two boot blocks are written once");
	check(updates == 1 && G.currentvolume == NULL, "the volume is written out and released");

	/* DosEnvec and device checks come first: nothing may change */
	s = good(); s.sector = 256;
	rejected("sector size below 512", &s, ERROR_BAD_NUMBER, 1);
	s = good(); s.sector = 8192;
	rejected("sector size above 4096", &s, ERROR_BAD_NUMBER, 1);
	s = good(); s.sector = 768;
	rejected("sector size not a power of two", &s, ERROR_BAD_NUMBER, 1);
	s = good(); s.size_block = 512; s.secsperblk = 4;      /* 8192 */
	rejected("logical block above 4096", &s, ERROR_BAD_NUMBER, 1);
	s = good(); s.size_block = 96;                         /* 384 */
	rejected("logical block not a power of two", &s, ERROR_BAD_NUMBER, 1);
	s = good(); s.total = 0xfffffff0u;
	rejected("larger than the maximum disk size", &s, ERROR_OBJECT_TOO_LARGE, 1);

	/* disk name, write protection */
	reset();
	s = good();
	{
		struct { const char *name; int ok; } names[] = {
			{ "", 0 },
			{ "123456789012345678901234567890", 1 },     /* 30 */
			{ "1234567890123456789012345678901", 0 },    /* 31 */
		};
		int i;
		for (i = 0; i < 3; i++) {
			rc = run(&s, names[i].name, &error);
			printf("  name of %2d characters                  rc=%d error=%ld  writes=%d\n", (int)strlen(names[i].name), rc, (long)error, raw_writes);
			if (names[i].ok) {
				check(rc == DOSTRUE, "a name of 30 characters must format");
			} else {
				check(rc == DOSFALSE && error == ERROR_INVALID_COMPONENT_NAME, "unusable name must be rejected");
				check(raw_writes == 0 && updates == 0, "a rejected name must not write");
			}
		}
	}

	/* nothing usable in the partition, or too little: refused before any write */
	reset();
	s = good(); s.first = 1; s.last = 0;
	rejected("empty partition (last < first)", &s, ERROR_BAD_NUMBER, 0);
	s = good(); s.first = 2; s.last = 101;                 /* 100 blocks */
	rejected("100 blocks", &s, ERROR_DISK_FULL, 0);
	s = good(); s.first = 0; s.last = 126;                 /* 127 blocks */
	rejected("127 blocks (one below the minimum)", &s, ERROR_DISK_FULL, 0);
	s = good(); s.first = 0; s.last = 199; s.reservedkib = 1000;
	rejected("reserved area larger than partition", &s, ERROR_DISK_FULL, 0);
	s = good(); s.first = 0; s.last = 129; s.reservedkib = 33; /* 64 reserved clusters of 2: 130 blocks used */
	rejected("reserved area uses every block", &s, ERROR_DISK_FULL, 0);

	/* smallest partitions that must still work */
	s = good(); s.first = 0; s.last = 127;
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d error=%ld  writes=%d\n", "128 blocks (the minimum)", rc, (long)error, raw_writes);
	check(rc == DOSTRUE && raw_writes == 1, "128 blocks must format");
	check(versions == 1, "ACTION_FORMAT shows the version requester");
	requester = FALSE;
	rc = run(&s, "Test", &error);
	requester = TRUE;
	printf("  %-38s rc=%d versions=%d\n", "without the requester", rc, versions);
	check(rc == DOSTRUE && versions == 0, "ACTION_PFS_FORMAT shows no requester");
	s = good(); s.first = 0; s.last = 130; s.reservedkib = 33; /* one block left for data */
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d error=%ld  writes=%d\n", "one data block left", rc, (long)error, raw_writes);
	check(rc == DOSTRUE && raw_writes == 1, "a partition with one block more than the reserved area must format");

	/* 4096 byte blocks: 1000 KiB are 250 reserved blocks of 4 KiB, 256 rounded up */
	s = good(); s.size_block = 1024; s.first = 0; s.last = 256; s.reservedkib = 1000;
	rejected("4096 byte blocks, 1000 KiB in 257 blocks", &s, ERROR_DISK_FULL, 0);
	s = good(); s.size_block = 1024; s.first = 0; s.last = 299; s.reservedkib = 1000;
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d error=%ld  writes=%d\n", "4096 byte blocks, 1000 KiB in 300", rc, (long)error, raw_writes);
	check(rc == DOSTRUE && raw_writes == 1, "1000 KiB of 4 KiB reserved blocks must fit 300 blocks");

	/* below the default: the size asked for, 64 rounded up from 33 */
	default_reserved = 1024;
	s = good(); s.first = 0; s.last = 299; s.reservedkib = 33;
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d reserved=%lu\n", "33 KiB, a default of 1024 in 300", rc, (unsigned long)made_reserved);
	check(rc == DOSTRUE && made_reserved == 64, "a size below the default must be taken");
	default_reserved = 32;
	/* below the minimum: the minimum, the bitmap twice and the format's
	 * blocks; 1 KiB on 2 GiB of 512 byte blocks needs 2 * (519 + 3) + 7 + 10
	 * + a rootblock cluster of 1, 1088 rounded up */
	s = good(); s.total = 4194304; s.first = 0; s.last = 4194303; s.reservedkib = 1;
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d reserved=%lu\n", "1 KiB on 2 GiB", rc, (unsigned long)made_reserved);
	check(rc == DOSTRUE && made_reserved == 1088, "a size below the minimum must give the minimum");
	/* more than MAXNUMRESERVED blocks */
	s = good(); s.reservedkib = 0x7fffffff;
	rejected("more reserved blocks than a volume holds", &s, ERROR_BAD_NUMBER, 0);

	/* anything that fails after the checks must still leave the boot blocks alone */
	s = good();
	reset(); cache_ok = 0;
	rejected("no memory for the data cache", &s, ERROR_NO_FREE_STORE, 0);
	reset(); detect_ok = 0;
	rejected("access mode detection fails", &s, ERROR_OBJECT_TOO_LARGE, 0);
	reset(); rext_ok = 0;
	rejected("root block extension fails", &s, ERROR_NO_FREE_STORE, 0);
	reset(); modules_err = 1;
	rejected("module setup reports damage", &s, PFS_ERROR_INCONSISTENT, 0);
	reset(); bitmap_ok = 0;
	rejected("bitmap cannot be created", &s, ERROR_DISK_FULL, 0);
	reset(); rootdir_ok = 0;
	rejected("root directory cannot be created", &s, ERROR_DISK_FULL, 0);
	reset(); deldir_err = ERROR_DISK_FULL;
	rejected("deldir cannot be created", &s, ERROR_DISK_FULL, 0);

	/* error-induced soft protect: run() resets the globals, so set it up by hand */
	reset();
	{
		UBYTE dn[] = { 4, 'T', 'e', 's', 't' };
		memset(&G, 0, sizeof(G));
		GEOM.dg_SectorSize = 512; GEOM.dg_TotalSectors = 20000;
		memset(ENV, 0, sizeof(ENV)); ENV[DE_SIZEBLOCK] = 128; ENV[DE_SECSPERBLK] = 1;
		G.geom = &GEOM; G.dosenvec = ENV; G.softprotect = -1;
		raw_writes = updates = 0; error = 0;
		rc = FDSFormat(dn, 0, &error, 0, TRUE, &G);
		printf("  %-38s rc=%d error=%ld  writes=%d\n", "error-induced soft protect", rc, (long)error, raw_writes);
		check(rc == DOSFALSE && error == ERROR_DISK_WRITE_PROTECTED, "soft protect must refuse a format");
		check(raw_writes == 0 && updates == 0, "a protected disk must not be written");
	}

	if (failures == 0) { printf("  PASS: a format that cannot succeed leaves the disk untouched\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
