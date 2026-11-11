/* LIVE @src directory.c @func UpdateLinks @func UpdateLinkDir @func MoveLink @func CreateLink @func CreateRollover @src anodes.c @func GetNextAnode
 *
 * Lifts the checkout's real link-list code of directory.c and asserts the
 * guards added to it. (DeleteLink and RemapLinks are in dir_deletelink_live.c.)
 *
 * Link-list walks. The link list of a hard-linked object is a chain of
 * anodes; on a corrupt disk it can point back at an earlier one.
 * UpdateLinks, UpdateLinkDir, MoveLink and the tail walk of CreateLink must
 * follow a valid chain of any length (4096, 4097, 4098 and 8000 links are
 * tried: a fixed length cap rejected a valid disk) and fail instead of looping
 * on a cycle (GetNextAnode, Brent). GetAnode is a mock on a synthetic anode
 * table that counts its calls: an unbounded walk is caught by a call cap
 * (the test then FAILS).
 *
 * CreateLink:
 *  - only files and real directories can be linked to: the volume, a soft link
 *    and a hard link to a directory give ERROR_OBJECT_WRONG_TYPE and nothing
 *    is created;
 *  - MakeDirEntry failing gives ERROR_DISK_FULL (nothing allocated, nothing
 *    freed); AddDirectoryEntry failing gives ERROR_DISK_FULL and frees the
 *    link's anode;
 *  - a cyclic list is reported as PFS_ERROR_INCONSISTENT and the half-made
 *    link is rolled back (entry removed, anode freed);
 *  - normal cases: first link and appending to a chain.
 * CreateRollover: every failure path (MakeDirEntry, GetAnodeChain,
 * AddDirectoryEntry, AllocateBlocksAC) reports ERROR_DISK_FULL and releases
 * exactly what was acquired up to that point.
 *
 * Every rejection is checked together with the matching success case, so a
 * guard that is too strict fails too. The extra fields (link, fsizex) of a
 * direntry are kept in two trailing members of the test's struct direntry
 * (GetExtraFields/AddExtraFields are mocks), so they travel with memcpy.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef uint8_t UBYTE;
typedef int8_t BYTE; typedef intptr_t SIPTR; typedef uintptr_t IPTR; typedef int16_t BOOL;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define DELDIR 1
#define MULTIUSER 1
#define ST_USERDIR 2
#define ST_SOFTLINK 3
#define ST_LINKDIR 4
#define ST_FILE (-3)
#define ST_LINKFILE (-4)
#define ST_ROLLOVERFILE (-16)
#define SPECIAL_DELDIR 1
#define SPECIAL_DELFILE 2
#define SPECIAL_FLUSHED 3
#define ANODE_ROOTDIR 5
#define MAX_ENTRYSIZE 128
#define ERROR_BAD_NUMBER 115
#define ERROR_OBJECT_EXISTS 203
#define ERROR_ACTION_NOT_KNOWN 209
#define ERROR_INVALID_COMPONENT_NAME 210
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_DISK_NOT_VALIDATED 213
#define ERROR_DISK_FULL 221
#define ERROR_WRITE_PROTECTED 223
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED		/* struct.h */

struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection, nlength, startofname, pad;
	ULONG xlink; UWORD xfsizex;			/* stand-ins for the extra fields */
};
struct cachedblock { int dummy; };
struct cdirblock { ULONG used; struct { ULONG anodenr; } blk; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct volumedata { BOOL metadata_error; };
struct volumeinfo { ULONG root; struct volumedata *volume; };
struct deldirinfo { ULONG special; struct volumedata *volume; };
union objectinfo { struct fileinfo file; struct volumeinfo volume; struct deldirinfo deldir; };
struct canode { ULONG clustersize, blocknr, next, nr; };
struct anodechain { int dummy; };
struct extrafields { ULONG link; ULONG virtualsize; ULONG rollpointer; UWORD fsizex; };
typedef struct globaldata {
	struct volumedata *currentvolume; int fnsize, dirextension, blockshift; ULONG locknr;
} globaldata;

static struct { BOOL res_alert; } alloc_data;
#define ReservedAreaIsLocked (alloc_data.res_alert)
#define FILENAMESIZE (g->fnsize)
#define BLOCKSHIFT (g->blockshift)
#define LOCK(blk) ((blk)->used = g->locknr)
#define FIANODENR(fi) ((fi)->direntry->anode)
#define IsRealDir(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type==ST_USERDIR))
#define IsFile(oi) ((IPTR)(oi).file.direntry>SPECIAL_FLUSHED && ((oi).file.direntry->type)<=0)
#define IsVolume(oi) ((oi).volume.root==0)
#define IsDelDir(oi) ((oi).deldir.special==SPECIAL_DELDIR)
#define IsDelFile(oi) ((oi).deldir.special==SPECIAL_DELFILE)
#define ENTER(x)
#define DB(x)

/* ---- the synthetic anode table: what is "on disk" ---- */
#define NANODES 9000
#define CAP 100000				/* GetAnode calls after which a walk is declared runaway */
static struct { ULONG clustersize, blocknr, next; } tab[NANODES];
static struct direntry linkent[NANODES];		/* the link entries FetchObject hands out */
static struct cdirblock fetchblk;
static long get_calls, saves, fetches, dirty;
static int runaway;
static ULONG freed[8], nfreed;
static int removed, makes, adds, changes, detaches, chains, allocs;
static int make_ok, add_ok, chain_ok, alloc_ok;
static const ULONG make_anode = 8900;			/* the anode MakeDirEntry hands out (above every test chain) */
static struct direntry newentry;
static struct cdirblock newblk;
static struct anodechain achain;

static BOOL GetAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	if (++get_calls > CAP) { runaway = 1; return FALSE; }
	a->clustersize = tab[nr].clustersize; a->blocknr = tab[nr].blocknr; a->next = tab[nr].next; a->nr = nr;
	return TRUE;
}
static BOOL SaveAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	saves++;
	tab[nr].clustersize = a->clustersize; tab[nr].blocknr = a->blocknr; tab[nr].next = a->next;
	return TRUE;
}
static BOOL FreeAnode(ULONG nr, globaldata *g) { (void)g; if (nfreed < 8) freed[nfreed] = nr; nfreed++; return TRUE; }
static void GetExtraFields(struct direntry *d, struct extrafields *x)
{ memset(x, 0, sizeof(*x)); x->link = d->xlink; x->fsizex = d->xfsizex; }
static void AddExtraFields(struct direntry *d, struct extrafields *x) { d->xlink = x->link; d->xfsizex = x->fsizex; }
static ULONG GetDEFileSize32(struct direntry *d, globaldata *g) { (void)g; return d->fsize; }
static BOOL MakeBlockDirty(struct cachedblock *b, globaldata *g) { (void)b; (void)g; dirty++; return TRUE; }
static BOOL FetchObject(ULONG dir, ULONG anodenr, union objectinfo *r, SIPTR *e, globaldata *g)
{
	(void)dir; (void)e; (void)g;
	fetches++;
	r->file.direntry = &linkent[anodenr]; r->file.dirblock = &fetchblk;
	return TRUE;
}
static BOOL CheckVolume(struct volumedata *v, BOOL w, SIPTR *e, globaldata *g) { (void)v; (void)w; (void)e; (void)g; return TRUE; }
static LONG SearchInDir(ULONG d, STRPTR n, union objectinfo *i, globaldata *g) { (void)d; (void)n; (void)i; (void)g; return 0; }
static BOOL MakeDirEntry(BYTE type, UBYTE *name, UBYTE *buf, globaldata *g)
{
	struct direntry *d = (struct direntry *)buf;
	(void)name; (void)g;
	makes++;
	if (!make_ok) return FALSE;
	memset(d, 0, sizeof(*d)); d->next = sizeof(*d); d->type = type; d->anode = make_anode;
	return TRUE;
}
static BOOL AddDirectoryEntry(union objectinfo *dir, struct direntry *e, struct fileinfo *r, globaldata *g)
{
	(void)dir; (void)g;
	adds++;
	if (!add_ok) return FALSE;
	newentry = *e;
	r->direntry = &newentry; r->dirblock = &newblk;
	return TRUE;
}
static void RemoveDirEntry(struct fileinfo fi, globaldata *g) { (void)fi; (void)g; removed++; }
static BOOL GetParent(union objectinfo *c, union objectinfo *p, SIPTR *e, globaldata *g)
{ (void)c; (void)e; (void)g; memset(p, 0, sizeof(*p)); return TRUE; }
static BOOL ChangeDirEntry(struct fileinfo from, struct direntry *to, union objectinfo *d, struct fileinfo *r, globaldata *g)
{
	(void)d; (void)g;
	changes++;
	if (to) memcpy(from.direntry, to, to->next);	/* the master takes the new entry */
	*r = from;
	return TRUE;
}
static struct anodechain *GetAnodeChain(ULONG nr, globaldata *g) { (void)nr; (void)g; chains++; return chain_ok ? &achain : NULL; }
static void DetachAnodeChain(struct anodechain *c, globaldata *g) { (void)c; (void)g; detaches++; }
static BOOL AllocateBlocksAC(struct anodechain *c, ULONG size, struct fileinfo *f, globaldata *g)
{ (void)c; (void)size; (void)f; (void)g; allocs++; return alloc_ok; }

