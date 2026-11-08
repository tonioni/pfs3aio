/* LIVE @src format.c @func MakeBootBlock @func MakeRootBlock @func FDSFormat
 *
 * A format that cannot succeed must not touch the disk. The boot block is the
 * first thing a formatter overwrites; if it is written before the checks have
 * passed, a rejected or failed format still destroys whatever was on the
 * partition (and the old volume's boot block says "PFS" for a disk whose root
 * block is gone). So every failure path of FDSFormat must leave the disk with
 * zero writes, and a rejection of the DosEnvec must also leave the geometry
 * the driver is currently using alone.
 *
 * Everything FDSFormat calls is mocked; the three functions above are the
 * driver's real code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef unsigned int ULONG; typedef int LONG; typedef unsigned short UWORD;
typedef unsigned char UBYTE; typedef int BOOL; typedef intptr_t SIPTR;
typedef UBYTE *DSTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define min(a, b) ((a) < (b) ? (a) : (b))
#define ENTER(x)

#define ERROR_NO_FREE_STORE 103
#define ERROR_BAD_NUMBER 115
#define ERROR_INVALID_COMPONENT_NAME 210
#define ERROR_OBJECT_TOO_LARGE 212
#define ERROR_DISK_WRITE_PROTECTED 214
#define ERROR_DISK_FULL 221
#define PFS_ERROR_INCONSISTENT 0x7001

#define DE_SIZEBLOCK 1
#define DE_SECSPERBLK 4
#define DE_LOWCYL 9

#define DNSIZE 32
#define BOOTBLOCK1 0
#define ROOTBLOCK 2
#define ANODE_ROOTDIR 5
#define MAXNUMRESERVED (4096 + 255*1024*8)
#define BITMAP_PAYLOAD_1K ((1024/4)-3)
#define BITMAP_PAYLOAD_2K ((2048/4)-3)
#define BITMAP_PAYLOAD_4K ((4096/4)-3)
#define MAXSMALLDISK ((4+1)*BITMAP_PAYLOAD_1K*BITMAP_PAYLOAD_1K*32)
#define MAXDISKSIZE1K ((103+1)*BITMAP_PAYLOAD_1K*BITMAP_PAYLOAD_1K*32)
#define MAXDISKSIZE2K ((103+1)*BITMAP_PAYLOAD_2K*BITMAP_PAYLOAD_2K*32)
#define MAXDISKSIZE ((ULONG)(103+1)*BITMAP_PAYLOAD_4K*BITMAP_PAYLOAD_4K*32)

#define ID_PFS_DISK  0x50465301
#define ID_PFS2_DISK 0x50465302
#define MODE_HARDDISK 1
#define MODE_SPLITTED_ANODES 2
#define MODE_DIR_EXTENSION 4
#define MODE_DELDIR 8
#define MODE_EXTENSION 32
#define MODE_DATESTAMP 64
#define MODE_SUPERINDEX 128
#define MODE_SUPERDELDIR 256
#define MODE_EXTROVING 512
#define MODE_LONGFN 1024
#define MODE_LARGEFILE 2048
#define MODE_STORED_GEOM 4096

/* the build configuration under test */
#define DELDIR 1
#define ACCESS_DETECT 1
#define WARN_EXPERIMENTAL 0
#define LARGE_FILE_SIZE 1

struct DriveGeometry { ULONG dg_SectorSize, dg_TotalSectors; };
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };
struct rootblock {
	ULONG disktype, options, datestamp, protection;
	UWORD creationday, creationminute, creationtick;
	ULONG firstreserved, lastreserved, reserved_free, blocksfree, alwaysfree;
	ULONG reserved_blksize, extension;
	UBYTE diskname[DNSIZE];
};
typedef struct rootblock rootblock_t;
struct bootblock { ULONG disktype; UBYTE rest[1020]; };
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
static void MakeReservedBitmap(struct rootblock **rbl, ULONG numreserved, globaldata *g) {}
static ULONG CalcNumReserved(globaldata *g, ULONG resblocksize) { return 32; }
static void ShowVersion(globaldata *g) {}
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
	ULONG rsblocks;
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
	raw_writes = updates = 0;
	*error = 0;
	dn[0] = (UBYTE)strlen(name);
	memcpy(dn + 1, name, dn[0]);
	rc = FDSFormat(dn, 0, error, s->rsblocks, &G);
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
	s = good(); s.first = 0; s.last = 199; s.rsblocks = 1000;
	rejected("reserved area larger than partition", &s, ERROR_DISK_FULL, 0);
	s = good(); s.first = 0; s.last = 129; s.rsblocks = 33; /* 64 reserved clusters of 2: 130 blocks used */
	rejected("reserved area uses every block", &s, ERROR_DISK_FULL, 0);

	/* smallest partitions that must still work */
	s = good(); s.first = 0; s.last = 127;
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d error=%ld  writes=%d\n", "128 blocks (the minimum)", rc, (long)error, raw_writes);
	check(rc == DOSTRUE && raw_writes == 1, "128 blocks must format");
	s = good(); s.first = 0; s.last = 130; s.rsblocks = 33; /* one block left for data */
	rc = run(&s, "Test", &error);
	printf("  %-38s rc=%d error=%ld  writes=%d\n", "one data block left", rc, (long)error, raw_writes);
	check(rc == DOSTRUE && raw_writes == 1, "a partition with one block more than the reserved area must format");

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
		rc = FDSFormat(dn, 0, &error, 0, &G);
		printf("  %-38s rc=%d error=%ld  writes=%d\n", "error-induced soft protect", rc, (long)error, raw_writes);
		check(rc == DOSFALSE && error == ERROR_DISK_WRITE_PROTECTED, "soft protect must refuse a format");
		check(raw_writes == 0 && updates == 0, "a protected disk must not be written");
	}

	if (failures == 0) { printf("  PASS: a format that cannot succeed leaves the disk untouched\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
