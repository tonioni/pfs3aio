/* LIVE @src directory.c @func DeleteDir @func KillEmpty @func NewDir @src anodes.c @func GetNextAnode
 *
 * Lifts the checkout's real DeleteDir, KillEmpty and NewDir.
 *
 * KillEmpty (removes the empty directory found under the empty name):
 *  - only a REAL directory is deleted. A hard link to a directory, a soft
 *    link and a file under that name are left alone: DeleteDir (DirIsEmpty,
 *    FreeAnode, the directory entry) is not reached;
 *  - the search starts at ANODE_ROOTDIR for the volume, else at the
 *    directory's anode; nothing found / unreadable: DOSFALSE.
 *
 * DeleteDir:
 *  - a chain of any length is deleted: every anode and its block is freed,
 *    also for 65535 anodes and more;
 *  - a cyclic chain (corrupt disk), also a long tail into a cycle, ends with
 *    PFS_ERROR_INCONSISTENT (GetNextAnode, Brent) and nothing is freed.
 *
 * NewDir, when AddListEntry fails (it "cannot" -- AFS_ERROR_NEWDIR_ADDLISTENTRY):
 *  - returns NULL with *error == ERROR_OBJECT_IN_USE (not a stale value);
 *  - undoes the entry: the new anode is freed, the directory entry removed and
 *    the list entry FREED (FreeListEntry, not RemoveListEntry -- it was never
 *    added); no reserved block was allocated yet;
 *  - the other failure paths keep their own errors and cleanup: no free
 *    reserved block -> DISK_FULL, no dirblock memory -> NO_FREE_STORE (both
 *    after the list entry was added, so it is REMOVED), the success path
 *    releases nothing.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef intptr_t SIPTR; typedef intptr_t IPTR;
typedef int BOOL; typedef char *STRPTR; typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define DELDIR 1
#define ANODE_ROOTDIR 5			/* blocks.h */
#define FNSIZE 108
#define CMSIZE 80
#define MAX_ENTRYSIZE (sizeof(struct direntry) + FNSIZE + CMSIZE + 34)
#define RTBF_THRESHOLD 256		/* struct.h */
#define HASHM_DIR 0x1f
#define ET_LOCK 0x000c
#define ET_EXCLREAD 2
#define ENTER(x)
#define DB(x)
#define ST_USERDIR 2			/* dos/dosextens.h */
#define ST_SOFTLINK 3
#define ST_LINKDIR 4
#define ST_FILE (-3)
#define ERROR_NO_FREE_STORE 103		/* dos/dos.h */
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_EXISTS 203
#define ERROR_INVALID_COMPONENT_NAME 210
#define ERROR_DISK_NOT_VALIDATED 213
#define ERROR_DISK_FULL 221
#define ERROR_WRITE_PROTECTED 223
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED		/* struct.h */
#define SPECIAL_FLUSHED 3
#define SPECIAL_DELDIR 1

#pragma pack(push, 2)
struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection; UBYTE nlength; UBYTE startofname; UBYTE pad;
};
#pragma pack(pop)
struct cdirblock { ULONG blocknr, oldblocknr; UBYTE changeflag; };
struct cachedblock;
typedef struct canode { ULONG clustersize, blocknr, next, nr; } canode_t;
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct volumeinfo { ULONG root; struct volumedata *volume; };
struct deldirinfo { ULONG special; struct volumedata *volume; };
union objectinfo { struct fileinfo file; struct volumeinfo volume; struct deldirinfo deldir; };
struct volumedata { int metadata_error; void *dirblks; };
union listtype { struct { unsigned pad:11, dir:1, type:2, access:2; } flags; UWORD value; };
typedef union listtype listtype;
typedef struct listentry { int dummy; } listentry_t;
typedef struct lockentry { listentry_t le; } lockentry_t;
typedef struct globaldata { struct volumedata *currentvolume; int fnsize; } globaldata;

