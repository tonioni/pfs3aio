/* LIVE @src directory.c @func SetDirParent @func IsChildOf @func RenameAndMove
 *
 * Lifts the checkout's real SetDirParent, IsChildOf and RenameAndMove and
 * asserts what they do to the directory tree.
 *
 * SetDirParent (rewrites blk.parent of EVERY dirblock of a moved directory):
 *  - a directory of several blocks spread over several anodes: every block is
 *    updated and marked dirty exactly once, no neighbouring block is touched;
 *  - GetAnode of the directory failing, LoadDirBlock failing on the first, a
 *    middle or the first block of a later anode, and the NEXT anode not being
 *    readable (NextBlock then fails and leaves anode.blocknr == ~0) all end in
 *    FALSE. A GetAnode failure must not even try to read block ~0.
 *
 * RenameAndMove:
 *  - a real directory moved to another parent: all its dirblocks get the new
 *    parent (the volume root counts as ANODE_ROOTDIR), SetDirParent failure
 *    (anode or dirblock) -> metadata_error, PFS_ERROR_INCONSISTENT, DOSFALSE;
 *  - a file, a hard link to a directory, a soft link, and a directory renamed
 *    inside its own parent never touch the dirblocks of their anode;
 *  - IsChildOf's verdict is used as is: a directory into itself or its child
 *    -> ERROR_OBJECT_IN_USE, a broken or cyclic parent chain ->
 *    PFS_ERROR_INCONSISTENT; both return DOSFALSE before anything is changed
 *    (no ChangeDirEntry, no dirblock written); a destination that is merely
 *    deeper elsewhere in the tree is allowed.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef intptr_t SIPTR; typedef intptr_t IPTR;
typedef int BOOL; typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define DELDIR 1
#define MAXWALK 4096			/* as in directory.c */
#define ANODE_ROOTDIR 5			/* as in blocks.h */
#define FNSIZE 108
#define CMSIZE 80
#define MAX_ENTRYSIZE (sizeof(struct direntry) + FNSIZE + CMSIZE + 34)
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define ENTER(x)
#define DB(x)
#define ST_USERDIR 2			/* dos/dosextens.h */
#define ST_SOFTLINK 3
#define ST_LINKDIR 4
#define ST_FILE (-3)
#define ST_LINKFILE (-4)
#define ERROR_OBJECT_IN_USE 202		/* dos/dos.h */
#define ERROR_OBJECT_EXISTS 203
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_DISK_NOT_VALIDATED 213
#define ERROR_DISK_FULL 221
#define ERROR_WRITE_PROTECTED 223
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED		/* struct.h */
#define SPECIAL_FLUSHED 3
#define SPECIAL_DELDIR 1

/* on-disk layouts, as in blocks.h */
#pragma pack(push, 2)
struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection; UBYTE nlength; UBYTE startofname; UBYTE pad;
};
struct dirblock {
	UWORD id; UWORD not_used; ULONG datestamp; UWORD not_used_2[2];
	ULONG anodenr; ULONG parent; UBYTE entries[0];
};
#pragma pack(pop)
struct volumedata;
struct cdirblock {
	struct cdirblock *next, *prev; struct volumedata *volume;
	ULONG blocknr, oldblocknr; UWORD used; UBYTE changeflag, dummy;
	struct dirblock blk;
};
struct cachedblock;
typedef struct canode { ULONG clustersize, blocknr, next, nr; } canode_t;
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct volumeinfo { ULONG root; struct volumedata *volume; };
struct deldirinfo { ULONG special; struct volumedata *volume; };
union objectinfo { struct fileinfo file; struct volumeinfo volume; struct deldirinfo deldir; };
struct rootblock { ULONG reserved_blksize; };
struct volumedata { int metadata_error; };
typedef struct globaldata {
	struct volumedata *currentvolume; struct rootblock *rootblock; int fnsize; int dirextension;
} globaldata;

