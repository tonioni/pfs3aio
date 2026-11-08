/* LIVE @src dd_funcs.c @func dd_OpenFromLock
 *
 * Lifts the checkout's real dd_OpenFromLock (ACTION_FH_FROM_LOCK). dos.library
 * keeps the lock when OpenFromLock fails, so the packet must not destroy the lock
 * before it knows the new fileentry could be added:
 *
 *  - success (exclusive and shared lock): the new fileentry is added, THEN the
 *    old lock entry is removed (order checked), the filehandle points at the new
 *    entry, and the entry type follows fl_Access;
 *  - AddListEntry (mock with the real conflict rule) sees the lock's anodenr as 0
 *    while it checks, otherwise an exclusive lock would conflict with itself; a
 *    stale entry on the same anode is still seen (it is not hidden), and the
 *    lock's anodenr is restored afterwards on the success and failure path;
 *  - AddListEntry fails: the lock is STILL on the volume, unchanged (anodenr
 *    restored), RemoveListEntry is never called, the new entry is freed exactly
 *    once (FreeListEntry), the filehandle is not touched, the error is
 *    ERROR_OBJECT_IN_USE;
 *  - the early refusals (no lock, bad volume, flushed dirblock, not a file,
 *    MakeListEntry fails) add and remove nothing.
 *
 * A tree that removes the lock first loses it when the add fails; a tree that
 * does not hide the lock fails to open an exclusive lock and shows a non-zero
 * anodenr at add time; a tree that forgets to restore leaves anodenr at 0.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef intptr_t SIPTR; typedef int32_t LONG; typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef int BOOL; typedef intptr_t BPTR; typedef uintptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define SHARED_LOCK (-2)
#define EXCLUSIVE_LOCK (-1)
#define ST_USERDIR 2
#define ST_FILE (-3)
#define SPECIAL_FLUSHED 3
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_WRONG_TYPE 212
#define PFS_ERROR_INCONSISTENT 213      /* ERROR_DISK_NOT_VALIDATED */
#define ERROR_DEVICE_NOT_MOUNTED 218

#define BADDR(x) ((void *)((intptr_t)(x) << 2))
#define MKBADDR(x) ((BPTR)((intptr_t)(x) >> 2))

/* struct.h: entry types. The host allocates bitfields from the LSB, m68k from
 * the MSB: declared in reverse so that .value means the same as on the Amiga. */
#define ET_FILEENTRY 0x0008
#define ETF_FILEENTRY 2
#define ETF_LOCK 3
#define ET_SHAREDREAD 0
#define ET_SHAREDWRITE 1
#define ET_EXCLWRITE 3
union listtype
{
	struct { unsigned access:2; unsigned type:2; unsigned dir:1; unsigned pad:11; } flags;
	UWORD value;
};
typedef union listtype listtype;
#define SHAREDLOCK(t) ((t).flags.access <= 1)

struct DosPacket { SIPTR dp_Arg1; SIPTR dp_Arg2; SIPTR dp_Res2; };
struct FileHandle { SIPTR fh_Arg1; };
struct FileLock { BPTR fl_Link; LONG fl_Key; LONG fl_Access; void *fl_Task; BPTR fl_Volume; };
struct direntry { UBYTE next; BYTE type; ULONG anode; };
struct fileinfo { struct direntry *direntry; void *dirblock; };
union objectinfo { struct fileinfo file; };
struct volumedata { int dummy; };

typedef struct listentry
{
	struct listentry *next;
	struct listentry *prev;
	struct FileLock lock;
	listtype type;
	ULONG anodenr;
	ULONG diranodenr;
	union objectinfo info;
	ULONG dirblocknr;
	ULONG dirblockoffset;
	struct volumedata *volume;
	UBYTE sleepref;
} listentry_t;

#define IsFile(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type)<=0)
#define ListEntryFromLock(x) ((x) ? \
 (listentry_t *)((UBYTE*)BADDR(x)-offsetof(listentry_t, lock)) : 0)
#define AddListEntry(a) _AddListEntry(a,g)

typedef struct globaldata { int dummy; } globaldata;

/* the mocked lock module: one volume with its entries in a small array */
static struct volumedata vol;
static listentry_t *vol_entries[16];
static int nvol;
static int seq, add_seq, remove_seq, add_calls, remove_calls, nfreed;
static listentry_t *freed[8], *removed, *made;
static listentry_t *the_lock;
static ULONG anodenr_seen_at_add, anodenr_seen_at_remove;
static UWORD made_type;
static int vol_ok, upd_dirblock, make_fails;

static BOOL CheckVolume(struct volumedata *v, BOOL write, SIPTR *err, globaldata *g)
{
	(void)v; (void)write; (void)g;
	if (vol_ok) return TRUE;
	*err = ERROR_DEVICE_NOT_MOUNTED;
	return FALSE;
}
static void UpdateLE(listentry_t *le, globaldata *g) { (void)g; le->dirblocknr = upd_dirblock; }

