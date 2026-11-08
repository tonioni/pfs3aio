/* LIVE @src directory.c @func NewFile
 *
 * Lifts the checkout's real NewFile and asserts what becomes of the OLD file's
 * anode chain when a file is overwritten with the deleted-directory (deldir)
 * enabled. To make room for the old version NewFile moves the chain's head
 * anode to a freshly allocated anode and resets the original one to "empty"
 * (the file is about to be rewritten). If AddToDeldir then fails, the head
 * must go back where the directory entry points, otherwise the old data
 * blocks belong to no anode any more and are lost.
 *
 *  - AddToDeldir fails: the original anode holds the original head again, the
 *    replacement anode is freed, and the entry still points at the original;
 *  - AddToDeldir succeeds: the original anode is empty and the replacement
 *    holds the old head (what the deldir keeps) -- no over-restoring;
 *  - deldir disabled: the original anode is simply reset, nothing is
 *    allocated.
 *
 * A tree that only frees the replacement anode on a failed AddToDeldir leaves
 * the original anode reset to empty while the entry still lists the old size.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef intptr_t SIPTR; typedef uint8_t UBYTE;
typedef int8_t BYTE; typedef uint16_t UWORD; typedef int BOOL; typedef uint32_t FSIZE;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define VERSION23 1
#define DELDIR 1
#define ULONG_MAX 0xffffffffu
#define MAX_ENTRYSIZE 512
#define ST_FILE (-3)
#define ST_LINKFILE (-4)
#define ST_LINKDIR 4
#define ST_ROLLOVERFILE (-16)
#define FIBF_DELETE 1
#define SPECIAL_DELDIR 1
#define ERROR_WRITE_PROTECTED 223
#define ERROR_DISK_FULL 221
#define ERROR_INVALID_COMPONENT_NAME 210
#define ERROR_DELETE_PROTECTED 222
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_NO_FREE_STORE 103
#define PFS_ERROR_INCONSISTENT 0x7001

struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection; UBYTE nlength; UBYTE startofname; UBYTE pad;
};
struct cdirblock { int dummy; };
struct cachedblock { int dummy; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct volumeinfo { ULONG root; void *volume; };
struct deldirinfo { ULONG special; void *volume; };
union objectinfo { struct fileinfo file; struct volumeinfo volume; struct deldirinfo deldir; };
typedef struct canode { ULONG clustersize, blocknr, next, nr; } canode_t;
struct anodechainnode { struct anodechainnode *next; struct canode an; };
struct anodechain { struct anodechain *next, *prev; ULONG refcount; struct anodechainnode head; };
struct extrafields { ULONG link, virtualsize, rollpointer; };
struct volumedata { int fileentries; };
typedef struct globaldata {
	struct volumedata *currentvolume; int fnsize; UBYTE dirextension, deldirenabled;
} globaldata;
enum freeblocktype { keepanodes, freeanodes };

static struct { BOOL res_alert; } alloc_data;
#define ReservedAreaIsLocked (alloc_data.res_alert)
#define FILENAMESIZE (g->fnsize)
#define FIANODENR(fi) ((fi)->direntry->anode)
#define IsVolume(oi) ((oi).volume.root==0)
#define IsDelDir(oi) ((oi).deldir.special==SPECIAL_DELDIR)
#define HeadOf(list) ((void *)0)
#define DB(x)

/* the anode table: what is "on disk" */
#define NANODES 256
static struct { ULONG clustersize, blocknr, next; int used; } tab[NANODES];
static ULONG nextfree;
static int detached, freed_anodes, addtodeldir_ok, alloc_fails;
static struct anodechain chain;

static BOOL CheckVolume(struct volumedata *v, BOOL w, SIPTR *e, globaldata *g) { (void)v; (void)w; (void)e; (void)g; return TRUE; }
static BOOL ScanLockList(void *l, ULONG nr) { (void)l; (void)nr; return FALSE; }
static void GetExtraFields(struct direntry *d, struct extrafields *x) { (void)d; memset(x, 0, sizeof(*x)); }
static void AddExtraFields(struct direntry *d, struct extrafields *x) { (void)d; (void)x; }
static BOOL GetAnode(struct canode *a, ULONG nr, globaldata *g) { (void)g; a->clustersize = tab[nr].clustersize; a->blocknr = tab[nr].blocknr; a->next = tab[nr].next; a->nr = nr; return TRUE; }
static BOOL FetchObject(ULONG d, ULONG t, union objectinfo *r, SIPTR *e, globaldata *g) { (void)d; (void)t; (void)r; (void)e; (void)g; return FALSE; }
static BOOL GetParent(union objectinfo *c, union objectinfo *p, SIPTR *e, globaldata *g) { (void)c; (void)p; (void)e; (void)g; return FALSE; }
static struct anodechain *GetAnodeChain(ULONG nr, globaldata *g)
{
	(void)g;
	chain.head.an.clustersize = tab[nr].clustersize; chain.head.an.blocknr = tab[nr].blocknr;
	chain.head.an.next = tab[nr].next; chain.head.an.nr = nr;
	return &chain;
}
static void DetachAnodeChain(struct anodechain *c, globaldata *g) { (void)c; (void)g; detached++; }
static int AllocDeldirSlot(globaldata *g) { (void)g; return 0; }
static BOOL AddToDeldir(union objectinfo *i, int slot, globaldata *g) { (void)i; (void)slot; (void)g; return addtodeldir_ok; }
static ULONG AllocAnode(ULONG connect, globaldata *g)
{
	ULONG nr;
	(void)connect; (void)g;
	if (alloc_fails) return 0;
	nr = nextfree++;
	tab[nr].used = 1;
	return nr;
}
static void FreeAnode(ULONG nr, globaldata *g) { (void)g; tab[nr].used = 0; freed_anodes++; }
static BOOL SaveAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	tab[nr].clustersize = a->clustersize; tab[nr].blocknr = a->blocknr; tab[nr].next = a->next;
	return TRUE;
}
static BOOL FreeBlocksAC(struct anodechain *c, ULONG size, enum freeblocktype t, globaldata *g) { (void)c; (void)size; (void)t; (void)g; return TRUE; }
static void SetDEFileSize(struct direntry *d, FSIZE s, globaldata *g) { (void)g; d->fsize = s; }
static BOOL MakeBlockDirty(struct cachedblock *b, globaldata *g) { (void)b; (void)g; return TRUE; }
static BOOL ChangeDirEntry(struct fileinfo from, struct direntry *to, union objectinfo *d, struct fileinfo *r, globaldata *g) { (void)from; (void)to; (void)d; (void)r; (void)g; return TRUE; }
static BOOL MakeDirEntry(BYTE type, UBYTE *name, UBYTE *buf, globaldata *g) { (void)type; (void)name; (void)buf; (void)g; return FALSE; }
static BOOL AddDirectoryEntry(union objectinfo *d, struct direntry *e, struct fileinfo *r, globaldata *g) { (void)d; (void)e; (void)r; (void)g; return FALSE; }

