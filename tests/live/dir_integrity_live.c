/* LIVE @src directory.c @func IsChildOf
 *
 * Proof of the directory integrity check. Lifts the checkout's real
 * IsChildOf and asserts CORRECT behaviour.
 *
 * IsChildOf (guards renaming a directory into its own subtree) must:
 *  - say "inside" for the directory itself and anything below it,
 *  - say "not inside" only when it really walked up to the volume root,
 *  - treat a GetParent failure (which does not return a parent) and a cyclic
 *    parent chain as an error, not as "not inside" and not as a hang,
 *  - not limit the depth: a valid chain of 5000 directories is walked to the
 *    root, as is one that ends in a cycle only after a long tail.
 * A tree that goes on with an unset parent after a GetParent failure, that
 * walks a cyclic chain forever, or that refuses a deep valid chain, FAILS;
 * the fixed one PASSES.
 */
#include <stdio.h>
#include <stdint.h>
typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uintptr_t IPTR;
typedef long SIPTR;
typedef short BOOL;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define ERROR_OBJECT_IN_USE 202
#define ERROR_DISK_NOT_VALIDATED 213
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED
#define SPECIAL_FLUSHED 3

typedef struct globaldata { int dummy; } globaldata;
struct direntry { ULONG anode; };
struct cdirblock { int dummy; };
union objectinfo { struct { struct direntry *direntry; struct cdirblock *dirblock; } file; };

/* mock volume: object 0 is the volume root (no direntry), object n has anode n */
#define NOBJ 12000
static int parent_of[NOBJ];			/* >= 0: parent, -1: GetParent fails */
static struct direntry ent[NOBJ];
static struct cdirblock blk;
static long calls;
static int runaway;
static union objectinfo obj(ULONG id)
{
	union objectinfo o;
	ent[id].anode = id;
	o.file.direntry = id ? &ent[id] : NULL;
	o.file.dirblock = &blk;
	return o;
}
#define IsSameOI(a, b) ((a.file.direntry == b.file.direntry) && (a.file.dirblock == b.file.dirblock))
static BOOL GetParent(union objectinfo *c, union objectinfo *up, SIPTR *error, globaldata *g)
{
	ULONG id = c->file.direntry ? c->file.direntry->anode : 0;
	(void)g;
	if (++calls > 100000) { runaway = 1; *error = 0; return FALSE; }
	if (id == 0) { *error = 0; return FALSE; }		/* root: no error, nothing returned */
	if (parent_of[id] < 0) { *error = PFS_ERROR_INCONSISTENT; return FALSE; }
	*up = obj(parent_of[id]);
	return TRUE;
}

#include "gen/IsChildOf.c"			/* the REAL function from ../../directory.c */

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static BOOL ischild(ULONG c, ULONG p, SIPTR *error)
{
	union objectinfo ci, pi;
	globaldata gd;

	ci = obj(c); pi = obj(p);
	calls = 0; runaway = 0;
	*error = 202 + 1;			/* stale value, as left by the caller's lookup */
	return IsChildOf(ci, pi, error, &gd);
}

int main(void)
{
	SIPTR e;

	/* 0 = volume; 1 in volume; 2 in 1; 3 in 2; 4 in volume; 5 broken; 6 in 5; 7 <-> 8 cycle */
	parent_of[1] = 0; parent_of[2] = 1; parent_of[3] = 2; parent_of[4] = 0;
	parent_of[5] = -1; parent_of[6] = 5; parent_of[7] = 8; parent_of[8] = 7;
	parent_of[9] = 9;					/* its own parent */
	/* 100..5099: a valid chain 5000 deep below the volume; 6000..6999 a tail leading into the cycle 7000 <-> 7001 */
	{
		int i;
		parent_of[100] = 0;
		for (i = 101; i < 5100; i++) parent_of[i] = i - 1;
		parent_of[6000] = 7000;
		for (i = 6001; i < 7000; i++) parent_of[i] = i - 1;
		parent_of[7000] = 7001; parent_of[7001] = 7000;
	}

	CHECK(ischild(3, 1, &e) && e == ERROR_OBJECT_IN_USE, "grandchild is inside (err %ld)", (long)e);
	CHECK(ischild(2, 2, &e) && e == ERROR_OBJECT_IN_USE, "directory is inside itself (err %ld)", (long)e);
	CHECK(!ischild(4, 1, &e), "sibling must not be inside");
	CHECK(!ischild(0, 1, &e), "volume must not be inside a directory");
	CHECK(!ischild(3, 4, &e), "other branch must not be inside");
	CHECK(ischild(6, 1, &e) && e == PFS_ERROR_INCONSISTENT,
		  "GetParent failure must be reported (err %ld)", (long)e);
	CHECK(ischild(7, 1, &e) && e == PFS_ERROR_INCONSISTENT && !runaway,
		  "cyclic parent chain must stop with an error (err %ld, runaway %d)", (long)e, runaway);

	CHECK(ischild(9, 1, &e) && e == PFS_ERROR_INCONSISTENT && !runaway,
		  "a directory that is its own parent must stop with an error (err %ld, runaway %d)", (long)e, runaway);
	CHECK(!ischild(5099, 4, &e) && !runaway,
		  "a valid chain 5000 deep must be walked to the root (err %ld, runaway %d)", (long)e, runaway);
	CHECK(ischild(5099, 100, &e) && e == ERROR_OBJECT_IN_USE,
		  "the top of a 5000 deep chain is found (err %ld)", (long)e);
	CHECK(ischild(6999, 4, &e) && e == PFS_ERROR_INCONSISTENT && !runaway,
		  "a long tail ending in a cycle must stop with an error (err %ld, runaway %d)", (long)e, runaway);

	if (failures) { printf("dir_integrity_live: %d FAILED\n", failures); return 1; }
	printf("dir_integrity_live: all checks passed\n");
	return 0;
}
