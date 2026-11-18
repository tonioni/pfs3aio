/* LIVE @src dd_funcs.c @func dd_CheckCustomPacket @func dd_SetLargeFile
 *
 * Lifts the checkout's real dd_SetLargeFile, the handler side of setlargefile
 * (ACTION_SET_LARGEFILE, large file builds only).
 *  - 0 asks for the state and writes nothing.
 *  - 1 on a PFS\2 volume turns MODE_LARGEFILE on, puts it into use at once
 *    and makes the volume dirty. It stays on.
 *  - 1 on a PFS\1 volume gives ERROR_BAD_NUMBER and the flag stays off.
 *  - Any other value gives ERROR_BAD_NUMBER without touching the volume.
 *  - A volume without a rootblock extension gives ERROR_NO_DISK.
 *  - A disk id other than PFS\2 or AFS\1 in Arg1 is refused.
 *  - Only switching on asks CheckVolume for a writable volume.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef intptr_t SIPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define ERROR_NO_DISK 226			/* dos/dos.h */
#define ERROR_BAD_NUMBER 115
#define ID_PFS_DISK (0x50465301L)	/* pfs3disk.h */
#define ID_PFS2_DISK (0x50465302L)
#define ID_AFS_DISK (0x41465301L)
#define ID_MUPFS_DISK (0x6d755046L)
#define MODE_LARGEFILE 2048

struct DosPacket { SIPTR dp_Arg1, dp_Arg2, dp_Res1, dp_Res2; };
struct crootblockextension { int dummy; };
struct rootblock { ULONG disktype; ULONG options; };
struct volumedata { struct rootblock *rootblk; struct crootblockextension *rblkextension; };
typedef struct globaldata {
	struct volumedata *currentvolume;
	struct rootblock *rootblock;
	BOOL dirty, largefile;
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
#include "gen/dd_SetLargeFile.c"

static struct crootblockextension rext;
static struct rootblock rbl;
static struct volumedata vol;
static globaldata gd;
static struct DosPacket pkt;
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } } while (0)

static void setup(ULONG disktype, BOOL on)
{
	rbl.disktype = disktype;
	rbl.options = on ? MODE_LARGEFILE : 0;
	vol.rootblk = &rbl;
	vol.rblkextension = &rext;
	gd.currentvolume = &vol;
	gd.rootblock = &rbl;
	gd.dirty = FALSE;
	gd.largefile = on;
}

static SIPTR call(ULONG id, SIPTR arg)
{
	checkvolume_calls = checkvolume_write = 0;
	memset(&pkt, 0, sizeof(pkt));
	pkt.dp_Arg1 = id;
	pkt.dp_Arg2 = arg;
	return dd_SetLargeFile(&pkt, &gd);
}

int main(void)
{
	SIPTR r;

	setup(ID_PFS2_DISK, FALSE);
	r = call(ID_PFS2_DISK, 0);
	CHECK(r == DOSTRUE && pkt.dp_Res2 == 0, "PFS\\2, 0: off");
	CHECK(!gd.dirty && checkvolume_calls == 1 && !checkvolume_write, "0: nothing written, read only check");

	r = call(ID_PFS2_DISK, 1);
	CHECK(r == DOSTRUE && pkt.dp_Res2 == 1, "PFS\\2, 1: on");
	CHECK((rbl.options & MODE_LARGEFILE) && gd.largefile && gd.dirty && checkvolume_write, "PFS\\2, 1: MODE_LARGEFILE, in use, dirty, writable check");
	gd.dirty = FALSE;
	r = call(ID_AFS_DISK, 0);
	CHECK(r == DOSTRUE && pkt.dp_Res2 == 1 && !gd.dirty, "asked again with AFS\\1 id: on, nothing written");
	r = call(ID_PFS2_DISK, 1);
	CHECK(r == DOSTRUE && pkt.dp_Res2 == 1, "1 again: stays on");

	setup(ID_PFS_DISK, FALSE);
	r = call(ID_PFS2_DISK, 1);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_BAD_NUMBER, "PFS\\1, 1: ERROR_BAD_NUMBER");
	CHECK(!(rbl.options & MODE_LARGEFILE) && !gd.largefile && !gd.dirty, "PFS\\1, 1: stays off");

	setup(ID_PFS2_DISK, FALSE);
	r = call(ID_PFS2_DISK, 2);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_BAD_NUMBER, "2: ERROR_BAD_NUMBER");
	r = call(ID_PFS2_DISK, -1);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_BAD_NUMBER, "-1: ERROR_BAD_NUMBER");
	CHECK(checkvolume_calls == 0 && !gd.dirty && !rbl.options, "bad value: volume not touched");

	setup(ID_PFS2_DISK, FALSE);
	vol.rblkextension = NULL;
	r = call(ID_PFS2_DISK, 0);
	CHECK(r == DOSFALSE && pkt.dp_Res2 == ERROR_NO_DISK, "no rootblock extension: ERROR_NO_DISK");

	setup(ID_PFS2_DISK, FALSE);
	r = call(ID_MUPFS_DISK, 1);
	CHECK(r == DOSFALSE && !rbl.options && !gd.dirty, "muPF id in Arg1: refused");

	if (fails) { printf("setlargefile_live: %d FAILED\n", fails); return 1; }
	printf("  PASS: dd_SetLargeFile asks, switches on a PFS\\2 volume and refuses PFS\\1, a bad value and an unknown request\n");
	return 0;
}