static listentry_t *MakeListEntry(union objectinfo *info, listtype type, SIPTR *error, globaldata *g)
{
	(void)g;
	made_type = type.value;
	if (make_fails) { *error = ERROR_NO_FREE_STORE; return NULL; }
	made = calloc(1, sizeof(listentry_t));
	made->type = type;
	made->anodenr = info->file.direntry->anode;
	made->info = *info;
	made->volume = &vol;
	return made;
}

/* AccessConflict() of lock.c: entries on the same anode conflict when either
 * is exclusive or both are shared writers */
static BOOL _AddListEntry(listentry_t *e, globaldata *g)
{
	int i;
	(void)g;
	add_calls++; add_seq = ++seq;
	anodenr_seen_at_add = the_lock->anodenr;
	for (i = 0; i < nvol; i++)
	{
		listentry_t *fe = vol_entries[i];
		if (fe->anodenr != e->anodenr) continue;
		if (!SHAREDLOCK(fe->type) || !SHAREDLOCK(e->type)) return DOSFALSE;
		if (fe->type.flags.access == ET_SHAREDWRITE && e->type.flags.access == ET_SHAREDWRITE) return DOSFALSE;
	}
	vol_entries[nvol++] = e;
	return DOSTRUE;
}
static void RemoveListEntry(listentry_t *e, globaldata *g)
{
	int i, j;
	(void)g;
	remove_calls++; remove_seq = ++seq;
	removed = e; anodenr_seen_at_remove = e->anodenr;
	for (i = j = 0; i < nvol; i++) if (vol_entries[i] != e) vol_entries[j++] = vol_entries[i];
	nvol = j;
}
static void FreeListEntry(listentry_t *e, globaldata *g) { (void)g; freed[nfreed++] = e; }

#include "gen/dd_OpenFromLock.c"             /* the REAL function from ../../dd_funcs.c */

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

#define OLD_ANODE 77
#define SENTINEL 0x1234

static struct direntry file_de = { 0, ST_FILE, OLD_ANODE };
static struct direntry dir_de = { 0, ST_USERDIR, OLD_ANODE };
static struct FileHandle fh;
static struct DosPacket pkt;
static globaldata gd;

static listentry_t *new_entry(LONG fl_access, UWORD access, ULONG anodenr, struct direntry *de)
{
	listentry_t *e = calloc(1, sizeof(listentry_t));
	e->type.flags.type = ETF_LOCK;
	e->type.flags.access = access;
	e->lock.fl_Access = fl_access;
	e->anodenr = anodenr;
	e->info.file.direntry = de;
	e->volume = &vol;
	return e;
}

/* fresh volume holding one lock on the file (anode OLD_ANODE) */
static listentry_t *setup(LONG fl_access, UWORD access, struct direntry *de)
{
	nvol = nfreed = add_calls = remove_calls = seq = add_seq = remove_seq = 0;
	removed = made = NULL; made_type = 0; anodenr_seen_at_add = anodenr_seen_at_remove = 0xdead;
	vol_ok = 1; upd_dirblock = 0; make_fails = 0;
	the_lock = new_entry(fl_access, access, OLD_ANODE, de);
	vol_entries[nvol++] = the_lock;
	fh.fh_Arg1 = SENTINEL;
	pkt.dp_Arg1 = MKBADDR(&fh);
	pkt.dp_Arg2 = MKBADDR(&the_lock->lock);
	pkt.dp_Res2 = 0;
	return the_lock;
}

static int on_volume(listentry_t *e)
{
	int i;
	for (i = 0; i < nvol; i++) if (vol_entries[i] == e) return 1;
	return 0;
}

static void success_case(const char *name, LONG fl_access, UWORD access, UWORD want_type)
{
	listentry_t *lock = setup(fl_access, access, &file_de);
	SIPTR res = dd_OpenFromLock(&pkt, &gd);

	printf("  %-24s res=%d type=%#x add@%d remove@%d anode@add=%u anode@remove=%u\n", name, (int)res,
		made_type, add_seq, remove_seq, (unsigned)anodenr_seen_at_add, (unsigned)anodenr_seen_at_remove);
	check(res == DOSTRUE, "the open must succeed");
	check(made_type == want_type, "the new entry type must follow fl_Access");
	check(made && fh.fh_Arg1 == (SIPTR)made, "the filehandle must point at the new fileentry");
	check(add_calls == 1 && remove_calls == 1 && removed == lock, "one add, and the lock is the entry removed");
	check(add_seq < remove_seq, "the new entry must be added BEFORE the lock is removed");
	check(anodenr_seen_at_add == 0, "AddListEntry must see the lock's anodenr as 0 (hidden from the conflict check)");
	check(anodenr_seen_at_remove == OLD_ANODE, "the lock's anodenr must be restored before it is removed");
	check(on_volume(made) && !on_volume(lock) && nvol == 1, "the volume must end up holding just the new entry");
	check(nfreed == 0, "a successful open frees nothing itself");
}

