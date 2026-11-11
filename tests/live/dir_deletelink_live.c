/* LIVE @src directory.c @func DeleteLink @func RemapLinks @src anodes.c @func GetNextAnode
 *
 * Lifts the checkout's real DeleteLink and RemapLinks (hard-link bookkeeping
 * of directory.c) and asserts the checks added to them. The other link
 * functions are in dir_links_live.c.
 *
 * DeleteLink removes a link: its entry is deleted, it is taken out of the
 * object's link list and its anode is freed.
 *  - The master (linked-to object) is fetched BEFORE the link entry is
 *    touched. An unreadable master (FetchObject fails with anything but
 *    OBJECT_NOT_FOUND) fails with PFS_ERROR_INCONSISTENT and leaves the link
 *    entry, the link anode and the link list exactly as they were (nothing
 *    freed, nothing saved, ChangeDirEntry not called).
 *  - A missing master (OBJECT_NOT_FOUND: a dangling link) is fine: the entry
 *    is deleted, the anode freed, no list is touched.
 *  - The master is fetched again after the entry is removed (it can move); a
 *    failing refetch (also with OBJECT_NOT_FOUND) is a link_failure.
 *  - Normal cases: the link is first in the list (the master is rewritten
 *    with the next link), in the middle, last, or not in the list at all.
 *  - No length limit: the walk along the master's list to find the
 *    predecessor follows a valid list of any length (5000 links here). A
 *    cyclic list is detected (GetNextAnode, Brent) and ends in link_failure
 *    instead of a hang (GetAnode is a mock that counts and caps the calls).
 * RemapLinks promotes the first link of a deleted master to be the master.
 *  - Promoting a DIRECTORY also sets the new parent in its dirblocks:
 *    SetDirParent(directory anode, new directory); a failure of
 *    SetDirParent or of UpdateLinkDir fails the call (-1, metadata_error).
 *  - Promoting a FILE never calls SetDirParent.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef uint8_t UBYTE;
typedef int8_t BYTE; typedef intptr_t SIPTR; typedef uintptr_t IPTR; typedef int16_t BOOL;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define ST_USERDIR 2
#define ST_LINKDIR 4
#define ST_FILE (-3)
#define ST_LINKFILE (-4)
#define ST_ROLLOVERFILE (-16)
#define MAX_ENTRYSIZE 128
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_DISK_NOT_VALIDATED 213
#define ERROR_DISK_FULL 221
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED		/* struct.h */
#define ENTER(x)

struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection, nlength, startofname, pad;
	ULONG xlink; UWORD xfsizex;			/* stand-ins for the extra fields */
};
struct cdirblock { int dummy; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct volumedata { BOOL metadata_error; };
union objectinfo { struct fileinfo file; };
struct canode { ULONG clustersize, blocknr, next, nr; };
struct extrafields { ULONG link; ULONG virtualsize; ULONG rollpointer; UWORD fsizex; };
typedef struct globaldata { struct volumedata *currentvolume; } globaldata;

/* ---- the synthetic anode table and the event log ---- */
#define NANODES 6000
#define CAP 100000				/* GetAnode calls after which a walk is declared runaway */
static struct { ULONG clustersize, blocknr, next; } tab[NANODES];
static struct direntry *entry_of[64];			/* FetchObject: anode number -> entry */
static struct cdirblock blk;
static struct direntry stale;				/* where a "moved" master used to be */
static long get_calls, saves, fetches, deletes, modifies, ucalls, dcalls;
static int runaway, moved, free_ok, uld_ok, sdp_ok, change_ok;
static ULONG fetch_dir[8], fetch_nr[8]; static SIPTR fetch_err[8];
static ULONG freed[8], nfreed;
static ULONG sdp_node, sdp_parent, uld_dir; static struct direntry *uld_entry;
static char ev[32]; static int nev;				/* F fetch, D delete entry, M change entry, S save anode, X free anode */
static void logc(char c) { if (nev < 30) ev[nev++] = c; ev[nev] = 0; }

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
	saves++; logc('S');
	tab[nr].clustersize = a->clustersize; tab[nr].blocknr = a->blocknr; tab[nr].next = a->next;
	return TRUE;
}
static BOOL FreeAnode(ULONG nr, globaldata *g)
{
	(void)g;
	logc('X');
	if (nfreed < 8) freed[nfreed] = nr;
	nfreed++;
	return free_ok;
}
static void GetExtraFields(struct direntry *d, struct extrafields *x)
{ memset(x, 0, sizeof(*x)); x->link = d->xlink; x->fsizex = d->xfsizex; }
static void AddExtraFields(struct direntry *d, struct extrafields *x) { d->xlink = x->link; d->xfsizex = x->fsizex; }
static BOOL FetchObject(ULONG dir, ULONG anodenr, union objectinfo *r, SIPTR *e, globaldata *g)
{
	int k = (int)++fetches;
	(void)g;
	logc('F');
	if (k < 8) { fetch_dir[k] = dir; fetch_nr[k] = anodenr; }
	if (k < 8 && fetch_err[k]) { *e = fetch_err[k]; return FALSE; }
	r->file.direntry = (moved && k == 1) ? &stale : entry_of[anodenr];
	r->file.dirblock = &blk;
	return TRUE;
}
static BOOL GetParent(union objectinfo *c, union objectinfo *p, SIPTR *e, globaldata *g)
{ (void)c; (void)e; (void)g; p->file.direntry = NULL; p->file.dirblock = &blk; return TRUE; }
static BOOL ChangeDirEntry(struct fileinfo from, struct direntry *to, union objectinfo *d, struct fileinfo *r, globaldata *g)
{
	(void)d; (void)g;
	if (!to) { deletes++; logc('D'); from.direntry->pad = 0xEE; return TRUE; }	/* pad: "entry deleted" marker */
	modifies++; logc('M');
	if (!change_ok) return FALSE;
	memcpy(from.direntry, to, to->next);		/* the entry takes the new contents */
	*r = from;
	return TRUE;
}
static BOOL UpdateLinkDir(struct direntry *o, ULONG newdiran, globaldata *g)
{ (void)g; ucalls++; uld_entry = o; uld_dir = newdiran; return uld_ok; }
static BOOL SetDirParent(ULONG dirnodenr, ULONG parentnr, globaldata *g)
{ (void)g; dcalls++; sdp_node = dirnodenr; sdp_parent = parentnr; return sdp_ok; }

/* state of a walk along an anode chain, as in anodes_protos.h */
struct anodewalk { ULONG mark; ULONG steps; };
#include "gen/GetNextAnode.c"			/* the REAL function from ../../anodes.c */
#include "gen/DeleteLink.c"			/* the REAL functions from ../../directory.c */
#include "gen/RemapLinks.c"

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

enum { LN = 10, LN2 = 11, LN3 = 12, MA = 20, MDIR = 40, LDIR = 41, CH = 100 };
static struct volumedata vol;
static globaldata gd = { &vol };
static struct direntry lent, ment;			/* the link entry and the master entry */
static struct fileinfo linkfi, masterfi;
static struct direntry lent_before, ment_before;
static struct { ULONG clustersize, blocknr, next; } tab_before[NANODES];