#include "gen/NewFile.c"                     /* the REAL function from ../../directory.c */

#define OLD 10
#define OLD_CLUSTER 8
#define OLD_BLOCK 1234
#define OLD_NEXT 77

static struct direntry de;
static struct cdirblock blk;

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}
static int is_empty(ULONG a) { return tab[a].clustersize == 0 && tab[a].blocknr == 0xffffffff && tab[a].next == 0; }
static int is_old(ULONG a) { return tab[a].clustersize == OLD_CLUSTER && tab[a].blocknr == OLD_BLOCK && tab[a].next == OLD_NEXT; }

static ULONG overwrite(int deldir, int ddok, int allocfail)
{
	struct volumedata vol;
	globaldata g = { &vol, 31, 0, (UBYTE)deldir };
	union objectinfo dir, nf;
	char name[] = "file";

	memset(tab, 0, sizeof(tab)); nextfree = 100;
	tab[OLD].clustersize = OLD_CLUSTER; tab[OLD].blocknr = OLD_BLOCK; tab[OLD].next = OLD_NEXT; tab[OLD].used = 1;
	memset(&de, 0, sizeof(de));
	de.next = sizeof(de); de.type = ST_FILE; de.anode = OLD; de.fsize = 40000;
	memset(&dir, 0, sizeof(dir));            /* a real directory: neither volume nor deldir */
	dir.volume.root = 5;
	memset(&nf, 0, sizeof(nf));
	nf.file.direntry = &de; nf.file.dirblock = &blk;
	detached = freed_anodes = 0;
	alloc_fails = allocfail;
	addtodeldir_ok = ddok;
	return NewFile(TRUE, &dir, name, &nf, &g);
}

int main(void)
{
	ULONG rc;

	rc = overwrite(1, 0, 0);
	printf("  deldir, AddToDeldir fails: rc=0x%x  orig anode: cluster=%u block=%u next=%u  replacement(100) used=%d  entry->anode=%u\n",
		rc, tab[OLD].clustersize, tab[OLD].blocknr, tab[OLD].next, tab[100].used, de.anode);
	check(rc == PFS_ERROR_INCONSISTENT, "a failed AddToDeldir must be reported");
	check(is_old(OLD), "the original anode must hold the old chain's head again (the old data blocks are otherwise orphaned)");
	check(!tab[100].used, "the replacement anode must be freed");
	check(de.anode == OLD, "the directory entry must still point at the original anode");
	check(de.fsize == 40000, "the entry must keep its size: the file was not overwritten");
	check(detached == 1, "the anode chain must be released exactly once");

	rc = overwrite(1, 1, 0);
	printf("  deldir, AddToDeldir ok:    rc=%u  orig anode empty=%d  replacement(100) old head=%d used=%d\n",
		rc, is_empty(OLD), is_old(100), tab[100].used);
	check(rc == 0, "overwriting with deldir must succeed");
	check(is_empty(OLD), "the original anode must be reset: the file is rewritten from empty");
	check(is_old(100) && tab[100].used, "the replacement anode must keep the old head for the deldir");
	check(de.anode == OLD && de.fsize == 0, "the entry keeps its anode and starts again at size 0");

	rc = overwrite(0, 1, 0);
	printf("  no deldir:                 rc=%u  orig anode empty=%d  freed=%d\n", rc, is_empty(OLD), freed_anodes);
	check(rc == 0, "overwriting without deldir must succeed");
	check(is_empty(OLD) && !tab[100].used && nextfree == 100, "the anode is reset in place, nothing is allocated");

	/* no anode left for the replacement: nothing may have been touched */
	rc = overwrite(1, 1, 1);
	printf("  deldir, no free anode:     rc=%u  orig anode intact=%d\n", rc, is_old(OLD));
	check(rc == ERROR_DISK_FULL, "no free anode must be reported as disk full");
	check(is_old(OLD) && de.anode == OLD && de.fsize == 40000, "nothing may change when the replacement anode cannot be allocated");

	if (failures == 0) { printf("  PASS: a failed deldir overwrite puts the old chain's head back\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