#define FIANODENR(fi) ((fi)->direntry->anode)
#define IsRealDir(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type==ST_USERDIR))
#define IsDir(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type)>0)
#define IsVolume(oi) ((oi).volume.root==0)
#define IsDelDir(oi) ((oi).deldir.special==SPECIAL_DELDIR)
#define FILENAMESIZE (g->fnsize)
static struct { ULONG rtbf_index; int res_alert; } alloc_data;
#define ReservedAreaIsLocked (alloc_data.res_alert)
#define CheckCache(list, mask, blk, g) ((void *)0)	/* no dirblock of the directory is cached */
#define MinRemove(n) ((void)0)
#define FreeLRU(b) ((void)0)
#define AddListEntry(a) _AddListEntry(a, g)

/* ---- recorders ---- */
static int dirisempty_calls, changedirentry_calls, updatedisk_calls, restobefreed_calls, freeanode_calls;
static ULONG dirisempty_arg, freed_first, freed_last, rtbf_first, rtbf_last;
static ULONG si_dirnodenr; static char si_name[16];
static int getanode_calls, runaway;

/* ---- anode table of DeleteDir: tbl_next[nr] / block nr + 1000 ---- */
#define NTAB 140000
static ULONG tbl_next[NTAB]; static int tbl_ok[NTAB];
static BOOL GetAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	if (++getanode_calls > 1000000) runaway = 1;
	if (runaway || nr == 0 || nr >= NTAB || !tbl_ok[nr]) {
		a->clustersize = a->next = 0; a->blocknr = ~0UL; a->nr = nr;
		return FALSE;
	}
	a->clustersize = 1; a->blocknr = nr + 1000; a->next = tbl_next[nr]; a->nr = nr;
	return TRUE;
}
static BOOL FreeAnode(ULONG nr, globaldata *g)
{
	(void)g;
	if (!freeanode_calls++) freed_first = nr;
	freed_last = nr;
	return TRUE;
}
static void ResToBeFreed(ULONG blocknr, globaldata *g)
{
	(void)g;
	if (!restobefreed_calls++) rtbf_first = blocknr;
	rtbf_last = blocknr;
}
static BOOL UpdateDisk(globaldata *g) { (void)g; updatedisk_calls++; return TRUE; }
static BOOL ChangeDirEntry(struct fileinfo from, struct direntry *to, union objectinfo *d, struct fileinfo *r, globaldata *g)
{
	(void)from; (void)to; (void)d; (void)r; (void)g;
	changedirentry_calls++;
	return TRUE;
}
static int dir_empty;
static BOOL DirIsEmpty(ULONG nr, SIPTR *error, globaldata *g)
{
	(void)error; (void)g;
	dirisempty_calls++; dirisempty_arg = nr;
	return dir_empty;
}

/* SearchInDir: what a lookup of the empty name / the new name yields */
static LONG si_result;
static struct direntry found_de;
static struct cdirblock found_blk;
static LONG SearchInDir(ULONG dirnodenr, STRPTR name, union objectinfo *info, globaldata *g)
{
	(void)g;
	si_dirnodenr = dirnodenr; strncpy(si_name, name, sizeof(si_name) - 1);
	info->file.direntry = &found_de; info->file.dirblock = &found_blk;
	return si_result;
}

/* ---- NewDir's collaborators ---- */
static int cv_ok, makeentry_ok, adddirent_ok, makelist_ok, addlist_ok, allocres_ok, makeblk_ok;
static int removedirentry_calls, removelist_calls, freelist_calls, allocres_calls, makeblk_calls,
	freeres_calls, errormsg_calls, adddirent_calls;
static ULONG makeblk_args[4], freeres_arg, freed_by_newdir;
static struct direntry newde;
static struct cdirblock newblk;
static listentry_t fe, blk_dummy;
static struct cdirblock dirblk;
#define NEW_ANODE 4242
#define RES_BLOCK 777
static const UBYTE AFS_ERROR_NEWDIR_ADDLISTENTRY[] = "newdir addlistentry";
static const UBYTE *last_msg;

