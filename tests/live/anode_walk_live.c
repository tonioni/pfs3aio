/* LIVE @src anodes.c @func GetNextAnode @func CorrectAnode
 *
 * CorrectAnode walks an anode chain to the anode that contains *anodeoffset.
 * It now follows the chain with GetNextAnode and a walk record (mark = the
 * first anode's number, steps = 0), so a corrupt chain whose `next` links
 * loop back - also an anode that links to itself - ends with DOSFALSE instead
 * of walking forever. Lifts the real GetNextAnode and CorrectAnode from
 * anodes.c; GetAnode is a mock that counts its calls and gives up (runaway)
 * after 100000 of them, which is how an endless loop shows up here.
 *
 * Proven:
 *  - offset inside the first anode: TRUE, offset and anode untouched, no read
 *    (also when that anode's next points at itself)
 *  - valid chains (short, every length 1..300, 70000 anodes): the right anode
 *    is reached and *anodeoffset is reduced by the cluster sizes passed;
 *    a valid chain is never rejected, whatever its length
 *  - offset beyond the chain's end: DOSFALSE
 *  - cyclic chains (self loop, 2-cycle, a tail leading into a loop, loop not
 *    containing the start): DOSFALSE after only a handful of reads
 *  - a failing GetAnode (first and later step): DOSFALSE
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1L)
#define DOSFALSE (0L)

struct canode { ULONG clustersize, blocknr, next, nr; };
struct anodewalk { ULONG mark, steps; };
typedef struct globaldata { int dummy; } globaldata;

#define CAP 100000UL
#define MAXN 70010
static ULONG csize[MAXN], nextof[MAXN];
static int failnr;			/* GetAnode fails for this anode number (0: never) */
static ULONG calls, nodes_known;
static int runaway;

/* what the real GetAnode leaves behind on failure: clustersize = next = 0,
 * blocknr = ~0, nr = anodenr */
static BOOL GetAnode(struct canode *anode, ULONG anodenr, globaldata *g)
{
	(void)g;
	if (++calls > CAP)
	{
		runaway = 1;
		goto failed;
	}
	if (anodenr == 0 || anodenr >= nodes_known || (ULONG)failnr == anodenr)
		goto failed;
	anode->clustersize = csize[anodenr];
	anode->blocknr = 0x500 + anodenr;
	anode->next = nextof[anodenr];
	anode->nr = anodenr;
	return TRUE;
failed:
	anode->clustersize = anode->next = 0;
	anode->blocknr = 0xFFFFFFFFUL;
	anode->nr = anodenr;
	return FALSE;
}

#include "gen/GetNextAnode.c"                  /* the REAL functions from ../../anodes.c */
#include "gen/CorrectAnode.c"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata G;

static void table(ULONG n)
{
	memset(csize, 0, sizeof csize);
	memset(nextof, 0, sizeof nextof);
	nodes_known = n + 1;
	failnr = 0;
}

/* start at anode 1 and let CorrectAnode move *off */
static BOOL run(ULONG *off, struct canode *an, ULONG *ncalls)
{
	BOOL r;

	calls = 0; runaway = 0;
	if (!GetAnode(an, 1, &G))
		return 2;
	calls = 0;
	r = CorrectAnode(an, off, &G);
	*ncalls = calls;
	return r;
}

static void expect(const char *name, ULONG off, int want, ULONG want_nr, ULONG want_off, ULONG maxcalls)
{
	struct canode an;
	ULONG ncalls, o = off;
	BOOL r = run(&o, &an, &ncalls);

	CHECK(!runaway, "%s: endless loop (GetAnode called more than %lu times)", name, CAP);
	CHECK(r == want, "%s: returned %ld, expected %ld", name, (long)r, (long)want);
	CHECK(ncalls <= maxcalls, "%s: %lu anode reads, expected at most %lu", name, (unsigned long)ncalls, (unsigned long)maxcalls);
	if (want == DOSTRUE)
	{
		CHECK(an.nr == want_nr, "%s: ended at anode %lu, expected %lu", name, (unsigned long)an.nr, (unsigned long)want_nr);
		CHECK(o == want_off, "%s: offset %lu, expected %lu", name, (unsigned long)o, (unsigned long)want_off);
	}
}