static void failed_add_case(const char *name, LONG fl_access, UWORD access)
{
	listentry_t *lock = setup(fl_access, access, &file_de);
	/* an unrelated, still existing exclusive entry on the same anode: a real conflict */
	listentry_t *stale = new_entry(EXCLUSIVE_LOCK, ET_EXCLWRITE, OLD_ANODE, &file_de);
	SIPTR res;
	vol_entries[nvol++] = stale;

	res = dd_OpenFromLock(&pkt, &gd);
	printf("  %-24s res=%d err=%d removes=%d freed=%d lock anode=%u\n", name, (int)res, (int)pkt.dp_Res2,
		remove_calls, nfreed, (unsigned)lock->anodenr);
	check(res == DOSFALSE && pkt.dp_Res2 == ERROR_OBJECT_IN_USE, "a conflicting add must fail with ERROR_OBJECT_IN_USE");
	check(add_calls == 1 && anodenr_seen_at_add == 0, "the add must have been attempted with the lock hidden");
	check(remove_calls == 0 && on_volume(lock) && on_volume(stale) && nvol == 2,
		"the old lock must STILL be on the volume (dos.library keeps it)");
	check(lock->anodenr == OLD_ANODE, "the lock's anodenr must be restored after a failed add");
	check(lock->lock.fl_Access == fl_access && lock->type.flags.access == access, "the lock must be unchanged");
	check(nfreed == 1 && freed[0] == made && !on_volume(made), "the new entry must be freed exactly once and not stay on the volume");
	check(fh.fh_Arg1 == SENTINEL, "the filehandle must not be touched");
}

static void refused(const char *name, SIPTR want_err)
{
	SIPTR res = dd_OpenFromLock(&pkt, &gd);
	printf("  %-24s res=%d err=%d\n", name, (int)res, (int)pkt.dp_Res2);
	check(res == DOSFALSE && pkt.dp_Res2 == want_err, name);
	check(add_calls == 0 && remove_calls == 0 && nfreed == 0, "a refused open must add, remove and free nothing");
	check(nvol >= 1 && on_volume(the_lock) && the_lock->anodenr == OLD_ANODE, "a refused open must leave the lock alone");
	check(fh.fh_Arg1 == SENTINEL, "a refused open must not touch the filehandle");
}

int main(void)
{
	listentry_t *shared2;

	setvbuf(stdout, NULL, _IONBF, 0);

	success_case("exclusive lock", EXCLUSIVE_LOCK, ET_EXCLWRITE, ET_FILEENTRY | ET_EXCLWRITE);
	success_case("shared lock", SHARED_LOCK, ET_SHAREDREAD, ET_FILEENTRY | ET_SHAREDREAD);

	/* another shared lock on the same file belongs to someone else and stays */
	setup(SHARED_LOCK, ET_SHAREDREAD, &file_de);
	shared2 = new_entry(SHARED_LOCK, ET_SHAREDREAD, OLD_ANODE, &file_de);
	vol_entries[nvol++] = shared2;
	{
		SIPTR res = dd_OpenFromLock(&pkt, &gd);
		printf("  other shared lock stays:       res=%d volume entries=%d\n", (int)res, nvol);
		check(res == DOSTRUE && removed == the_lock, "the opened lock is the one removed");
		check(on_volume(shared2) && shared2->anodenr == OLD_ANODE && nvol == 2, "an unrelated lock must stay, untouched");
	}

	failed_add_case("add fails (shared)", SHARED_LOCK, ET_SHAREDREAD);
	failed_add_case("add fails (exclusive)", EXCLUSIVE_LOCK, ET_EXCLWRITE);

	setup(SHARED_LOCK, ET_SHAREDREAD, &file_de); pkt.dp_Arg2 = 0;
	{
		SIPTR res = dd_OpenFromLock(&pkt, &gd);
		printf("  no lock:                       res=%d\n", (int)res);
		check(res == DOSFALSE && add_calls == 0 && remove_calls == 0, "a NULL lock must be refused without touching the volume");
	}
	setup(SHARED_LOCK, ET_SHAREDREAD, &file_de); vol_ok = 0;
	refused("volume not mounted", ERROR_DEVICE_NOT_MOUNTED);
	setup(SHARED_LOCK, ET_SHAREDREAD, &file_de); upd_dirblock = 5;
	refused("flushed dirblock", PFS_ERROR_INCONSISTENT);
	setup(SHARED_LOCK, ET_SHAREDREAD, &dir_de);
	refused("lock on a directory", ERROR_OBJECT_WRONG_TYPE);
	setup(SHARED_LOCK, ET_SHAREDREAD, &file_de); make_fails = 1;
	refused("MakeListEntry fails", ERROR_NO_FREE_STORE);

	if (failures == 0) { printf("  PASS: OpenFromLock adds the new entry first and keeps the lock when that fails\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