static BOOL CheckVolume(struct volumedata *v, BOOL w, SIPTR *e, globaldata *g) { (void)v; (void)w; (void)e; (void)g; return cv_ok; }
static BOOL MakeDirEntry(BYTE type, UBYTE *name, UBYTE *buf, globaldata *g)
{
	(void)name; (void)g;
	if (!makeentry_ok) return FALSE;
	memset(buf, 0, sizeof(struct direntry));
	((struct direntry *)buf)->type = type; ((struct direntry *)buf)->anode = NEW_ANODE;
	return TRUE;
}
static BOOL AddDirectoryEntry(union objectinfo *d, struct direntry *e, struct fileinfo *r, globaldata *g)
{
	(void)d; (void)g;
	adddirent_calls++;
	if (!adddirent_ok) return FALSE;
	newde = *e;
	r->direntry = &newde; r->dirblock = &newblk;
	return TRUE;
}
static struct listentry *MakeListEntry(union objectinfo *info, listtype type, SIPTR *error, globaldata *g)
{
	(void)info; (void)type; (void)g;
	if (!makelist_ok) { *error = ERROR_NO_FREE_STORE; return NULL; }
	return &fe;
}
static BOOL _AddListEntry(listentry_t *e, globaldata *g) { (void)e; (void)g; return addlist_ok; }
static void RemoveListEntry(listentry_t *e, globaldata *g) { (void)e; (void)g; removelist_calls++; }
static void FreeListEntry(listentry_t *e, globaldata *g) { (void)e; (void)g; freelist_calls++; }
static void RemoveDirEntry(struct fileinfo info, globaldata *g) { (void)info; (void)g; removedirentry_calls++; }
static ULONG AllocReservedBlock(globaldata *g) { (void)g; allocres_calls++; return allocres_ok ? RES_BLOCK : 0; }
static void FreeReservedBlock(ULONG nr, globaldata *g) { (void)g; freeres_calls++; freeres_arg = nr; }
static struct cdirblock *MakeDirBlock(ULONG blocknr, ULONG anodenr, ULONG rootanodenr, ULONG parentnr, globaldata *g)
{
	(void)g;
	makeblk_calls++;
	makeblk_args[0] = blocknr; makeblk_args[1] = anodenr; makeblk_args[2] = rootanodenr; makeblk_args[3] = parentnr;
	return makeblk_ok ? &dirblk : NULL;
}
static LONG ErrorMsg(CONST_STRPTR msg, void *arg, globaldata *g) { (void)arg; (void)g; errormsg_calls++; last_msg = (const UBYTE *)msg; return 0; }

struct anodewalk { ULONG mark; ULONG steps; };
#include "gen/GetNextAnode.c"			/* the REAL function from ../../anodes.c */
#include "gen/DeleteDir.c"			/* the REAL functions from ../../directory.c */
#include "gen/KillEmpty.c"
#include "gen/NewDir.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static struct volumedata vol;
static globaldata gd = { &vol, FNSIZE };

static void reset(void)
{
	memset(tbl_next, 0, sizeof(tbl_next)); memset(tbl_ok, 0, sizeof(tbl_ok));
	dirisempty_calls = changedirentry_calls = updatedisk_calls = restobefreed_calls = freeanode_calls = 0;
	dirisempty_arg = freed_first = freed_last = rtbf_first = rtbf_last = 0;
	si_dirnodenr = 0; si_name[0] = 'x'; si_name[1] = 0;
	getanode_calls = runaway = 0;
	dir_empty = 1; si_result = 1;
	memset(&found_de, 0, sizeof(found_de)); found_de.type = ST_USERDIR; found_de.anode = 7;
	cv_ok = makeentry_ok = adddirent_ok = makelist_ok = addlist_ok = allocres_ok = makeblk_ok = 1;
	removedirentry_calls = removelist_calls = freelist_calls = allocres_calls = makeblk_calls = 0;
	freeres_calls = errormsg_calls = adddirent_calls = 0;
	freeres_arg = 0; last_msg = NULL;
	memset(makeblk_args, 0, sizeof(makeblk_args));
	alloc_data.rtbf_index = 0; alloc_data.res_alert = 0;
	vol.metadata_error = 0; vol.dirblks = NULL;
}
static void chain(ULONG n)			/* anodes 1 .. n, 1 is the head */
{
	ULONG i;
	for (i = 1; i <= n; i++) { tbl_ok[i] = 1; tbl_next[i] = (i < n) ? i + 1 : 0; }
	found_de.anode = 1;
}
static int nothing_deleted(void)
{
	return freeanode_calls == 0 && restobefreed_calls == 0 && changedirentry_calls == 0 && updatedisk_calls == 0;
}

