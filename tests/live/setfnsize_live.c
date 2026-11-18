/* LIVE @src dd_funcs.c @func dd_CheckCustomPacket @func dd_SetFileSize
 *
 * Lifts the checkout's real dd_SetFileSize, the handler side of setfnsize
 * (ACTION_SET_FNSIZE).
 *  - 0 asks for the current size and writes nothing.
 *  - A size from 30 to 107 and not below the current one goes into the
 *    rootblock extension and into use at once, turns MODE_LONGFN on and
 *    makes the volume dirty.
 *  - A size below 30 or below the current one gives ERROR_BAD_NUMBER, 108
 *    and more ERROR_OBJECT_TOO_LARGE.
 *  - A volume without a rootblock extension gives ERROR_NO_DISK.
 *  - A disk id other than PFS\2 or AFS\1 in Arg1 is refused.
 *  - Only a change asks CheckVolume for a writable volume.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "live.h"
#include "pfs3disk.h"

#define FNSIZE 108
#define FILENAMESIZE (g->fnsize)

struct DosPacket { SIPTR dp_Arg1, dp_Arg2, dp_Res1, dp_Res2; };
struct crootblockextension { struct { UWORD fnsize; } blk; };
struct volumedata { struct crootblockextension *rblkextension; };
typedef struct globaldata {
	struct volumedata *currentvolume;
	struct rootblock *rootblock;
	UWORD fnsize;
	BOOL dirty;
} globaldata;

static int checkvolume_calls, checkvolume_write;
static BOOL CheckVolume(struct volumedata *v, BOOL write, SIPTR *error, globaldata *g)
{
	(void)v; (void)error; (void)g;
	checkvolume_calls++;
	checkvolume_write = write;
	return TRUE;
}

#include "gen/dd_CheckCustomPacket.c"
#include "gen/dd_SetFileSize.c"

static struct crootblockextension rext;
static struct volumedata vol;
static struct rootblock rbl;
static globaldata gd;
static struct DosPacket pkt;

static SIPTR call(ULONG id, SIPTR size, int fnsize)
{
	rext.blk.fnsize = fnsize;
	vol.rblkextension = &rext;
	rbl.options = 0;
	gd.currentvolume = &vol;
	gd.rootblock = &rbl;
	gd.fnsize = fnsize;
	gd.dirty = FALSE;
	checkvolume_calls = checkvolume_write = 0;
	memset(&pkt, 0, sizeof(pkt));
	pkt.dp_Arg1 = id;
	pkt.dp_Arg2 = size;
	return dd_SetFileSize(&pkt, &gd);
}

int main(void)
{
	SIPTR r;

	r = call(ID_PFS2_DISK, 0, 32);
	CHECK(r == DOSTRUE && pkt.dp_Res2 == 32, "0: current size 32");
	CHECK(!gd.dirty && !rbl.options && checkvolume_calls == 1 && !checkvolume_write, "0: nothing written, read only check");

	r = call(ID_PFS2_DISK, 107, 32);
	CHECK(r == DOSTRUE && pkt.dp_Res2 == 107, "107: set");
	CHECK(gd.fnsize == 107 && rext.blk.fnsize == 107, "107: in use and in the rootblock extension");
	CHECK((rbl.options & MODE_LONGFN) && gd.dirty && checkvolume_write, "107: MODE_LONGFN, dirty, writable check");

	r = call(ID_AFS_DISK, 30, 30);
	CHECK(r == DOSTRUE && gd.fnsize == 30, "30 with AFS\\1 id, current 30: set");

	r = call(ID_PFS2_DISK, 29, 0);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_BAD_NUMBER, "29: ERROR_BAD_NUMBER");
	r = call(ID_PFS2_DISK, 40, 50);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_BAD_NUMBER && gd.fnsize == 50, "40 below current 50: ERROR_BAD_NUMBER, size stays");
	r = call(ID_PFS2_DISK, 108, 32);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_OBJECT_TOO_LARGE && gd.fnsize == 32, "108: ERROR_OBJECT_TOO_LARGE, size stays");
	CHECK(checkvolume_calls == 0 && !gd.dirty, "refused size: volume not touched");

	call(ID_PFS2_DISK, 0, 32);
	vol.rblkextension = NULL;
	r = dd_SetFileSize(&pkt, &gd);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_NO_DISK, "no rootblock extension: ERROR_NO_DISK");

	r = call(ID_PFS_DISK, 107, 32);
	CHECK(r == DOSFALSE && gd.fnsize == 32 && !gd.dirty, "PFS\\1 id in Arg1: refused");

	if (fails) { printf("setfnsize_live: %d FAILED\n", fails); return 1; }
	printf("  PASS: dd_SetFileSize asks, sets 30 to 107 and refuses a smaller, too large or unknown request\n");
	return 0;
}
