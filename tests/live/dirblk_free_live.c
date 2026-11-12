/* LIVE @src directory.c @func RenameAcrossDirs @func RemoveDirEntry @func CutDirEntry @func UpdateChangedRef
 *
 * Lifts the checkout's real RenameAcrossDirs (delete and move to another
 * directory), RemoveDirEntry, CutDirEntry and UpdateChangedRef. A directory
 * block that the removal leaves empty is freed in the same packet
 * (FreeEmptyDBlk), unless something still points into it:
 *  - the last entry of a block, nothing referring to it: FreeEmptyDBlk, the
 *    block is not marked dirty (it is gone);
 *  - FreeEmptyDBlk keeps the block (first block of the directory: 0) or fails
 *    (-1): the block is marked dirty, as before;
 *  - a block that keeps entries: marked dirty, FreeEmptyDBlk not called;
 *  - a lock on the removed entry that stays there (delete with to == NULL, as
 *    forced_RemoveDirEntry does): kept and marked dirty;
 *  - an ExNext position on the removed entry is moved on (GetNextEntry)
 *    BEFORE the block is freed; if it stays in the block, the block is kept;
 *  - a move to another directory takes the lock along to the new entry, so
 *    the emptied source block is freed;
 *  - a failed add (no space in the destination) removes nothing;
 *  - RemoveDirEntry, used by the other callers, still marks the block dirty.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef intptr_t SIPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0

#pragma pack(push, 2)
struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection; UBYTE nlength; UBYTE startofname; UBYTE pad;
};
#pragma pack(pop)
#define BLKSIZE 1024
struct dirblock { UWORD id, not_used; ULONG datestamp; UWORD not_used_2[2]; ULONG anodenr, parent; UBYTE entries[BLKSIZE - 20]; };
struct cachedblock;
struct cdirblock { struct volumedata *volume; ULONG blocknr; UWORD used; struct dirblock blk; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
union objectinfo { struct fileinfo file; };
struct MinList { void *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct volumedata { struct MinList fileentries; };
typedef struct listentry {
	struct listentry *next, *prev;
	struct { struct { unsigned dir : 1; } flags; } type;
	union objectinfo info;
} listentry_t;
typedef struct { listentry_t le; struct fileinfo nextentry; ULONG nextanode; } lockentry_t;
struct rootblock { UWORD reserved_blksize; };
typedef struct globaldata { UWORD locknr; struct rootblock *rootblock; } globaldata;

#define LOCK(blk) ((blk)->used = g->locknr)
#define HeadOf(list) ((void *)((list)->mlh_Head))
#define FIRSTENTRY(blok) ((struct direntry *)((blok)->blk.entries))

/* ---- mocks, every call logged in order ---- */
static char trace[256];
static void log1(char c) { size_t n = strlen(trace); if (n < sizeof trace - 1) { trace[n] = c; trace[n + 1] = 0; } }

static WORD free_result;
static struct cachedblock *freed, *dirtied;
static int adds_ok;
static struct fileinfo add_result, next_target;

static BOOL GetParent(union objectinfo *child, union objectinfo *parent, SIPTR *error, globaldata *g)
{ (void)child; (void)parent; (void)error; (void)g; return FALSE; }
static void Touch(struct fileinfo *info, globaldata *g) { (void)info; (void)g; }
static BOOL MakeBlockDirty(struct cachedblock *blk, globaldata *g) { (void)g; dirtied = blk; log1('D'); return TRUE; }
static WORD FreeEmptyDBlk(struct cachedblock *blk, globaldata *g) { (void)g; freed = blk; log1('F'); return free_result; }
static BOOL AddDirectoryEntry(union objectinfo *dir, struct direntry *newentry, struct fileinfo *newinfo, globaldata *g)
{
	(void)dir; (void)newentry; (void)g;
	log1('A');
	if (!adds_ok)
		return FALSE;
	*newinfo = add_result;
	return TRUE;
}
static void GetNextEntry(lockentry_t *file, globaldata *g) { (void)g; file->nextentry = next_target; log1('N'); }

static BOOL CutDirEntry(struct fileinfo info, globaldata * g);
static BOOL UpdateChangedRef(struct fileinfo from, struct fileinfo *to, int diff, globaldata * g);

#include "gen/RenameAcrossDirs.c"		/* the REAL functions from ../../directory.c */
#include "gen/RemoveDirEntry.c"
#include "gen/CutDirEntry.c"
#include "gen/UpdateChangedRef.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata gd;
static struct rootblock rb = { BLKSIZE };
static struct volumedata vol;
static struct cdirblock src, dst, other;
static listentry_t tail;		/* list terminator: next == NULL */

/* block with n entries of 40 bytes; returns entry i */
static struct direntry *entry(struct cdirblock *b, int i) { return (struct direntry *)(b->blk.entries + 40 * i); }
static void fill(struct cdirblock *b, int n)
{
	int i;
	memset(&b->blk, 0, sizeof b->blk);
	b->volume = &vol;
	b->used = 0;
	for (i = 0; i < n; i++) { entry(b, i)->next = 40; entry(b, i)->anode = 100 + i; }
}

