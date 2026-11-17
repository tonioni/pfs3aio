/* LIVE @src pfsdoctor/standardscan.c @func ChainLoopEnd
 *
 * ChainLoopEnd finds where RepairDir cuts a directory anode chain that loops
 * back: the last anode before the repeat. The real function is lifted from
 * pfsdoctor/standardscan.c; GetAnode is a mock over a table that counts its reads.
 *  - a chain that ends: 0, also for 70000 anodes;
 *  - an anode linked to itself, a loop back to the first anode, a tail into
 *    a loop, a loop back to the second anode: the anode that links back;
 *  - a chain that cannot be read: 0;
 *  - the reads stay below six times the chain length.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

typedef uint32_t uint32;
typedef struct { uint32 clustersize, blocknr, next, nr; } canode_t;

#define NTAB 70010
static uint32 nextof[NTAB];
static bool used[NTAB];
static uint32 reads;

static bool GetAnode(canode_t *anode, uint32 anodenr, bool fix)
{
	(void)fix;
	reads++;
	if (anodenr >= NTAB || !used[anodenr])
		return false;
	anode->nr = anodenr;
	anode->clustersize = 1;
	anode->blocknr = 1000 + anodenr;
	anode->next = nextof[anodenr];
	return true;
}

#include "gen/ChainLoopEnd.c"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* anodes 1..n linked in order, n links to [last] */
static void chain(uint32 n, uint32 last)
{
	uint32 i;

	memset(nextof, 0, sizeof nextof);
	memset(used, 0, sizeof used);
	for (i = 1; i <= n; i++)
	{
		used[i] = true;
		nextof[i] = i < n ? i + 1 : last;
	}
	reads = 0;
}

static void expect(const char *name, uint32 n, uint32 last, uint32 want)
{
	uint32 got;

	chain(n, last);
	got = ChainLoopEnd(1);
	CHECK(got == want, "%s: %lu, want %lu", name, (unsigned long)got, (unsigned long)want);
	CHECK(reads <= 6 * n + 4, "%s: %lu reads for %lu anodes", name, (unsigned long)reads, (unsigned long)n);
}

int main(void)
{
	uint32 n;

	expect("one anode, ends", 1, 0, 0);
	expect("chain of 27, ends", 27, 0, 0);
	expect("chain of 70000, ends", 70000, 0, 0);
	expect("one anode linked to itself", 1, 1, 1);
	expect("last anode linked to itself", 27, 27, 27);
	expect("27 anodes, back to the first", 27, 1, 27);
	expect("27 anodes, back to the second", 27, 2, 27);
	expect("10 anodes, back to the fifth", 10, 5, 10);
	expect("70000 anodes, back to the first", 70000, 1, 70000);
	for (n = 1; n <= 64; n++)
	{
		uint32 to;
		char name[64];

		for (to = 1; to <= n; to++)
		{
			sprintf(name, "%lu anodes, back to %lu", (unsigned long)n, (unsigned long)to);
			expect(name, n, to, n);
		}
	}

	chain(5, 0);
	used[3] = false;
	CHECK(ChainLoopEnd(1) == 0, "unreadable anode in the chain: not 0");
	chain(5, 0);
	used[1] = false;
	CHECK(ChainLoopEnd(1) == 0, "unreadable first anode: not 0");

	if (fails)
	{
		printf("  %d check(s) failed\n", fails);
		return 1;
	}
	printf("  PASS: ChainLoopEnd finds the anode that links back; chains that end give 0\n");
	return 0;
}
