/* LIVE @src dd_funcs.c @func dd_Close
 *
 * Proof of the dd_Close fix: dos.library frees the FileHandle whatever dd_Close
 * returns, so the fileentry must be removed from the volume on EVERY path.
 * Lifts the checkout's real dd_Close and drives it through each failure that
 * used to return early (volume gone/not writable, flushed dirblock, read-only
 * close on a ghost volume), asserting the entry is removed exactly once
 * and the error is reported.
 *
 * A tree that returns DOSFALSE without RemoveListEntry leaves a stale
 * fileentry behind (the ghost volume never dies) -- this test FAILS. With the
 * fix every path removes the entry and it PASSES.
 */
#include <stdio.h>
#include <stdint.h>
typedef intptr_t SIPTR; typedef int32_t LONG; typedef uint32_t ULONG; typedef uint8_t UBYTE;
typedef int BOOL; typedef uint32_t FSIZE;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define DELDIR 1
#define ETF_VOLUME 1
#define ERROR_DEVICE_NOT_MOUNTED 218
#define PFS_ERROR_INCONSISTENT 0x7001

struct DosPacket { SIPTR dp_Arg1; SIPTR dp_Res2; };
struct volumedata { BOOL metadata_error; };
struct fileinfo { void *direntry; };
union objectinfo { struct fileinfo file; };
typedef struct listentry {
	struct volumedata *volume;
	struct { struct { UBYTE type; } flags; } type;
	union objectinfo info;
	ULONG dirblocknr;
} listentry_t;
typedef struct { listentry_t le; UBYTE checknotify; FSIZE originalsize; } fileentry_t;
typedef struct globaldata { int dummy; } globaldata;

#define IsVolumeEntry(e) ((e)->type.flags.type == ETF_VOLUME)
#define IsDelFile(oi) 0

static int vol_ok, dirblock_after_update, removed, touched;
static struct volumedata *removed_volume;

static BOOL CheckVolume(struct volumedata *v, BOOL write, SIPTR *err, globaldata *g)
{
	(void)v; (void)g;
	(void)write;
	if (vol_ok) return TRUE;
	*err = ERROR_DEVICE_NOT_MOUNTED;
	return FALSE;
}
static void UpdateLE(listentry_t *le, globaldata *g) { le->dirblocknr = dirblock_after_update; (void)g; }
static void Touch(struct fileinfo *fi, globaldata *g) { (void)fi; (void)g; touched++; }
static FSIZE GetDEFileSize(void *de, globaldata *g) { (void)de; (void)g; return 0; }
static BOOL UpdateLinks(void *de, globaldata *g) { (void)de; (void)g; return TRUE; }
static void PFSDoNotify(struct fileinfo *fi, BOOL f, globaldata *g) { (void)fi; (void)f; (void)g; }
static void RemoveListEntry(listentry_t *le, globaldata *g)
{
	(void)g;
	removed++;
	removed_volume = le->volume;
}

#include "gen/dd_Close.c"                    /* the REAL function from ../../dd_funcs.c */

static int failures;

static void run(const char *name, int checknotify, int ok, int dirblk, SIPTR want_res, SIPTR want_err)
{
	struct volumedata vol = { FALSE };
	fileentry_t fe = { { &vol, { { 0 } }, { { (void *)8 } }, 0 }, 0, 0 };
	struct DosPacket pkt = { (SIPTR)&fe, 0 };
	globaldata g;
	SIPTR res;

	fe.checknotify = checknotify;
	vol_ok = ok;
	dirblock_after_update = dirblk;
	removed = touched = 0;
	removed_volume = NULL;

	res = dd_Close(&pkt, &g);
	printf("  %-34s res=%d err=%d removed=%d\n", name, (int)res, (int)pkt.dp_Res2, removed);
	if (removed != 1 || removed_volume != &vol)
	{ printf("  FAIL: %s: fileentry not removed exactly once\n", name); failures++; }
	if (res != want_res || (want_res == DOSFALSE && pkt.dp_Res2 != want_err))
	{ printf("  FAIL: %s: wrong result/error\n", name); failures++; }
}

int main(void)
{
	/* ok: 0 = CheckVolume fails, 1 = passes */
	run("write check fails (ghost/ejected)", 1, 0, 0, DOSFALSE, ERROR_DEVICE_NOT_MOUNTED);
	run("flushed dirblock after UpdateLE", 1, 1, 7, DOSFALSE, PFS_ERROR_INCONSISTENT);
	run("read-only close on ghost volume", 0, 0, 0, DOSFALSE, ERROR_DEVICE_NOT_MOUNTED);
	run("normal write close", 1, 1, 0, DOSTRUE, 0);
	run("normal read-only close", 0, 1, 0, DOSTRUE, 0);

	if (failures == 0) { printf("  PASS: every Close path removes the fileentry\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