static void test_killempty(void)
{
	union objectinfo parent;
	BOOL r;

	/* a real directory under the empty name is deleted; the search starts at the volume root */
	reset(); chain(2);
	memset(&parent, 0, sizeof(parent));
	r = KillEmpty(&parent, &gd);
	CHECK(r == DOSTRUE, "real empty dir: DOSTRUE (r=%d)", r);
	CHECK(si_dirnodenr == ANODE_ROOTDIR && si_name[0] == 0, "volume parent: search in ANODE_ROOTDIR for the empty name (nr %u)", si_dirnodenr);
	CHECK(dirisempty_calls == 1 && dirisempty_arg == 1 && freeanode_calls == 2 && changedirentry_calls == 1,
		  "real dir: emptiness checked, both anodes freed, entry removed (isempty %d freed %d entry %d)",
		  dirisempty_calls, freeanode_calls, changedirentry_calls);
	CHECK(restobefreed_calls == 2 && rtbf_first == 1001 && rtbf_last == 1002, "real dir: both blocks to be freed");

	/* parent is a directory: its anode is searched */
	reset(); chain(1);
	memset(&parent, 0, sizeof(parent));
	{ static struct direntry pde; static struct cdirblock pblk; pde.anode = 99; parent.file.direntry = &pde; parent.file.dirblock = &pblk; }
	r = KillEmpty(&parent, &gd);
	CHECK(r == DOSTRUE && si_dirnodenr == 99, "directory parent: search in its anode (nr %u)", si_dirnodenr);

	/* anything under that name but a real directory survives: link to a dir, soft link, file */
	{
		static const struct { BYTE type; const char *what; } kinds[] = {
			{ ST_LINKDIR, "hard link to a directory" }, { ST_SOFTLINK, "soft link" }, { ST_FILE, "file" } };
		int k;
		for (k = 0; k < 3; k++) {
			reset(); chain(2);
			found_de.type = kinds[k].type;
			memset(&parent, 0, sizeof(parent));
			r = KillEmpty(&parent, &gd);
			CHECK(r == DOSFALSE, "%s under the empty name: DOSFALSE (r=%d)", kinds[k].what, r);
			CHECK(dirisempty_calls == 0 && nothing_deleted(), "%s must not be deleted (isempty %d freed %d entry %d)",
				  kinds[k].what, dirisempty_calls, freeanode_calls, changedirentry_calls);
		}
	}

	/* absent / unreadable */
	reset(); chain(2); si_result = 0;
	memset(&parent, 0, sizeof(parent));
	r = KillEmpty(&parent, &gd);
	CHECK(r == DOSFALSE && dirisempty_calls == 0 && nothing_deleted(), "no empty-name entry: DOSFALSE, nothing deleted");
	reset(); chain(2); si_result = -1;
	r = KillEmpty(&parent, &gd);
	CHECK(r == DOSFALSE && dirisempty_calls == 0 && nothing_deleted(), "unreadable directory: DOSFALSE, nothing deleted");

	/* a real directory that is not empty is kept */
	reset(); chain(2); dir_empty = 0;
	r = KillEmpty(&parent, &gd);
	CHECK(r == DOSFALSE && dirisempty_calls == 1 && nothing_deleted(), "non-empty dir: DOSFALSE, nothing freed");
}

