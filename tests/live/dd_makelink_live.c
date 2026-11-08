/* LIVE @src dd_funcs.c @func dd_MakeLink
 *
 * Lifts the checkout's real dd_MakeLink and drives it with every kind of hard
 * link target. Only a file or a real directory has a direntry a link can point
 * at: a volume lock, a soft link, a link to a directory and a deldir file must
 * be refused with ERROR_OBJECT_WRONG_TYPE before CreateLink is called, while a
 * file, a real directory and a file link (type <= 0) must still reach it.
 * A valid soft link is checked as the neighbour that must keep working.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef intptr_t SIPTR; typedef int32_t LONG; typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef int BOOL; typedef intptr_t BPTR; typedef uintptr_t IPTR;
typedef UBYTE *DSTR; typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define MULTIUSER 0
#define DELDIR 1
#define DB(x)
#define LINK_HARD 0
#define LINK_SOFT 1
#define ST_USERDIR 2
#define ST_SOFTLINK 3
#define ST_LINKDIR 4
#define ST_FILE (-3)
#define ST_LINKFILE (-4)
#define SPECIAL_DELFILE 2
#define SPECIAL_FLUSHED 3
#define PATHSIZE 256
#define ID_AFS_DISK (0x41465301L)
#define ID_PFS2_DISK (0x50465302L)
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_WRONG_TYPE 212
#define PFS_ERROR_INCONSISTENT 213      /* ERROR_DISK_NOT_VALIDATED */
#define ERROR_NO_DISK 226

#define BADDR(x) ((void *)((intptr_t)(x) << 2))
#define MKBADDR(x) ((BPTR)((intptr_t)(x) >> 2))
#define BARG2(p) BADDR(p->dp_Arg2)

/* struct.h: entry types; the host allocates bitfields from the LSB, m68k from
 * the MSB, so they are declared in reverse to keep .value meaning the same */
#define ETF_VOLUME 1
#define ETF_FILEENTRY 2
#define ETF_LOCK 3
union listtype
{
	struct { unsigned access:2; unsigned type:2; unsigned dir:1; unsigned pad:11; } flags;
	UWORD value;
};
typedef union listtype listtype;
#define IsVolumeEntry(e) ((e)->type.flags.type == ETF_VOLUME)
#define IsFileEntry(e) ((e)->type.flags.type == ETF_FILEENTRY)

struct DosPacket { SIPTR dp_Arg1, dp_Arg2, dp_Arg3, dp_Arg4, dp_Arg5, dp_Res2; };
struct FileLock { BPTR fl_Link; LONG fl_Key; LONG fl_Access; void *fl_Task; BPTR fl_Volume; };
struct DeviceList { ULONG dummy; };
struct direntry { UBYTE next; BYTE type; ULONG anode; };
struct cdirblock { struct { ULONG anodenr; } blk; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct volumedata;
struct volumeinfo { ULONG root; struct volumedata *volume; };
struct deldirinfo { ULONG special; struct volumedata *volume; };
union objectinfo { struct fileinfo file; struct volumeinfo volume; struct deldirinfo deldir; };
struct volumedata { struct DeviceList *devlist; };

typedef struct listentry
{
	struct listentry *next, *prev;
	struct FileLock lock;
	listtype type;
	ULONG anodenr, diranodenr;
	union objectinfo info;
	ULONG dirblocknr, dirblockoffset;
	struct volumedata *volume;
	UBYTE sleepref;
} listentry_t;
typedef struct { listentry_t le; } fileentry_t;
typedef struct { listentry_t le; ULONG nextanode; } lockentry_t;

#define IsFile(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type)<=0)
#define IsRealDir(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type==ST_USERDIR))
#define IsDelFile(oi) ((oi).deldir.special==SPECIAL_DELFILE)
#define LockEntryFromLock(x) ((x) ? \
 (lockentry_t *)((UBYTE*)BADDR(x)-offsetof(listentry_t, lock)) : 0)
static int lookups;              /* counts how often a lock is turned into an entry */
#define ListEntryFromLock(x) (lookups++, (x) ? \
 (listentry_t *)((UBYTE*)BADDR(x)-offsetof(listentry_t, lock)) : 0)

typedef struct globaldata { struct volumedata *currentvolume; } globaldata;