static void reset(void)
{
	memset(tab, 0, sizeof(tab)); memset(entry_of, 0, sizeof(entry_of));
	memset(fetch_err, 0, sizeof(fetch_err)); memset(fetch_dir, 0, sizeof(fetch_dir)); memset(fetch_nr, 0, sizeof(fetch_nr));
	memset(freed, 0, sizeof(freed)); nfreed = 0; nev = 0; ev[0] = 0;
	get_calls = saves = fetches = deletes = modifies = ucalls = dcalls = 0;
	runaway = moved = 0; free_ok = uld_ok = sdp_ok = change_ok = 1;
	vol.metadata_error = 0;
	memset(&stale, 0, sizeof(stale)); stale.next = sizeof(stale); stale.type = ST_FILE; stale.anode = MA; stale.xlink = 999;
	/* a link: entry in its own directory, anode LN names the master's directory and the master */
	memset(&lent, 0, sizeof(lent));
	lent.next = sizeof(lent); lent.type = ST_LINKFILE; lent.anode = LN; lent.xlink = MA;
	tab[LN].clustersize = MDIR; tab[LN].blocknr = LDIR; tab[LN].next = 0;
	linkfi.direntry = &lent; linkfi.dirblock = &blk;
	/* the master */
	memset(&ment, 0, sizeof(ment));
	ment.next = sizeof(ment); ment.type = ST_FILE; ment.anode = MA; ment.fsize = 777; ment.xlink = LN; ment.xfsizex = 5;
	masterfi.direntry = &ment; masterfi.dirblock = &blk;
	entry_of[MA] = &ment; entry_of[LN] = &lent;
}
static void snapshot(void) { memcpy(tab_before, tab, sizeof(tab)); lent_before = lent; ment_before = ment; }
static int untouched(void)
{
	return !memcmp(tab_before, tab, sizeof(tab)) && !memcmp(&lent_before, &lent, sizeof(lent)) &&
		   !memcmp(&ment_before, &ment, sizeof(ment));
}
static int bounded(void) { return !runaway; }

static BOOL delete_link(SIPTR *err)
{
	*err = 0;
	return DeleteLink(&linkfi, err, &gd);
}

/* master list: [pred ->] LN [-> next] built from 'pred' (0: LN is the head) and 'next' */
static void list_around(ULONG pred, ULONG next)
{
	tab[LN].next = next;
	if (pred) { ment.xlink = pred; tab[pred].clustersize = MDIR; tab[pred].blocknr = LDIR; tab[pred].next = LN; }
	else ment.xlink = LN;
}