/* macros copied from struct.h / blocks.h */
#define COMMENT(de) ((UBYTE*)(&((de)->startofname) + (de)->nlength))
#define FILENAMESIZE (g->fnsize)
#define FIANODENR(fi) ((fi)->direntry->anode)
#define IsRealDir(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type==ST_USERDIR))
#define IsDir(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type)>0)
#define IsVolume(oi) ((oi).volume.root==0)
#define IsDelDir(oi) ((oi).deldir.special==SPECIAL_DELDIR)
#define IsRootA(oi) ((oi).volume.root == 0)
#define IsSameOI(oi1, oi2) ((oi1.file.direntry == oi2.file.direntry) && \
	(oi1.file.dirblock == oi2.file.dirblock))
static struct { int res_alert; } alloc_data;
#define ReservedAreaIsLocked (alloc_data.res_alert)

/* ---- the anode table and the dirblocks: what is "on disk" ---- */
#define NANODES 128
#define NBLOCKS 512
#define UNSET 0xdeadbeefu
static struct { ULONG clustersize, blocknr, next; int ok; } tab[NANODES];
static struct cdirblock pool[NBLOCKS];
static int bad[NBLOCKS], dirty[NBLOCKS];
static int loads, anode_reads;

static BOOL GetAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	anode_reads++;
	if (nr >= NANODES || !tab[nr].ok) {		/* as the real GetAnode */
		a->clustersize = a->next = 0; a->blocknr = ~0UL; a->nr = nr;
		return FALSE;
	}
	a->clustersize = tab[nr].clustersize; a->blocknr = tab[nr].blocknr;
	a->next = tab[nr].next; a->nr = nr;
	return TRUE;
}
/* as the real NextBlock/CorrectAnode, without the cycle marker */
static BOOL NextBlock(struct canode *a, ULONG *off, globaldata *g)
{
	(*off)++;
	while (*off >= a->clustersize) {
		if (!a->next) return FALSE;
		*off -= a->clustersize;
		if (!GetAnode(a, a->next, g)) return FALSE;
	}
	return TRUE;
}
static struct cdirblock *LoadDirBlock(ULONG nr, globaldata *g)
{
	(void)g;
	loads++;
	if (nr >= NBLOCKS || bad[nr]) return NULL;
	pool[nr].blocknr = nr;
	return &pool[nr];
}
static BOOL MakeBlockDirty(struct cachedblock *b, globaldata *g)
{
	(void)g;
	dirty[((struct cdirblock *)b)->blocknr]++;
	return TRUE;
}

#include "gen/SetDirParent.c"			/* the REAL functions from ../../directory.c */

/* ---- the directory tree for RenameAndMove ---- */
enum { N_ROOT, N_X, N_Y, N_Z, N_W, N_BROKEN, N_C1, N_C2, N_COUNT };
static const ULONG node_anode[N_COUNT] = { 0, 30, 21, 22, 23, 24, 25, 26 };
typedef struct { struct cdirblock b; UBYTE space[600]; } blkmem_t;
static blkmem_t nodeblk[N_COUNT], destblk;
static int parent_of[N_COUNT];			/* node index; -1: GetParent fails */
static int parent_calls, runaway;
static int chg_calls, chg_ok, move_calls, notify_calls;
static ULONG chg_anode;
static BYTE chg_type;
static struct volumedata vol;
static struct rootblock rb = { 512 };
static globaldata gd = { &vol, &rb, FNSIZE, 0 };

static struct direntry *entry_of(int n) { return (struct direntry *)nodeblk[n].b.blk.entries; }
static union objectinfo oi(int n)
{
	union objectinfo o;
	memset(&o, 0, sizeof(o));
	if (n != N_ROOT) { o.file.direntry = entry_of(n); o.file.dirblock = &nodeblk[n].b; }
	return o;
}
static int node_of(union objectinfo *o)
{
	int n;
	for (n = 1; n < N_COUNT; n++) if (o->file.direntry == entry_of(n)) return n;
	return -1;
}