#define SkipColon(filename,pathname)                                \
do {                                                                   \
	if ((filename = strchr ((pathname), ':')))                       \
		filename++;                                                 \
	else                                                            \
		filename=(pathname);                                        \
} while (0)

#define GetFileInfoFromLock(argument, access, fe, fi)               \
do {                                                                   \
	if ((fe = LockEntryFromLock (argument)))                        \
	{                                                               \
		if (!CheckVolume ((fe)->le.volume, access, error, g))         \
			return DOSFALSE;                                        \
		UpdateLE ((listentry_t *)(fe), g);                          \
		if ((fe)->le.dirblocknr)                                    \
		{                                                           \
			*error = PFS_ERROR_INCONSISTENT;                        \
			return DOSFALSE;                                        \
		}                                                           \
		fi = &(fe)->le.info;                                        \
	}                                                               \
	else                                                            \
	{                                                               \
		if (!g->currentvolume)                                      \
		{                                                           \
			*error = ERROR_NO_DISK;                                 \
			return DOSFALSE;                                        \
		}                                                           \
		fi = NULL;                                                  \
	}                                                               \
} while (0)

/* ---- mocks: everything below the handlers just records ---- */
static int cv_calls, ule_calls, gfp_calls, csl_calls, cl_calls;
static int notify_calls, updnotify_calls;
static SIPTR cv_ok_err;
static struct { STRPTR name, target; union objectinfo *obj; } mk;
static struct { ULONG dirnode, anode; } un;
static struct cdirblock linkblock = { { 555 } };
static char last_fullname[64];       /* the name lives on the handler's stack: copy it */

static BOOL CheckVolume(struct volumedata *v, BOOL write, SIPTR *err, globaldata *g)
{
	(void)v; (void)write; (void)g;
	cv_calls++;
	if (cv_ok_err) { *err = cv_ok_err; return FALSE; }
	return TRUE;
}
static void UpdateLE(listentry_t *le, globaldata *g) { (void)le; (void)g; ule_calls++; }
static UBYTE *BCPLtoCString(STRPTR dest, ULONG capacity, DSTR src)
{
	UWORD len = *src, i;
	if (len >= capacity) { *dest = 0; return NULL; }
	for (i = 0; i < len; i++) dest[i] = src[i + 1];
	dest[len] = 0;
	return (UBYTE *)dest;
}
static UBYTE *GetFullPath(union objectinfo *parent, STRPTR fullname, union objectinfo *result, SIPTR *error, globaldata *g)
{
	(void)parent; (void)result; (void)error; (void)g;
	gfp_calls++; strncpy(last_fullname, fullname, sizeof(last_fullname) - 1);
	return (UBYTE *)fullname;
}
static BOOL CreateSoftLink(union objectinfo *dir, STRPTR name, STRPTR target, union objectinfo *result, SIPTR *error, globaldata *g)
{
	(void)dir; (void)error; (void)g;
	csl_calls++; mk.name = name; mk.target = target;
	result->file.dirblock = &linkblock;
	return TRUE;
}
static BOOL CreateLink(union objectinfo *dir, STRPTR name, union objectinfo *obj, union objectinfo *result, SIPTR *error, globaldata *g)
{
	(void)dir; (void)error; (void)g;
	cl_calls++; mk.name = name; mk.obj = obj;
	/* a volume or delfile has no real direntry to link to: a real CreateLink could not succeed either */
	if ((uintptr_t)obj->file.direntry <= SPECIAL_FLUSHED)
		return FALSE;
	result->file.dirblock = &linkblock;
	return TRUE;
}
static void PFSDoNotify(struct fileinfo *object, BOOL checkparent, globaldata *g) { (void)object; (void)checkparent; (void)g; notify_calls++; }
static void PFSUpdateNotify(ULONG dirnode, DSTR filename, ULONG anodenr, globaldata *g)
{
	(void)filename; (void)g;
	updnotify_calls++; un.dirnode = dirnode; un.anode = anodenr;
}

#include "gen/dd_MakeLink.c"                 /* the REAL function from ../../dd_funcs.c */

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct DeviceList devlist;
static struct volumedata vol = { &devlist };
static globaldata gd;
static struct DosPacket pkt;

static void reset(void)
{
	cv_calls = ule_calls = gfp_calls = csl_calls = cl_calls = 0;
	notify_calls = updnotify_calls = lookups = 0;
	cv_ok_err = 0; last_fullname[0] = 0;
	memset(&mk, 0, sizeof(mk));
	memset(&pkt, 0, sizeof(pkt));
	gd.currentvolume = &vol;
}