/* state of a walk along an anode chain, as in anodes_protos.h */
struct anodewalk { ULONG mark; ULONG steps; };
#include "gen/GetNextAnode.c"			/* the REAL function from ../../anodes.c */
#include "gen/UpdateLinks.c"			/* the REAL functions from ../../directory.c */
#include "gen/UpdateLinkDir.c"
#include "gen/MoveLink.c"
#include "gen/CreateLink.c"
#include "gen/CreateRollover.c"

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct volumedata vol;
static globaldata gd = { &vol, 31, 1, 9, 1 };
static struct direntry master;
static struct cdirblock masterblk;

static void reset(void)
{
	memset(tab, 0, sizeof(tab)); memset(linkent, 0, sizeof(linkent));
	memset(&newentry, 0, sizeof(newentry)); memset(&newblk, 0, sizeof(newblk));
	memset(freed, 0, sizeof(freed)); nfreed = 0;
	get_calls = saves = fetches = dirty = 0; runaway = 0;
	removed = makes = adds = changes = detaches = chains = allocs = 0;
	make_ok = add_ok = chain_ok = alloc_ok = 1;
	alloc_data.res_alert = 0; vol.metadata_error = 0;
	newblk.blk.anodenr = 77;
	memset(&master, 0, sizeof(master));
	master.next = sizeof(master); master.type = ST_FILE; master.anode = 20; master.fsize = 123456;
	masterblk.blk.anodenr = 40;
}

/* anodes first .. first+len-1 chained one after the other, the last one ends the list */
static void build_chain(ULONG first, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		tab[first + i].clustersize = 3; tab[first + i].blocknr = 4;
		tab[first + i].next = (i == len - 1) ? 0 : first + i + 1;
	}
}
/* the same, but the last one points back at the anode 'back' places before it */
static void build_cycle(ULONG first, int len, int back) { build_chain(first, len); tab[first + len - 1].next = first + len - 1 - back; }

/* a walk is "bounded" if it did not hit the call cap */
static int bounded(void) { return !runaway; }
/* the chain lengths tried: around the old fixed cap of 4096, and far above it */
static const int LENS[] = { 4096, 4097, 4098, 8000 };
#define NLENS 4

