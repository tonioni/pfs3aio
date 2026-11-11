/* LIVE @src directory.c @func DirIsEmpty @src anodes.c @func GetNextAnode
 *
 * Lifts the checkout's real DirIsEmpty. A directory keeps its emptied
 * directory blocks, so its anode chain can hold many blocks without an entry.
 *
 *  - empty: every block of the chain has no entry, also for long chains;
 *  - not empty: the first block with an entry ends the walk with
 *    ERROR_DIRECTORY_NOT_EMPTY;
 *  - a cyclic chain of empty blocks (corrupt disk), also a long tail into a
 *    cycle, ends with PFS_ERROR_INCONSISTENT (GetNextAnode, Brent);
 *  - an unreadable anode or directory block: PFS_ERROR_INCONSISTENT.
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
#define ERROR_DIRECTORY_NOT_EMPTY 216	/* dos/dos.h */
#define PFS_ERROR_INCONSISTENT 213		/* ERROR_DISK_NOT_VALIDATED, as in struct.h */

struct direntry { UBYTE next; BYTE type; };
struct dirblock { UBYTE entries[4]; };
struct cdirblock { ULONG blocknr; struct dirblock blk; };
#define FIRSTENTRY(blok) ((struct direntry*)((blok)->blk.entries))
typedef struct canode { ULONG clustersize, blocknr, next, nr; } canode_t;
typedef struct globaldata { int dummy; } globaldata;

/* anode table: tbl_next[nr], block nr + 1000; a block is empty unless full[nr] */
#define NTAB 140000
static ULONG tbl_next[NTAB]; static UBYTE tbl_ok[NTAB], full[NTAB], unloadable[NTAB];
static int getanode_calls, runaway;

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

static struct cdirblock blk;
static struct cdirblock *LoadDirBlock(ULONG blocknr, globaldata *g)
{
	ULONG nr = blocknr - 1000;
	(void)g;
	if (nr >= NTAB || unloadable[nr])
		return NULL;
	memset(&blk, 0, sizeof(blk));
	blk.blocknr = blocknr;
	FIRSTENTRY(&blk)->next = full[nr] ? 40 : 0;
	return &blk;
}

struct anodewalk { ULONG mark; ULONG steps; };
#include "gen/GetNextAnode.c"			/* the REAL function from ../../anodes.c */
#include "gen/DirIsEmpty.c"			/* the REAL function from ../../directory.c */

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata gd;

static void chain(ULONG n)			/* anodes 1 .. n, all blocks empty */
{
	ULONG i;
	memset(tbl_next, 0, sizeof(tbl_next)); memset(tbl_ok, 0, sizeof(tbl_ok));
	memset(full, 0, sizeof(full)); memset(unloadable, 0, sizeof(unloadable));
	for (i = 1; i <= n; i++) { tbl_ok[i] = 1; tbl_next[i] = (i < n) ? i + 1 : 0; }
	getanode_calls = runaway = 0;
}

static BOOL run(SIPTR *e)
{
	*e = 0;
	return DirIsEmpty(1, e, &gd);
}

int main(void)
{
	SIPTR e;
	BOOL r;

	/* empty directories, short and long */
	{
		static const ULONG lens[] = { 1, 3, 65536, 131072 };
		int i;
		for (i = 0; i < 4; i++) {
			chain(lens[i]);
			r = run(&e);
			CHECK(r == DOSTRUE && getanode_calls == (int)lens[i],
				  "%u empty blocks: empty (r=%d e=%ld reads %d)", (unsigned)lens[i], r, (long)e, getanode_calls);
		}
	}

	/* an entry in the first, a middle and the last block */
	chain(5); full[1] = 1;
	r = run(&e);
	CHECK(r == DOSFALSE && e == ERROR_DIRECTORY_NOT_EMPTY && getanode_calls == 1, "entry in block 1: not empty, nothing more read");
	chain(5); full[3] = 1;
	r = run(&e);
	CHECK(r == DOSFALSE && e == ERROR_DIRECTORY_NOT_EMPTY && getanode_calls == 3, "entry in block 3: not empty after 3 reads");
	chain(70000); full[70000] = 1;
	r = run(&e);
	CHECK(r == DOSFALSE && e == ERROR_DIRECTORY_NOT_EMPTY, "entry in block 70000: not empty (r=%d e=%ld)", r, (long)e);

	/* cyclic chains of empty blocks */
	chain(1); tbl_next[1] = 1;
	r = run(&e);
	CHECK(!runaway && r == DOSFALSE && e == PFS_ERROR_INCONSISTENT,
		  "block pointing at itself: INCONSISTENT (r=%d e=%ld runaway %d)", r, (long)e, runaway);
	chain(2); tbl_next[2] = 1;
	r = run(&e);
	CHECK(!runaway && r == DOSFALSE && e == PFS_ERROR_INCONSISTENT && getanode_calls <= 8,
		  "two blocks pointing at each other: INCONSISTENT (r=%d e=%ld reads %d)", r, (long)e, getanode_calls);
	chain(100000); tbl_next[100000] = 70000;
	r = run(&e);
	CHECK(!runaway && r == DOSFALSE && e == PFS_ERROR_INCONSISTENT,
		  "tail into a cycle: INCONSISTENT (r=%d e=%ld runaway %d)", r, (long)e, runaway);

	/* unreadable anode or block */
	chain(3); tbl_ok[3] = 0;
	r = run(&e);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT, "unreadable anode: INCONSISTENT");
	chain(3); unloadable[2] = 1;
	r = run(&e);
	CHECK(r == DOSFALSE && e == PFS_ERROR_INCONSISTENT, "unreadable block: INCONSISTENT");

	if (failures) { printf("dir_isempty_live: %d FAILED\n", failures); return 1; }
	printf("  PASS: DirIsEmpty walks empty chains of any length, stops on cyclic chains\n");
	return 0;
}
