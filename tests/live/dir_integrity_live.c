/* LIVE @src directory.c @func IsChildOf
 *
 * Proof of the directory integrity check. Lifts the checkout's real
 * IsChildOf and asserts CORRECT behaviour.
 *
 * IsChildOf (guards renaming a directory into its own subtree) must:
 *  - say "inside" for the directory itself and anything below it,
 *  - say "not inside" only when it really walked up to the volume root,
 *  - treat a GetParent failure (which does not return a parent) and a cyclic
 *    parent chain as an error, not as "not inside" and not as a hang.
 * A tree that goes on with an unset parent after a GetParent failure, or
 * that walks a cyclic chain forever, FAILS; the fixed one PASSES.
 */
#include <stdio.h>
#include <stdint.h>
typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef long SIPTR;
typedef short BOOL;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define ERROR_OBJECT_IN_USE 202
#define ERROR_DISK_NOT_VALIDATED 213
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED
#define MAXWALK 4096			/* as in directory.c */

typedef struct globaldata { int dummy; } globaldata;
union objectinfo { struct { ULONG id; } file; };

/* mock volume: object 0 is the volume root */
static int parent_of[16];			/* >= 0: parent, -1: GetParent fails */
static long calls;
static int runaway;
static BOOL IsSameOI(union objectinfo a, union objectinfo b) { return a.file.id == b.file.id; }
static BOOL GetParent(union objectinfo *c, union objectinfo *up, SIPTR *error, globaldata *g)
{
	(void)g;
	if (++calls > 100000) { runaway = 1; *error = 0; return FALSE; }
	if (c->file.id == 0) { *error = 0; return FALSE; }		/* root: no error, nothing returned */
	if (parent_of[c->file.id] < 0) { *error = PFS_ERROR_INCONSISTENT; return FALSE; }
	up->file.id = parent_of[c->file.id];
	return TRUE;
}

#include "gen/IsChildOf.c"			/* the REAL function from ../../directory.c */

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static BOOL ischild(ULONG c, ULONG p, SIPTR *error)
{
	union objectinfo ci, pi;
	globaldata gd;

	ci.file.id = c; pi.file.id = p;
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

	CHECK(ischild(3, 1, &e) && e == ERROR_OBJECT_IN_USE, "grandchild is inside (err %ld)", (long)e);
	CHECK(ischild(2, 2, &e) && e == ERROR_OBJECT_IN_USE, "directory is inside itself (err %ld)", (long)e);
	CHECK(!ischild(4, 1, &e), "sibling must not be inside");
	CHECK(!ischild(0, 1, &e), "volume must not be inside a directory");
	CHECK(!ischild(3, 4, &e), "other branch must not be inside");
	CHECK(ischild(6, 1, &e) && e == PFS_ERROR_INCONSISTENT,
		  "GetParent failure must be reported (err %ld)", (long)e);
	CHECK(ischild(7, 1, &e) && e == PFS_ERROR_INCONSISTENT && !runaway,
		  "cyclic parent chain must stop with an error (err %ld, runaway %d)", (long)e, runaway);

	if (failures) { printf("dir_integrity_live: %d FAILED\n", failures); return 1; }
	printf("dir_integrity_live: all checks passed\n");
	return 0;
}