static void test_walks(void)
{
	struct direntry m;
	ULONG first = 10;
	int ok, k;
	char msg[80];

	/* UpdateLinks: every linked entry gets the master's size */
	reset(); build_chain(first, 3); master.xlink = first;
	ok = UpdateLinks(&master, &gd);
	check(ok == TRUE && fetches == 3 && dirty == 3, "UpdateLinks: a short chain is walked completely");
	check(linkent[10].fsize == 123456 && linkent[12].fsize == 123456, "UpdateLinks: the size reaches every link");
	reset(); master.xlink = 0;
	check(UpdateLinks(&master, &gd) == TRUE && get_calls == 0, "UpdateLinks: no links, nothing to do");
	for (k = 0; k < NLENS; k++) {
		reset(); build_chain(first, LENS[k]); master.xlink = first;
		ok = UpdateLinks(&master, &gd);
		sprintf(msg, "UpdateLinks: a chain of %d links is walked completely", LENS[k]);
		check(ok == TRUE && fetches == LENS[k] && linkent[first + LENS[k] - 1].fsize == 123456, msg);
	}
	reset(); build_cycle(first, 5, 3); master.xlink = first;
	ok = UpdateLinks(&master, &gd);
	check(ok == FALSE && bounded(), "UpdateLinks: a cyclic chain terminates with FALSE");

	/* UpdateLinkDir: every link of the list gets the new object directory */
	reset(); build_chain(first, 3); master.xlink = first;
	ok = UpdateLinkDir(&master, 555, &gd);
	check(ok == TRUE && saves == 3, "UpdateLinkDir: a short chain is walked completely");
	check(tab[10].clustersize == 555 && tab[11].clustersize == 555 && tab[12].clustersize == 555,
		"UpdateLinkDir: the new directory is stored in every link");
	for (k = 0; k < NLENS; k++) {
		reset(); build_chain(first, LENS[k]); master.xlink = first;
		ok = UpdateLinkDir(&master, 555, &gd);
		sprintf(msg, "UpdateLinkDir: a chain of %d links is walked completely", LENS[k]);
		check(ok == TRUE && saves == LENS[k] && tab[first + LENS[k] - 1].clustersize == 555, msg);
	}
	reset(); build_cycle(first, 4, 2); master.xlink = first;
	ok = UpdateLinkDir(&master, 555, &gd);
	check(ok == FALSE && bounded(), "UpdateLinkDir: a cyclic chain terminates with FALSE");

	/* MoveLink: the head (linked-to object) walks the list, a link just changes its own anode */
	reset(); build_chain(first, 3); master.xlink = first;
	ok = MoveLink(&master, 555, &gd);
	check(ok == TRUE && saves == 3 && tab[10].clustersize == 555 && tab[12].clustersize == 555,
		"MoveLink: the head updates every link of a short chain");
	for (k = 0; k < NLENS; k++) {
		reset(); build_chain(first, LENS[k]); master.xlink = first;
		ok = MoveLink(&master, 555, &gd);
		sprintf(msg, "MoveLink: a chain of %d links is walked completely", LENS[k]);
		check(ok == TRUE && saves == LENS[k] && tab[first + LENS[k] - 1].clustersize == 555, msg);
	}
	reset(); build_cycle(first, 4, 3); master.xlink = first;
	ok = MoveLink(&master, 555, &gd);
	check(ok == FALSE && bounded(), "MoveLink: a cyclic chain terminates with FALSE");
	reset(); memset(&m, 0, sizeof(m)); m.next = sizeof(m); m.type = ST_LINKFILE; m.anode = 30; m.xlink = 20;
	tab[30].clustersize = 9; tab[30].blocknr = 8;
	ok = MoveLink(&m, 555, &gd);
	check(ok == TRUE && tab[30].blocknr == 555 && tab[30].clustersize == 9 && saves == 1,
		"MoveLink: a link only gets a new directory in its own anode");
	reset(); ok = MoveLink(&master, 555, &gd);
	check(ok == TRUE && get_calls == 0 && saves == 0, "MoveLink: an object without links is left alone");
}

static union objectinfo linkdirobj, objobj, newlinkobj;
static char lname[] = "lnk";
static sigjmp_buf crashjb;
static void on_crash(int sig) { (void)sig; siglongjmp(crashjb, 1); }

