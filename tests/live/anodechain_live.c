/* LIVE @src anodes.c @func GetNextAnode @func FreeAnodeChain @func MakeAnodeChain
 *
 * A corrupt anode chain whose `next` links loop back (also onto itself) must
 * make MakeAnodeChain fail instead of allocating chain nodes until memory runs
 * out. A valid chain, however long, must still be built completely.
 * The real GetNextAnode, FreeAnodeChain and MakeAnodeChain are lifted from
 * anodes.c; GetAnode and the allocator are mocked.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define NULL_ANODE 0
#define ENTER(x)

struct canode { ULONG clustersize, blocknr, next, nr; };
struct anodechainnode { struct anodechainnode *next; struct canode an; };
struct anodechain
{
	struct anodechain *next, *prev;
	ULONG refcount;
	struct anodechainnode head;
};
struct anodewalk { ULONG mark, steps; };
struct volumedata { int anodechainlist; };

typedef struct globaldata
{
	void *(*allocmemp)(ULONG, struct globaldata *);
	void (*freememp)(void *, struct globaldata *);
	struct volumedata *currentvolume;
} globaldata;

#define AllocMemP(size,g) ((g->allocmemp)(size,g))
#define FreeMemP(mem,g) ((g->freememp)(mem,g))
#define MinAddHead(list, node) ((node)->next = (node))
#define MinRemove(node) ((void)0)

#define LIMIT 1000000UL		/* "memory" for chain nodes */
static ULONG allocs, live, exhausted;

static void *mock_alloc(ULONG size, globaldata *g)
{
	(void)g;
	if (allocs >= LIMIT)
	{
		exhausted = 1;
		return NULL;
	}
	allocs++;
	live++;
	return calloc(1, size);
}

static void mock_free(void *p, globaldata *g)
{
	(void)g;
	live--;
	free(p);
}

/* anode n links to nextof[n]; anodes beyond the table form a valid chain */
static ULONG nextof[16];
static ULONG validlen;		/* valid mode: anode n links to n+1 until validlen */

static BOOL GetAnode(struct canode *anode, ULONG anodenr, globaldata *g)
{
	(void)g;
	anode->nr = anodenr;
	anode->next = validlen ? (anodenr < validlen ? anodenr + 1 : 0) : nextof[anodenr];
	return TRUE;
}

#include "gen/FreeAnodeChain.c"            /* the REAL functions from ../../anodes.c */
#include "gen/GetNextAnode.c"
#include "gen/MakeAnodeChain.c"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static globaldata G = { mock_alloc, mock_free, NULL };

static void run(const char *name, int expect_ok, ULONG maxallocs)
{
	struct anodechain *ac;

	allocs = live = exhausted = 0;
	ac = MakeAnodeChain(1, &G);
	CHECK(!exhausted, "%s: ran out of memory building the chain (%lu allocations)", name, (unsigned long)allocs);
	CHECK((ac != NULL) == expect_ok, "%s: MakeAnodeChain %s", name, ac ? "succeeded" : "failed");
	CHECK(allocs <= maxallocs, "%s: %lu allocations, expected at most %lu", name, (unsigned long)allocs, (unsigned long)maxallocs);
	if (ac)
		FreeAnodeChain(ac, &G);
	CHECK(live == 0, "%s: %lu nodes leaked", name, (unsigned long)live);
}

int main(void)
{
	/* self-referencing anode */
	memset(nextof, 0, sizeof nextof);
	nextof[1] = 1;
	run("next == self", 0, 8);

	/* two anodes pointing at each other */
	memset(nextof, 0, sizeof nextof);
	nextof[1] = 2; nextof[2] = 1;
	run("loop 1-2-1", 0, 8);

	/* tail leading into a loop: 1 2 3 4 5 6 3 ... */
	memset(nextof, 0, sizeof nextof);
	nextof[1] = 2; nextof[2] = 3; nextof[3] = 4; nextof[4] = 5; nextof[5] = 6; nextof[6] = 3;
	run("tail into loop", 0, 64);

	/* valid chains must never be rejected, short or very long */
	memset(nextof, 0, sizeof nextof);
	nextof[1] = 2; nextof[2] = 3; nextof[3] = 4; nextof[4] = 0;
	run("valid short", 1, 4);
	validlen = 1;
	run("valid single", 1, 1);
	validlen = 70000;		/* longer than any 16 bit counter */
	run("valid long", 1, 70000);

	if (fails) { printf("  %d check(s) failed\n", fails); return 1; }
	printf("  PASS: looping anode chains are rejected, valid chains unaffected\n");
	return 0;
}