static void test_deletelink(void)
{
	SIPTR err;
	BOOL ok;
	int i;
	SIPTR bad[3] = { ERROR_NO_FREE_STORE, PFS_ERROR_INCONSISTENT, ERROR_DISK_FULL };

	/* an unreadable master: nothing may change */
	for (i = 0; i < 3; i++) {
		reset(); list_around(LN2, LN3); tab[LN3].next = 0;
		fetch_err[1] = bad[i];
		snapshot();
		ok = delete_link(&err);
		printf("  unreadable master (err %ld): rc=%d err=%ld events=\"%s\"\n", (long)bad[i], ok, (long)err, ev);
		check(ok == DOSFALSE && err == PFS_ERROR_INCONSISTENT, "DeleteLink: an unreadable master fails with INCONSISTENT");
		check(deletes == 0 && modifies == 0 && saves == 0 && nfreed == 0 && !strcmp(ev, "F"),
			"DeleteLink: an unreadable master: entry not deleted, nothing saved or freed (master fetched first)");
		check(untouched(), "DeleteLink: an unreadable master leaves link entry, link anode and list unchanged");
	}

	/* a missing master (dangling link) is fine */
	reset(); list_around(LN2, LN3); tab[LN3].next = 0; fetch_err[1] = ERROR_OBJECT_NOT_FOUND;
	snapshot();
	ok = delete_link(&err);
	check(ok == DOSTRUE, "DeleteLink: a dangling link (master not found) can be deleted");
	check(deletes == 1 && nfreed == 1 && freed[0] == LN && saves == 0 && modifies == 0 && fetches == 1 && !strcmp(ev, "FDX"),
		"DeleteLink: dangling link: entry deleted, anode freed, nothing else touched");
	check(!memcmp(&ment_before, &ment, sizeof(ment)) && tab[LN2].next == LN && !vol.metadata_error,
		"DeleteLink: dangling link: no master or list is modified");
	reset(); fetch_err[1] = ERROR_OBJECT_NOT_FOUND; free_ok = 0;
	ok = delete_link(&err);
	check(ok == DOSFALSE && err == PFS_ERROR_INCONSISTENT && vol.metadata_error,
		"DeleteLink: dangling link whose anode cannot be freed is a link_failure");

	/* the refetch after the entry is removed */
	for (i = 0; i < 2; i++) {
		reset(); list_around(0, LN2);
		fetch_err[2] = i ? ERROR_OBJECT_NOT_FOUND : ERROR_NO_FREE_STORE;
		ok = delete_link(&err);
		printf("  failing refetch (err %ld): rc=%d err=%ld events=\"%s\"\n", (long)fetch_err[2], ok, (long)err, ev);
		check(ok == DOSFALSE && err == PFS_ERROR_INCONSISTENT && vol.metadata_error,
			"DeleteLink: a failing refetch of the master is a link_failure");
		check(nfreed == 0 && modifies == 0 && saves == 0 && !strcmp(ev, "FDF"),
			"DeleteLink: a failing refetch frees nothing and changes no master");
	}
	reset(); list_around(0, LN2); moved = 1;
	ok = delete_link(&err);
	check(ok == DOSTRUE && ment.xlink == LN2 && stale.xlink == 999 && modifies == 1,
		"DeleteLink: the refetched master (not the first fetch) is the one that is rewritten");

	/* normal cases */
	reset(); list_around(0, LN2);
	ok = delete_link(&err);
	check(ok == DOSTRUE && !strcmp(ev, "FDFMX"), "DeleteLink: first link: fetch, delete entry, refetch, rewrite master, free");
	check(ment.xlink == LN2 && nfreed == 1 && freed[0] == LN && saves == 0 && !vol.metadata_error,
		"DeleteLink: first link: the master's list now starts at the next link");
	check(fetch_dir[1] == MDIR && fetch_nr[1] == MA && fetch_dir[2] == MDIR && fetch_nr[2] == MA,
		"DeleteLink: the master is fetched by (its directory, its anode), both times");
	reset(); list_around(LN2, LN3); tab[LN3].next = 0;
	ok = delete_link(&err);
	check(ok == DOSTRUE && !strcmp(ev, "FDFSX") && tab[LN2].next == LN3 && tab[LN3].next == 0 && ment.xlink == LN2 && modifies == 0,
		"DeleteLink: middle link is unlinked from its predecessor, master untouched");
	check(nfreed == 1 && freed[0] == LN, "DeleteLink: middle link: only its own anode is freed");
	reset(); list_around(LN2, 0);
	ok = delete_link(&err);
	check(ok == DOSTRUE && tab[LN2].next == 0 && nfreed == 1 && freed[0] == LN, "DeleteLink: last link: predecessor ends the list");
	reset(); ment.xlink = LN2; tab[LN2].next = 0;		/* the list does not contain our link at all */
	snapshot();
	ok = delete_link(&err);
	check(ok == DOSTRUE && saves == 0 && tab[LN2].next == 0 && nfreed == 1 && freed[0] == LN && !vol.metadata_error,
		"DeleteLink: a link missing from the list is just freed");

	/* the walk for the predecessor: any valid length, cycles detected */
	reset(); ment.xlink = CH;
	for (i = 0; i < 4000; i++) { tab[CH + i].next = (i == 3999) ? LN : CH + i + 1; }
	tab[LN].next = LN2;
	ok = delete_link(&err);
	check(ok == DOSTRUE && tab[CH + 3999].next == LN2 && nfreed == 1 && freed[0] == LN && bounded(),
		"DeleteLink: a list of 4000 links is walked to the predecessor");
	reset(); ment.xlink = CH;				/* 4096 and 4097 links: the sizes the old fixed bound sat at */
	for (i = 0; i < 4097; i++) { tab[CH + i].next = (i == 4096) ? LN : CH + i + 1; }
	ok = delete_link(&err);
	check(ok == DOSTRUE && tab[CH + 4096].next == 0 && nfreed == 1 && freed[0] == LN && !vol.metadata_error && bounded(),
		"DeleteLink: a list of 4098 links works");
	reset(); ment.xlink = CH;
	for (i = 0; i < 5000; i++) { tab[CH + i].next = (i == 4999) ? LN : CH + i + 1; }
	ok = delete_link(&err);
	check(ok == DOSTRUE && tab[CH + 4999].next == 0 && nfreed == 1 && freed[0] == LN && !vol.metadata_error && bounded(),
		"DeleteLink: a list of 5000 links works, no metadata error");
	reset(); ment.xlink = CH;				/* cycle that does not contain LN */
	tab[CH].next = CH + 1; tab[CH + 1].next = CH + 2; tab[CH + 2].next = CH + 1;
	ok = delete_link(&err);
	printf("  cyclic list: rc=%d err=%ld get_calls=%ld runaway=%d\n", ok, (long)err, get_calls, runaway);
	check(ok == DOSFALSE && err == PFS_ERROR_INCONSISTENT && vol.metadata_error && nfreed == 0 && bounded(),
		"DeleteLink: a cyclic list terminates with INCONSISTENT");
}