/* master has the given type; type 0 stands for the volume (no direntry at all).
 * Returns 2 if CreateLink crashed (it would dereference the volume's missing entry). */
static BOOL do_createlink(int type, SIPTR *err)
{
	memset(&linkdirobj, 0, sizeof(linkdirobj));		/* the volume as link directory */
	memset(&objobj, 0, sizeof(objobj)); memset(&newlinkobj, 0, sizeof(newlinkobj));
	if (type) {
		master.type = type;
		objobj.file.direntry = &master; objobj.file.dirblock = &masterblk;
	}
	*err = 0;
	signal(SIGSEGV, on_crash);
	if (sigsetjmp(crashjb, 1))
		return 2;
	return CreateLink(&linkdirobj, lname, &objobj, &newlinkobj, err, &gd);
}

static void test_createlink(void)
{
	SIPTR err;
	BOOL ok;
	ULONG first = 10;
	int t, i, rejected[3] = { 0, ST_SOFTLINK, ST_LINKDIR };
	int accepted[3] = { ST_FILE, ST_ROLLOVERFILE, ST_USERDIR };

	/* only files and real directories can be linked to */
	for (t = 0; t < 3; t++) {
		reset();
		ok = do_createlink(rejected[t], &err);
		printf("  CreateLink to type %d: rc=%d err=%ld makes=%d\n", rejected[t], ok, (long)err, makes);
		check(ok == DOSFALSE && err == ERROR_OBJECT_WRONG_TYPE, "CreateLink: volume/softlink/dirlink target gives OBJECT_WRONG_TYPE (or crashed)");
		check(makes == 0 && adds == 0 && saves == 0 && nfreed == 0 && changes == 0, "CreateLink: a rejected target creates nothing");
	}
	for (t = 0; t < 3; t++) {
		reset(); master.fsize = 4242;
		ok = do_createlink(accepted[t], &err);
		check(ok == DOSTRUE && err == 0, "CreateLink: file, rollover file and real directory can be linked to");
		check(newentry.type == (accepted[t] == ST_USERDIR ? ST_LINKDIR : ST_LINKFILE) && newentry.fsize == 4242,
			"CreateLink: the link entry mirrors the object's kind and size");
		check(master.xlink == make_anode && changes == 1, "CreateLink: the first link is recorded in the object");
		check(tab[make_anode].clustersize == 40 && tab[make_anode].blocknr == 77 && tab[make_anode].next == 0,
			"CreateLink: the link anode points at the object's and the link's directory");
		check(nfreed == 0 && removed == 0, "CreateLink: nothing is released on success");
	}

	/* MakeDirEntry fails: disk full, nothing to release */
	reset(); make_ok = 0;
	ok = do_createlink(ST_FILE, &err);
	check(ok == DOSFALSE && err == ERROR_DISK_FULL, "CreateLink: MakeDirEntry failure gives DISK_FULL");
	check(adds == 0 && saves == 0 && nfreed == 0 && removed == 0 && changes == 0,
		"CreateLink: MakeDirEntry failure leaves nothing behind");

	/* AddDirectoryEntry fails: disk full, the link anode is given back */
	reset(); add_ok = 0;
	ok = do_createlink(ST_FILE, &err);
	check(ok == DOSFALSE && err == ERROR_DISK_FULL, "CreateLink: AddDirectoryEntry failure gives DISK_FULL");
	check(nfreed == 1 && freed[0] == make_anode, "CreateLink: AddDirectoryEntry failure frees the link anode");
	check(saves == 0 && removed == 0 && changes == 0 && master.xlink == 0, "CreateLink: the object is left unchanged");

	/* appending to an existing chain: the old tail points at the new link */
	reset(); build_chain(first, 3); master.xlink = first;
	ok = do_createlink(ST_FILE, &err);
	check(ok == DOSTRUE && tab[12].next == make_anode && tab[10].next == 11 && tab[11].next == 12,
		"CreateLink: the new link is appended to the tail of the chain");
	check(master.xlink == first && changes == 0, "CreateLink: the object's entry is not rewritten for a later link");
	for (i = 0; i < NLENS; i++) {
		char msg[96];
		reset(); build_chain(first, LENS[i]); master.xlink = first;
		ok = do_createlink(ST_FILE, &err);
		sprintf(msg, "CreateLink: a link is appended to a chain of %d links", LENS[i]);
		check(ok == DOSTRUE && tab[first + LENS[i] - 1].next == make_anode && removed == 0 && nfreed == 0 && bounded(), msg);
	}

	/* a cyclic chain: reported, and the new link rolled back */
	reset();
	build_cycle(first, 5, 2);
	master.xlink = first;
	ok = do_createlink(ST_FILE, &err);
	printf("  CreateLink cyclic chain: rc=%d err=%ld get_calls=%ld runaway=%d\n", ok, (long)err, get_calls, runaway);
	check(ok == DOSFALSE && err == PFS_ERROR_INCONSISTENT && bounded(), "CreateLink: a cyclic link list fails with INCONSISTENT");
	check(removed == 1 && nfreed == 1 && freed[0] == make_anode, "CreateLink: the half-made link is removed and its anode freed");
	check(tab[first + 4].next == first + 2, "CreateLink: the chain is not modified");
}