static void list(listentry_t **e, int n)
{
	int i;
	vol.fileentries.mlh_Head = n ? (void *)e[0] : (void *)&tail;
	for (i = 0; i < n; i++)
		e[i]->next = i + 1 < n ? e[i + 1] : &tail;
	tail.next = NULL;
}

static void reset(int nsrc, WORD fr)
{
	gd.locknr = 7; gd.rootblock = &rb;
	fill(&src, nsrc); fill(&dst, 1); fill(&other, 1);
	trace[0] = 0; freed = dirtied = NULL;
	free_result = fr; adds_ok = 1;
	add_result.dirblock = &dst; add_result.direntry = entry(&dst, 0);
	next_target.dirblock = &other; next_target.direntry = entry(&other, 0);
	list(NULL, 0);
}

static BOOL del(int i)
{
	struct fileinfo from = { entry(&src, i), &src };
	return RenameAcrossDirs(from, NULL, NULL, NULL, &gd);
}

int main(void)
{
	static struct direntry to;
	union objectinfo destdir;
	struct fileinfo from, result;
	listentry_t fl;
	lockentry_t dl;
	listentry_t *one[1];

	/* the last entry, nothing refers to it: freed, not dirtied */
	reset(1, 1);
	CHECK(del(0) && !strcmp(trace, "F") && freed == (struct cachedblock *)&src, "last entry: freed, not dirtied (trace %s)", trace);
	CHECK(FIRSTENTRY(&src)->next == 0, "last entry: entry removed");

	/* kept by FreeEmptyDBlk (first block) or failure: dirtied */
	reset(1, 0);
	CHECK(del(0) && !strcmp(trace, "FD") && dirtied == (struct cachedblock *)&src, "first block: kept and dirtied (trace %s)", trace);
	reset(1, -1);
	CHECK(del(0) && !strcmp(trace, "FD") && dirtied == (struct cachedblock *)&src, "free failed: dirtied (trace %s)", trace);

	/* block keeps entries: dirtied, no free */
	reset(3, 1);
	CHECK(del(1) && !strcmp(trace, "D") && entry(&src, 1)->anode == 102 && entry(&src, 2)->next == 0, "not empty: dirtied only, entries moved up (trace %s)", trace);

	/* a lock on the removed entry stays (to == NULL): kept */
	reset(1, 1);
	memset(&fl, 0, sizeof fl);
	fl.info.file.dirblock = &src; fl.info.file.direntry = entry(&src, 0);
	one[0] = &fl; list(one, 1);
	CHECK(del(0) && !strcmp(trace, "D"), "lock on the removed entry: kept and dirtied (trace %s)", trace);

	/* ExNext position on the removed entry: moved on first, then freed */
	reset(1, 1);
	memset(&dl, 0, sizeof dl);
	dl.le.type.flags.dir = 1;
	dl.le.info.file.dirblock = &other; dl.le.info.file.direntry = entry(&other, 0);
	dl.nextentry.dirblock = &src; dl.nextentry.direntry = entry(&src, 0);
	one[0] = &dl.le; list(one, 1);
	CHECK(del(0) && !strcmp(trace, "NF") && dl.nextentry.dirblock == &other, "ExNext position: moved on before the free (trace %s)", trace);

	/* ... and if it stays in the block: kept */
	reset(1, 1);
	next_target.dirblock = &src; next_target.direntry = entry(&src, 0);
	dl.nextentry.dirblock = &src; dl.nextentry.direntry = entry(&src, 0);
	list(one, 1);
	CHECK(del(0) && !strcmp(trace, "ND"), "ExNext position left in the block: kept (trace %s)", trace);

	/* moved to another directory: the lock follows, the source block is freed */
	reset(1, 1);
	memset(&fl, 0, sizeof fl);
	fl.info.file.dirblock = &src; fl.info.file.direntry = entry(&src, 0);
	one[0] = &fl; list(one, 1);
	from.dirblock = &src; from.direntry = entry(&src, 0);
	CHECK(RenameAcrossDirs(from, &to, &destdir, &result, &gd) && !strcmp(trace, "AF") && fl.info.file.dirblock == &dst
		&& result.dirblock == &dst && dst.used == gd.locknr, "move: lock follows, source freed, result locked (trace %s)", trace);

	/* failed add: nothing removed */
	reset(1, 1);
	adds_ok = 0;
	from.dirblock = &src; from.direntry = entry(&src, 0);
	CHECK(!RenameAcrossDirs(from, &to, &destdir, &result, &gd) && !strcmp(trace, "A") && FIRSTENTRY(&src)->next == 40, "failed add: source intact (trace %s)", trace);

	/* RemoveDirEntry, for the other callers: dirtied, never freed */
	reset(1, 1);
	from.dirblock = &src; from.direntry = entry(&src, 0);
	RemoveDirEntry(from, &gd);
	CHECK(!strcmp(trace, "D") && FIRSTENTRY(&src)->next == 0, "RemoveDirEntry: dirtied, not freed (trace %s)", trace);

	if (failures) { printf("dirblk_free_live: %d FAILED\n", failures); return 1; }
	printf("  PASS: an emptied dirblock is freed once nothing points into it, else marked dirty\n");
	return 0;
}