static BOOL GetParent(union objectinfo *c, union objectinfo *up, SIPTR *error, globaldata *g)
{
	int n;
	(void)g;
	if (++parent_calls > 50000) { runaway = 1; *error = 0; return FALSE; }
	if (IsVolume(*c)) { *error = 0; return FALSE; }		/* root: no error, nothing returned */
	n = node_of(c);
	if (n < 0 || parent_of[n] < 0) { *error = PFS_ERROR_INCONSISTENT; return FALSE; }
	*up = oi(parent_of[n]);
	return TRUE;
}
static union objectinfo wanted_dest;
static STRPTR GetFullPath(union objectinfo *dir, STRPTR path, union objectinfo *out, SIPTR *error, globaldata *g)
{
	(void)dir; (void)error; (void)g;
	*out = wanted_dest;
	return path;
}
static BOOL FindObject(union objectinfo *dir, STRPTR name, union objectinfo *res, SIPTR *error, globaldata *g)
{
	(void)dir; (void)name; (void)res; (void)g;
	*error = ERROR_OBJECT_NOT_FOUND;
	return FALSE;
}
static BOOL ChangeDirEntry(struct fileinfo from, struct direntry *to, union objectinfo *dest, struct fileinfo *res, globaldata *g)
{
	(void)from; (void)dest; (void)g;
	chg_calls++;
	if (!chg_ok) return FALSE;
	chg_anode = to->anode; chg_type = to->type;
	memcpy(destblk.b.blk.entries, to, to->next);
	res->direntry = (struct direntry *)destblk.b.blk.entries;
	res->dirblock = &destblk.b;
	return TRUE;
}
static BOOL MoveLink(struct direntry *o, ULONG newdiran, globaldata *g) { (void)o; (void)newdiran; (void)g; move_calls++; return TRUE; }
static void PFSDoNotify(struct fileinfo *fi, BOOL c, globaldata *g) { (void)fi; (void)c; (void)g; notify_calls++; }

#include "gen/IsChildOf.c"
#include "gen/RenameAndMove.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void reset(void)
{
	int i, n;
	memset(tab, 0, sizeof(tab)); memset(bad, 0, sizeof(bad)); memset(dirty, 0, sizeof(dirty));
	memset(pool, 0, sizeof(pool));
	for (i = 0; i < NBLOCKS; i++) pool[i].blk.parent = UNSET;
	loads = anode_reads = 0;
	/* single block directory, a six block directory over three anodes, ... */
	tab[40].clustersize = 1; tab[40].blocknr = 100; tab[40].next = 0;   tab[40].ok = 1;
	tab[41].clustersize = 3; tab[41].blocknr = 110; tab[41].next = 42;  tab[41].ok = 1;
	tab[42].clustersize = 2; tab[42].blocknr = 120; tab[42].next = 43;  tab[42].ok = 1;
	tab[43].clustersize = 1; tab[43].blocknr = 130; tab[43].next = 0;   tab[43].ok = 1;
	/* 44: unreadable; 45 -> unreadable 46 */
	tab[45].clustersize = 2; tab[45].blocknr = 140; tab[45].next = 46;  tab[45].ok = 1;
	/* RenameAndMove: the moved directory (30 -> 31) and the linked-to anodes */
	tab[30].clustersize = 2; tab[30].blocknr = 200; tab[30].next = 31;  tab[30].ok = 1;
	tab[31].clustersize = 1; tab[31].blocknr = 210; tab[31].next = 0;   tab[31].ok = 1;
	for (i = 50; i <= 53; i++) { tab[i].clustersize = 1; tab[i].blocknr = 300 + i; tab[i].ok = 1; }
	for (n = 0; n < N_COUNT; n++) {
		struct direntry *e;
		memset(&nodeblk[n], 0, sizeof(nodeblk[n]));
		e = entry_of(n);
		e->next = 22; e->type = ST_USERDIR; e->anode = node_anode[n];
		e->nlength = 3; memcpy(&e->startofname, "src", 3); *COMMENT(e) = 0;
	}
	memset(&destblk, 0, sizeof(destblk));
	parent_of[N_ROOT] = -1; parent_of[N_X] = N_ROOT; parent_of[N_Y] = N_ROOT; parent_of[N_Z] = N_X;
	parent_of[N_W] = N_Y; parent_of[N_BROKEN] = -1; parent_of[N_C1] = N_C2; parent_of[N_C2] = N_C1;
	vol.metadata_error = 0;
	parent_calls = runaway = chg_calls = move_calls = notify_calls = 0;
	chg_ok = 1; chg_anode = 0; chg_type = 0;
	alloc_data.res_alert = 0;
}