static BOOL do_rollover(ULONG size, SIPTR *err)
{
	union objectinfo dir, res;
	char name[] = "roll";

	memset(&dir, 0, sizeof(dir)); memset(&res, 0, sizeof(res));
	*err = 0;
	return CreateRollover(&dir, name, size, &res, err, &gd);
}

static void test_rollover(void)
{
	SIPTR err;
	BOOL ok;

	reset();
	ok = do_rollover(3, &err);
	check(ok == DOSTRUE && err == 0 && newentry.fsize == 3 << 9, "CreateRollover: success returns TRUE with the size set");
	check(nfreed == 0 && detaches == 0 && removed == 0, "CreateRollover: nothing is released on success");

	reset(); make_ok = 0;
	ok = do_rollover(3, &err);
	check(ok == DOSFALSE && err == ERROR_DISK_FULL, "CreateRollover: MakeDirEntry failure gives DISK_FULL");
	check(chains == 0 && adds == 0 && nfreed == 0 && detaches == 0 && removed == 0, "CreateRollover: MakeDirEntry failure releases nothing");

	reset(); chain_ok = 0;
	ok = do_rollover(3, &err);
	check(ok == DOSFALSE && err == ERROR_DISK_FULL, "CreateRollover: GetAnodeChain failure gives DISK_FULL");
	check(nfreed == 1 && freed[0] == make_anode && detaches == 0 && removed == 0 && adds == 0,
		"CreateRollover: GetAnodeChain failure frees only the anode");

	reset(); add_ok = 0;
	ok = do_rollover(3, &err);
	check(ok == DOSFALSE && err == ERROR_DISK_FULL, "CreateRollover: AddDirectoryEntry failure gives DISK_FULL");
	check(nfreed == 1 && freed[0] == make_anode && detaches == 1 && removed == 0 && allocs == 0,
		"CreateRollover: AddDirectoryEntry failure detaches the chain and frees the anode");

	reset(); alloc_ok = 0;
	ok = do_rollover(3, &err);
	check(ok == DOSFALSE && err == ERROR_DISK_FULL, "CreateRollover: AllocateBlocksAC failure gives DISK_FULL");
	check(nfreed == 1 && freed[0] == make_anode && detaches == 1 && removed == 1,
		"CreateRollover: AllocateBlocksAC failure removes the entry, detaches the chain, frees the anode");
}

int main(void)
{
	test_walks();
	test_createlink();
	test_rollover();
	if (failures == 0) { printf("  PASS: dir_links_live: link walks follow any valid chain and stop on cycles, CreateLink/CreateRollover guards hold\n"); return 0; }
	printf("  FAIL: dir_links_live: %d check(s) failed\n", failures);
	return 1;
}