/* RemapLinks: master 'mtype' is deleted, the first link LN (next link LN2) becomes the master;
 * uld/sdp: what the UpdateLinkDir/SetDirParent mocks return */
static LONG remap(int mtype, int uld, int sdp)
{
	reset();
	ment.type = mtype; ment.xlink = LN; tab[LN].next = LN2;
	lent.type = (mtype == ST_USERDIR) ? ST_LINKDIR : ST_LINKFILE;
	uld_ok = uld; sdp_ok = sdp;
	return RemapLinks(&masterfi, &gd);
}

static void test_remaplinks(void)
{
	LONG rc;
	int i, ftypes[2] = { ST_FILE, ST_ROLLOVERFILE };

	/* promoted directory: the new parent is set in its dirblocks */
	rc = remap(ST_USERDIR, 1, 1);
	check(rc == TRUE, "RemapLinks: promoting a directory succeeds");
	check(dcalls == 1 && sdp_node == MA && sdp_parent == LDIR, "RemapLinks: a promoted directory gets SetDirParent(its anode, the link's directory)");
	check(ucalls == 1 && uld_entry == &lent && uld_dir == LDIR, "RemapLinks: UpdateLinkDir gets the promoted entry and the link's directory");
	check(lent.type == ST_USERDIR && lent.anode == MA && lent.fsize == 777 && lent.xlink == LN2 && lent.xfsizex == 5,
		"RemapLinks: the link entry becomes the master (type, anode, size, rest of the list)");
	check(deletes == 1 && nfreed == 1 && freed[0] == LN && !vol.metadata_error, "RemapLinks: old master entry deleted, link anode freed");

	rc = remap(ST_USERDIR, 1, 0);
	check(rc == -1 && vol.metadata_error && dcalls == 1, "RemapLinks: a failing SetDirParent fails the call and flags the metadata");
	rc = remap(ST_USERDIR, 0, 1);
	check(rc == -1 && vol.metadata_error, "RemapLinks: a failing UpdateLinkDir fails the call and flags the metadata (directory)");

	/* promoted file: SetDirParent has nothing to do */
	for (i = 0; i < 2; i++) {
		rc = remap(ftypes[i], 1, 1);
		check(rc == TRUE && dcalls == 0 && ucalls == 1 && !vol.metadata_error, "RemapLinks: promoting a file succeeds without SetDirParent");
		check(lent.type == ftypes[i] && lent.anode == MA && lent.xlink == LN2, "RemapLinks: the link entry becomes the master (file)");
		rc = remap(ftypes[i], 1, 0);
		check(rc == TRUE && dcalls == 0 && !vol.metadata_error, "RemapLinks: SetDirParent is not consulted for a file");
		rc = remap(ftypes[i], 0, 1);
		check(rc == -1 && vol.metadata_error && dcalls == 0, "RemapLinks: a failing UpdateLinkDir fails the call (file)");
	}

	/* no link: nothing to promote */
	reset(); ment.xlink = 0;
	rc = RemapLinks(&masterfi, &gd);
	check(rc == FALSE && nfreed == 0 && deletes == 0 && ucalls == 0 && dcalls == 0, "RemapLinks: no link, nothing is promoted");
}

int main(void)
{
	test_deletelink();
	test_remaplinks();
	if (failures == 0) { printf("  PASS: dir_deletelink_live: DeleteLink fails safe before changing anything, RemapLinks sets the directory parent\n"); return 0; }
	printf("  FAIL: dir_deletelink_live: %d check(s) failed\n", failures);
	return 1;
}