/* exactly the blocks in list[] were rewritten (once, new parent), nothing else */
static int only_changed(const int *list, int n, ULONG parent)
{
	int i, j, ok = 1;
	for (i = 0; i < NBLOCKS; i++) {
		int want = 0;
		for (j = 0; j < n; j++) if (list[j] == i) want = 1;
		if (want && !(pool[i].blk.parent == parent && dirty[i] == 1)) ok = 0;
		if (!want && !(pool[i].blk.parent == UNSET && dirty[i] == 0)) ok = 0;
	}
	return ok;
}

static void test_setdirparent(void)
{
	static const int all6[] = { 110, 111, 112, 120, 121, 130 };
	static const int first2[] = { 110, 111 };
	static const int none[] = { 0 };
	BOOL r;

	reset();
	r = SetDirParent(40, 77, &gd);
	CHECK(r == TRUE, "single block directory: TRUE");
	CHECK(only_changed((const int[]){ 100 }, 1, 77), "single block directory: only block 100 rewritten, once");

	reset();
	r = SetDirParent(41, 77, &gd);
	CHECK(r == TRUE, "three anodes, six blocks: TRUE");
	CHECK(only_changed(all6, 6, 77), "all six dirblocks must get the new parent, none other");

	reset(); tab[44].ok = 0;
	r = SetDirParent(44, 77, &gd);
	CHECK(r == FALSE, "unreadable directory anode: FALSE");
	CHECK(loads == 0, "unreadable directory anode: no block may be read (loaded %d)", loads);
	CHECK(only_changed(none, 0, 77), "unreadable directory anode: nothing written");

	reset(); bad[100] = 1;
	r = SetDirParent(40, 77, &gd);
	CHECK(r == FALSE, "unreadable first dirblock: FALSE");
	CHECK(only_changed(none, 0, 77), "unreadable first dirblock: nothing written");

	reset(); bad[112] = 1;
	r = SetDirParent(41, 77, &gd);
	CHECK(r == FALSE, "unreadable third block of the first anode: FALSE");
	CHECK(only_changed(first2, 2, 77), "blocks after the unreadable one must not be written (the walk stops)");

	reset(); bad[120] = 1;
	r = SetDirParent(41, 77, &gd);
	CHECK(r == FALSE, "unreadable first block of the second anode: FALSE");
	CHECK(only_changed((const int[]){ 110, 111, 112 }, 3, 77), "blocks of the second anode and later must not be written");

	reset(); bad[130] = 1;
	r = SetDirParent(41, 77, &gd);
	CHECK(r == FALSE, "unreadable last block: FALSE");
	CHECK(only_changed((const int[]){ 110, 111, 112, 120, 121 }, 5, 77), "the first five blocks were written");

	/* the next anode cannot be read: NextBlock fails with anode.blocknr == ~0, not "end of directory" */
	reset();
	r = SetDirParent(45, 77, &gd);
	CHECK(r == FALSE, "unreadable next anode: FALSE (not 'end of directory')");
	CHECK(only_changed((const int[]){ 140, 141 }, 2, 77), "the blocks before the unreadable anode were written");
	tab[46].clustersize = 1; tab[46].blocknr = 150; tab[46].next = 0; tab[46].ok = 1;
	memset(dirty, 0, sizeof(dirty));
	r = SetDirParent(45, 78, &gd);
	CHECK(r == TRUE, "readable next anode: TRUE");
	CHECK(pool[140].blk.parent == 78 && pool[141].blk.parent == 78 && pool[150].blk.parent == 78 &&
		  dirty[150] == 1 && dirty[140] == 1, "readable next anode: all three blocks have the new parent (once dirty)");
}