int main(void)
{
	ULONG i, n, o, nc;
	struct canode an;
	char name[64];

	/* 1 -> 2 -> 3 -> 4, cluster sizes 4, 8, 2, 16 (so anode k starts at 0, 4, 12, 14) */
	table(4);
	csize[1] = 4; csize[2] = 8; csize[3] = 2; csize[4] = 16;
	nextof[1] = 2; nextof[2] = 3; nextof[3] = 4; nextof[4] = 0;
	expect("first anode, offset 0", 0, DOSTRUE, 1, 0, 0);
	expect("first anode, last offset", 3, DOSTRUE, 1, 3, 0);
	expect("first block of anode 2", 4, DOSTRUE, 2, 0, 1);
	expect("last block of anode 2", 11, DOSTRUE, 2, 7, 1);
	expect("first block of anode 3", 12, DOSTRUE, 3, 0, 2);
	expect("second block of anode 3", 13, DOSTRUE, 3, 1, 2);
	expect("first block of anode 4", 14, DOSTRUE, 4, 0, 3);
	expect("last block of anode 4", 29, DOSTRUE, 4, 15, 3);
	expect("one past the end", 30, DOSFALSE, 0, 0, 3);
	expect("far past the end", 0xFFFFFFF0UL, DOSFALSE, 0, 0, 3);

	/* offset inside the first anode is fine even if that anode links to itself */
	table(1);
	csize[1] = 8; nextof[1] = 1;
	expect("self loop, offset inside", 7, DOSTRUE, 1, 7, 0);

	/* cycles: every walk beyond the cycle must end in DOSFALSE, quickly */
	table(1);
	csize[1] = 1; nextof[1] = 1;
	/* the walk is primed with the start anode, so a self link is refused before any read */
	expect("next == self", 10, DOSFALSE, 0, 0, 0);
	table(2);
	csize[1] = csize[2] = 1; nextof[1] = 2; nextof[2] = 1;
	expect("loop 1-2-1", 1000, DOSFALSE, 0, 0, 8);
	table(6);
	for (i = 1; i <= 6; i++) csize[i] = 1;
	nextof[1] = 2; nextof[2] = 3; nextof[3] = 4; nextof[4] = 5; nextof[5] = 6; nextof[6] = 3;
	expect("tail into loop 1 2 3 4 5 6 3", 1000000, DOSFALSE, 0, 0, 40);
	expect("tail into loop, offset inside the tail", 3, DOSTRUE, 4, 0, 3);
	table(6);
	for (i = 1; i <= 6; i++) csize[i] = 0;		/* zero sized anodes never absorb the offset */
	nextof[1] = 2; nextof[2] = 3; nextof[3] = 1;
	expect("loop of zero sized anodes", 0, DOSFALSE, 0, 0, 8);
	table(40);
	for (i = 1; i <= 40; i++) { csize[i] = 1; nextof[i] = i + 1; }
	nextof[40] = 20;				/* long tail, big loop (21 anodes) */
	expect("long tail into big loop", 100000, DOSFALSE, 0, 0, 200);

	/* a start anode whose number is not 1: mark must be the anode's own number */
	table(9);
	for (i = 1; i <= 9; i++) csize[i] = 1;
	nextof[7] = 8; nextof[8] = 7;
	o = 100; calls = 0; runaway = 0;
	GetAnode(&an, 7, &G);
	calls = 0;
	CHECK(CorrectAnode(&an, &o, &G) == DOSFALSE && !runaway && calls <= 8, "loop 7-8-7 started at 7: no termination (%lu reads)", (unsigned long)calls);

	/* a failing GetAnode ends the walk with DOSFALSE */
	table(4);
	for (i = 1; i <= 4; i++) { csize[i] = 2; nextof[i] = i + 1; }
	nextof[4] = 0;
	failnr = 2;
	expect("first step fails", 3, DOSFALSE, 0, 0, 1);
	failnr = 3;
	expect("second step fails", 5, DOSFALSE, 0, 0, 2);
	failnr = 3;
	expect("failing anode beyond the offset is never read", 3, DOSTRUE, 2, 1, 1);
	failnr = 0;
	expect("same chain without a failure", 5, DOSTRUE, 3, 1, 2);

	/* a valid chain is never rejected: every length 1..300 and one far beyond 16 bit counters */
	for (n = 1; n <= 300; n++)
	{
		table(n);
		for (i = 1; i <= n; i++) { csize[i] = 3; nextof[i] = i < n ? i + 1 : 0; }
		sprintf(name, "valid chain length %lu", (unsigned long)n);
		expect(name, 3 * n - 1, DOSTRUE, n, 2, n - 1);
		expect(name, 3 * n, DOSFALSE, 0, 0, n - 1);
	}
	table(70000);
	for (i = 1; i <= 70000; i++) { csize[i] = 1; nextof[i] = i < 70000 ? i + 1 : 0; }
	expect("valid chain of 70000 anodes", 69999, DOSTRUE, 70000, 0, 69999);
	expect("valid chain of 70000 anodes, past the end", 70000, DOSFALSE, 0, 0, 69999);

	if (fails) { printf("  %d check(s) failed\n", fails); return 1; }
	printf("  PASS: CorrectAnode ends on looping chains, valid chains are walked completely\n");
	return 0;
}