static listentry_t *new_entry(UWORD etype, struct direntry *de)
{
	listentry_t *e = calloc(1, sizeof(lockentry_t));
	e->type.flags.type = etype;
	e->info.file.direntry = de;
	e->volume = &vol;
	e->lock.fl_Volume = MKBADDR(&devlist);
	return e;
}

/* ---- dd_MakeLink ---- */
static UBYTE linkname[16] = { 4, 'l', 'i', 'n', 'k' };
static void make_link(SIPTR target, SIPTR type)
{
	reset();
	pkt.dp_Arg2 = MKBADDR(linkname);
	pkt.dp_Arg3 = target; pkt.dp_Arg4 = type;
}

static void test_makelink(void)
{
	static struct direntry file_de = { 0, ST_FILE, 4001 }, dir_de = { 0, ST_USERDIR, 4002 };
	static struct direntry soft_de = { 0, ST_SOFTLINK, 4003 }, ldir_de = { 0, ST_LINKDIR, 4004 };
	static struct direntry lfile_de = { 0, ST_LINKFILE, 4005 };
	listentry_t *file = new_entry(ETF_LOCK, &file_de), *dir = new_entry(ETF_LOCK, &dir_de);
	listentry_t *vollock = new_entry(ETF_VOLUME, NULL), *soft = new_entry(ETF_LOCK, &soft_de);
	listentry_t *ldir = new_entry(ETF_LOCK, &ldir_de), *delf = new_entry(ETF_LOCK, (struct direntry *)(IPTR)SPECIAL_DELFILE);
	listentry_t *lfile = new_entry(ETF_LOCK, &lfile_de);
	SIPTR res;
	struct { const char *name; listentry_t *le; } bad[] = {
		{ "volume lock", vollock }, { "soft link", soft }, { "link to a dir", ldir }, { "deldir file", delf },
	};
	int i;

	make_link((SIPTR)"target", LINK_SOFT);
	res = dd_MakeLink(&pkt, &gd);
	printf("  soft link, valid:        res=%d softlinks=%d notify=%d\n", (int)res, csl_calls, notify_calls);
	check(res == TRUE && csl_calls == 1 && mk.target && strcmp(mk.target, "target") == 0, "a valid soft link must reach CreateSoftLink");
	check(gfp_calls == 1 && strcmp(last_fullname, "link") == 0 && notify_calls == 1, "the name is resolved and the link notified");

	make_link(MKBADDR(&file->lock), LINK_HARD);
	res = dd_MakeLink(&pkt, &gd);
	printf("  hard link to a file:     res=%d createlink=%d\n", (int)res, cl_calls);
	check(res == TRUE && cl_calls == 1 && mk.obj == &file->info, "a file target must reach CreateLink with its objectinfo");
	check(updnotify_calls == 1 && un.dirnode == 555 && un.anode == 4001 && notify_calls == 1, "the link is announced with the target's anode");

	make_link(MKBADDR(&dir->lock), LINK_HARD);
	res = dd_MakeLink(&pkt, &gd);
	printf("  hard link to a real dir: res=%d createlink=%d\n", (int)res, cl_calls);
	check(res == TRUE && cl_calls == 1 && mk.obj == &dir->info, "a real directory target must reach CreateLink");

	make_link(MKBADDR(&lfile->lock), LINK_HARD);
	res = dd_MakeLink(&pkt, &gd);
	check(res == TRUE && cl_calls == 1, "a hard link to a file link (type <= 0) is a file and reaches CreateLink");

	for (i = 0; i < 4; i++)
	{
		make_link(MKBADDR(&bad[i].le->lock), LINK_HARD);
		res = dd_MakeLink(&pkt, &gd);
		printf("  hard link to %-12s res=%d err=%d createlink=%d\n", bad[i].name, (int)res, (int)pkt.dp_Res2, cl_calls);
		check(res == DOSFALSE && pkt.dp_Res2 == ERROR_OBJECT_WRONG_TYPE, bad[i].name);
		check(cl_calls == 0 && notify_calls == 0 && updnotify_calls == 0, "CreateLink must not be called for a target that is no file or real dir");
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	test_makelink();

	if (failures == 0) { printf("  PASS: hard links only accept a file or a real directory as target\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