static BOOL rename_to(int srcparent, int dest, BYTE type, ULONG anode, SIPTR *err)
{
	union objectinfo sdi, src, dir;
	char path[] = "dest";
	struct direntry *x = entry_of(N_X);

	x->type = type; x->anode = anode;
	parent_of[N_X] = srcparent;
	sdi = oi(srcparent);
	src = oi(N_X);
	dir = oi(N_ROOT);
	wanted_dest = oi(dest);
	*err = 0;
	return RenameAndMove(&sdi, &src, &dir, path, err, &gd);
}

static void test_rename(void)
{
	static const int xblocks[] = { 200, 201, 210 };
	static const int none[] = { 0 };
	SIPTR e;
	BOOL r;

	/* real directory moved from the root to Y: its three dirblocks get Y's anode as parent */
	reset();
	r = rename_to(N_ROOT, N_Y, ST_USERDIR, 30, &e);
	CHECK(r == DOSTRUE && chg_calls == 1 && vol.metadata_error == 0, "dir -> Y: DOSTRUE (r=%d)", r);
	CHECK(chg_anode == 30 && chg_type == ST_USERDIR, "the new entry keeps the directory's anode");
	CHECK(only_changed(xblocks, 3, node_anode[N_Y]), "dir -> Y: every dirblock of anode 30/31 has parent 21, nothing else changed");
	CHECK(move_calls == 1 && notify_calls == 2, "dir -> Y: links updated, notified (move %d notify %d)", move_calls, notify_calls);

	/* ... and back to the volume root: parent is ANODE_ROOTDIR */
	reset();
	r = rename_to(N_Y, N_ROOT, ST_USERDIR, 30, &e);
	CHECK(r == DOSTRUE, "dir -> root: DOSTRUE");
	CHECK(only_changed(xblocks, 3, ANODE_ROOTDIR), "dir -> root: every dirblock has parent ANODE_ROOTDIR");

	/* a destination deeper in an unrelated branch is fine: IsChildOf walked up to the root */
	reset();
	r = rename_to(N_ROOT, N_W, ST_USERDIR, 30, &e);
	CHECK(r == DOSTRUE && chg_calls == 1, "dir -> W (below Y): allowed");
	CHECK(only_changed(xblocks, 3, node_anode[N_W]), "dir -> W: parent is W's anode");

	/* SetDirParent fails: anode unreadable / a dirblock unreadable */
	reset(); tab[30].ok = 0;
	r = rename_to(N_ROOT, N_Y, ST_USERDIR, 30, &e);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && vol.metadata_error == 1,
		  "unreadable dir anode: DOSFALSE, INCONSISTENT, metadata_error (r=%d e=%ld me=%d)", r, (long)e, vol.metadata_error);
	reset(); bad[210] = 1;
	r = rename_to(N_ROOT, N_Y, ST_USERDIR, 30, &e);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && vol.metadata_error == 1,
		  "unreadable last dirblock: DOSFALSE, INCONSISTENT, metadata_error (r=%d e=%ld me=%d)", r, (long)e, vol.metadata_error);
	reset(); bad[201] = 1;
	r = rename_to(N_ROOT, N_Y, ST_USERDIR, 30, &e);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && vol.metadata_error == 1,
		  "unreadable middle dirblock: DOSFALSE, INCONSISTENT, metadata_error (r=%d e=%ld me=%d)", r, (long)e, vol.metadata_error);

	/* only a REAL directory that changes parent has its dirblocks rewritten */
	{
		static const struct { BYTE type; ULONG anode; const char *what; } kinds[] = {
			{ ST_FILE, 52, "file" }, { ST_LINKFILE, 53, "link to a file" },
			{ ST_LINKDIR, 50, "hard link to a directory" }, { ST_SOFTLINK, 51, "soft link" } };
		int k;
		for (k = 0; k < 4; k++) {
			reset();
			bad[300 + kinds[k].anode] = 1;		/* would fail the move, if its blocks were read */
			r = rename_to(N_ROOT, N_Y, kinds[k].type, kinds[k].anode, &e);
			CHECK(r == DOSTRUE && vol.metadata_error == 0, "%s moved to Y: DOSTRUE (r=%d)", kinds[k].what, r);
			CHECK(anode_reads == 0 && loads == 0, "%s: its anode/dirblocks must not be read as a directory (anode reads %d, loads %d)",
				  kinds[k].what, anode_reads, loads);
			CHECK(only_changed(none, 0, 0), "%s: no dirblock may be rewritten", kinds[k].what);
		}
	}

	/* same parent: nothing is walked, nothing rewritten, even if the blocks are unreadable */
	reset(); bad[200] = bad[201] = bad[210] = 1;
	r = rename_to(N_ROOT, N_ROOT, ST_USERDIR, 30, &e);
	CHECK(r == DOSTRUE && chg_calls == 1 && move_calls == 0, "rename inside the root: DOSTRUE (r=%d)", r);
	CHECK(parent_calls == 0 && loads == 0 && anode_reads == 0, "rename inside the root: no IsChildOf walk, no SetDirParent");
	reset();
	r = rename_to(N_Y, N_Y, ST_USERDIR, 30, &e);
	CHECK(r == DOSTRUE && parent_calls == 0 && loads == 0 && only_changed(none, 0, 0), "rename inside Y: dirblocks untouched");

	/* IsChildOf: into itself and into its own child -> IN_USE, nothing changed */
	reset();
	r = rename_to(N_ROOT, N_Z, ST_USERDIR, 30, &e);
	CHECK(r == DOSFALSE && e == ERROR_OBJECT_IN_USE, "dir into its child: DOSFALSE, OBJECT_IN_USE (r=%d e=%ld)", r, (long)e);
	CHECK(chg_calls == 0 && move_calls == 0 && notify_calls == 0 && vol.metadata_error == 0 && only_changed(none, 0, 0),
		  "dir into its child: nothing may have been changed (chg %d move %d notify %d)", chg_calls, move_calls, notify_calls);
	reset();
	r = rename_to(N_ROOT, N_X, ST_USERDIR, 30, &e);
	CHECK(r == DOSFALSE && e == ERROR_OBJECT_IN_USE && chg_calls == 0, "dir into itself: DOSFALSE, OBJECT_IN_USE (e=%ld)", (long)e);

	/* a broken parent chain: the destination cannot be proven to be outside the source */
	reset();
	r = rename_to(N_ROOT, N_BROKEN, ST_USERDIR, 30, &e);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT, "broken parent chain: DOSFALSE, INCONSISTENT (r=%d e=%ld)", r, (long)e);
	CHECK(chg_calls == 0 && move_calls == 0 && notify_calls == 0 && only_changed(none, 0, 0), "broken parent chain: nothing may have been changed");

	/* a cyclic parent chain ends with the same error instead of looping */
	reset();
	r = rename_to(N_ROOT, N_C1, ST_USERDIR, 30, &e);
	CHECK(!runaway && parent_calls <= MAXWALK + 2, "cyclic chain must stop (GetParent calls %d)", parent_calls);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT, "cyclic parent chain: DOSFALSE, INCONSISTENT (r=%d e=%ld)", r, (long)e);
	CHECK(chg_calls == 0 && move_calls == 0 && notify_calls == 0 && only_changed(none, 0, 0), "cyclic parent chain: nothing may have been changed");

	/* only real directories are subject to the check: files and links move anywhere */
	reset();
	r = rename_to(N_ROOT, N_BROKEN, ST_FILE, 52, &e);
	CHECK(r == DOSTRUE && parent_calls == 0, "file into a directory with a broken chain: no IsChildOf (calls %d)", parent_calls);
	reset();
	r = rename_to(N_ROOT, N_Z, ST_LINKDIR, 50, &e);
	CHECK(r == DOSTRUE && parent_calls == 0, "link to a directory into the directory's child: allowed (r=%d)", r);
}

int main(void)
{
	test_setdirparent();
	test_rename();
	if (failures) { printf("dir_setparent_live: %d FAILED\n", failures); return 1; }
	printf("  PASS: SetDirParent and RenameAndMove directory moves\n");
	return 0;
}