static void test_deletedir(void)
{
	union objectinfo info;
	SIPTR e;
	BOOL r;

	memset(&info, 0, sizeof(info));
	info.file.direntry = &found_de; info.file.dirblock = &found_blk;

	/* ordinary chain of three anodes */
	reset(); chain(3); e = 0;
	r = DeleteDir(&info, &e, &gd);
	CHECK(r == DOSTRUE, "3 anode chain: DOSTRUE (r=%d e=%ld)", r, (long)e);
	CHECK(freeanode_calls == 3 && freed_first == 1 && freed_last == 3, "3 anode chain: anodes 1..3 freed");
	CHECK(restobefreed_calls == 3 && rtbf_first == 1001 && rtbf_last == 1003, "3 anode chain: blocks 1001..1003 to be freed");
	CHECK(changedirentry_calls == 1 && updatedisk_calls == 0, "3 anode chain: entry removed, no early disk update");

	/* long chains, also past what a 16-bit counter holds: deleted entirely */
	{
		static const ULONG lens[] = { 65534, 65535, 65536, 65537, 131072 };
		int i;
		for (i = 0; i < 5; i++) {
			ULONG n = lens[i];
			reset(); chain(n); e = 0;
			r = DeleteDir(&info, &e, &gd);
			CHECK(r == DOSTRUE && freeanode_calls == (int)n && freed_last == n && restobefreed_calls == (int)n,
				  "%u anode chain: deleted (r=%d freed %d e=%ld)", (unsigned)n, r, freeanode_calls, (long)e);
			CHECK(changedirentry_calls == 1 && updatedisk_calls == 1,
				  "%u anode chain: entry removed, disk updated for the tobefreed cache", (unsigned)n);
		}
	}

	/* two anodes pointing at each other: corrupt, must end with an error and free nothing */
	reset(); chain(2); tbl_next[2] = 1; e = 0;
	r = DeleteDir(&info, &e, &gd);
	CHECK(!runaway, "cyclic chain: the walk must stop on its own (GetAnode calls %d)", getanode_calls);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT, "cyclic chain: DOSFALSE, PFS_ERROR_INCONSISTENT (r=%d e=%ld)", r, (long)e);
	CHECK(getanode_calls <= 8, "cyclic chain: ended within a few reads (GetAnode calls %d)", getanode_calls);
	CHECK(nothing_deleted(), "cyclic chain: nothing freed or removed (freed %d entry %d)", freeanode_calls, changedirentry_calls);

	/* a long tail into a cycle: 100000 anodes, the last one back to 70000 */
	reset(); chain(100000); tbl_next[100000] = 70000; e = 0;
	r = DeleteDir(&info, &e, &gd);
	CHECK(!runaway && r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && nothing_deleted(),
		  "tail into a cycle: DOSFALSE, INCONSISTENT, nothing freed (r=%d e=%ld runaway %d)", r, (long)e, runaway);

	/* an anode pointing at itself */
	reset(); chain(1); tbl_next[1] = 1; e = 0;
	r = DeleteDir(&info, &e, &gd);
	CHECK(!runaway && r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && nothing_deleted(),
		  "self-referencing anode: DOSFALSE, INCONSISTENT, nothing freed (r=%d e=%ld runaway %d)", r, (long)e, runaway);

	/* an unreadable anode in the chain: same error, nothing freed */
	reset(); chain(3); tbl_ok[3] = 0; e = 0;
	r = DeleteDir(&info, &e, &gd);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && nothing_deleted(), "unreadable anode in the chain: DOSFALSE, INCONSISTENT, nothing freed");

	/* a directory that is not empty: DirIsEmpty decides, nothing is walked */
	reset(); chain(3); dir_empty = 0; e = 0;
	r = DeleteDir(&info, &e, &gd);
	CHECK(r == DOSFALSE && getanode_calls == 0 && nothing_deleted(), "non-empty directory: DOSFALSE, anodes not even read");
}

static ULONG run_newdir(lockentry_t **out)
{
	static char name[] = "newdir";
	union objectinfo parent;
	SIPTR e = 0;			/* what the caller left in *error */
	lockentry_t *le;

	memset(&parent, 0, sizeof(parent));
	si_result = 0;			/* name does not exist yet */
	le = NewDir(&parent, name, &e, &gd);
	if (out) *out = le;
	return (ULONG)e;
}

static void test_newdir(void)
{
	lockentry_t *le;
	ULONG e;

	/* success: nothing is released; the new dirblock gets the new anode and the root as parent */
	reset();
	e = run_newdir(&le);
	CHECK(le == (lockentry_t *)&fe, "success: the new lock entry is returned");
	CHECK(removedirentry_calls == 0 && removelist_calls == 0 && freelist_calls == 0 && freeres_calls == 0 && freeanode_calls == 0,
		  "success: nothing may be released");
	CHECK(makeblk_calls == 1 && makeblk_args[0] == RES_BLOCK && makeblk_args[1] == NEW_ANODE &&
		  makeblk_args[2] == NEW_ANODE && makeblk_args[3] == ANODE_ROOTDIR, "success: first dirblock made for the new anode, parent root");

	/* AddListEntry fails: IN_USE, and the half-made directory is undone */
	reset(); addlist_ok = 0;
	e = run_newdir(&le);
	CHECK(le == NULL, "AddListEntry fails: NULL");
	CHECK(e == ERROR_OBJECT_IN_USE, "AddListEntry fails: *error must be ERROR_OBJECT_IN_USE (got %u)", e);
	CHECK(errormsg_calls == 1 && last_msg == AFS_ERROR_NEWDIR_ADDLISTENTRY, "AddListEntry fails: the volume is told (ErrorMsg)");
	CHECK(freeanode_calls == 1 && freed_first == NEW_ANODE, "AddListEntry fails: the new directory's anode %d is freed (freed %d, nr %u)", NEW_ANODE, freeanode_calls, freed_first);
	CHECK(removedirentry_calls == 1, "AddListEntry fails: the directory entry is removed");
	CHECK(freelist_calls == 1 && removelist_calls == 0, "AddListEntry fails: the never-added list entry is FREED, not removed (free %d remove %d)", freelist_calls, removelist_calls);
	CHECK(allocres_calls == 0 && makeblk_calls == 0 && freeres_calls == 0, "AddListEntry fails: no reserved block was touched");

	/* no reserved block: DISK_FULL, list entry was added so it is removed */
	reset(); allocres_ok = 0;
	e = run_newdir(&le);
	CHECK(le == NULL && e == ERROR_DISK_FULL, "no reserved block: NULL, DISK_FULL (e=%u)", e);
	CHECK(freeanode_calls == 1 && freed_first == NEW_ANODE && removedirentry_calls == 1 && removelist_calls == 1 && freelist_calls == 0,
		  "no reserved block: anode freed, entry removed, list entry REMOVED (free %d remove %d)", freelist_calls, removelist_calls);
	CHECK(makeblk_calls == 0 && freeres_calls == 0, "no reserved block: no dirblock made, no block freed");

	/* dirblock memory missing: NO_FREE_STORE, the reserved block goes back */
	reset(); makeblk_ok = 0;
	e = run_newdir(&le);
	CHECK(le == NULL && e == ERROR_NO_FREE_STORE, "no dirblock memory: NULL, NO_FREE_STORE (e=%u)", e);
	CHECK(freeres_calls == 1 && freeres_arg == RES_BLOCK, "no dirblock memory: the reserved block is freed");
	CHECK(freeanode_calls == 1 && removedirentry_calls == 1 && removelist_calls == 1 && freelist_calls == 0, "no dirblock memory: same cleanup, list entry REMOVED");

	/* no memory for the list entry: the error comes from MakeListEntry; entry and anode are undone, no list cleanup */
	reset(); makelist_ok = 0;
	e = run_newdir(&le);
	CHECK(le == NULL && e == ERROR_NO_FREE_STORE, "MakeListEntry fails: NULL, its error kept (e=%u)", e);
	CHECK(freeanode_calls == 1 && removedirentry_calls == 1 && removelist_calls == 0 && freelist_calls == 0 && errormsg_calls == 0,
		  "MakeListEntry fails: anode freed, entry removed, no list entry to release");

	/* no room for the directory entry */
	reset(); adddirent_ok = 0;
	e = run_newdir(&le);
	CHECK(le == NULL && e == ERROR_DISK_FULL && freeanode_calls == 1 && freed_first == NEW_ANODE && removedirentry_calls == 0,
		  "AddDirectoryEntry fails: NULL, DISK_FULL, the anode from MakeDirEntry freed (e=%u)", e);
}

int main(void)
{
	test_killempty();
	test_deletedir();
	test_newdir();
	if (failures) { printf("dir_misc_live: %d FAILED\n", failures); return 1; }
	printf("  PASS: KillEmpty only deletes real directories; DeleteDir deletes chains of any length, stops on cyclic chains; NewDir cleanup\n");
	return 0;
}
