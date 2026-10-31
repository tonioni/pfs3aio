/* Benchmark of operations a filesystem inner loop is made of, so the compilers
 * and the two tiers can be compared on the same work across emulated CPUs.
 *
 * This measures compiler output on representative operations. It is not the
 * driver, and it says nothing about real hardware timing: the emulator has no
 * memory wait states, no caches and no chip-RAM contention.
 *
 *   sh tests/m68k/bench.sh
 */

#include <exec/types.h>

/* The compilers pass arguments in registers here (-mregparm=3), but the C
 * startup calls main on the stack. */
#define BENCH_MAIN __attribute__((stkparm))

#define BLK 2048
#define NAMES 64

static UBYTE blocka[BLK];
static UBYTE names[NAMES][32];
static ULONG bitmap[BLK / 32];

struct node {
	struct node *next;
	ULONG blocknr;
	UWORD used;
};
static struct node pool[256];

/* block shifting, as directory entry insert/remove does */
static ULONG work_move(void)
{
	ULONG i, sum = 0;
	int k;

	for (k = 0; k < 40; k++) {
		for (i = 0; i < BLK; i++)
			blocka[i] = (UBYTE)(i + k);
		/* shift down by 6, the direntry shape: overlapping, word aligned */
		{
			UBYTE *d = blocka;
			const UBYTE *s = blocka + 6;
			for (i = 0; i < BLK - 6; i++)
				d[i] = s[i];
		}
		sum += blocka[0] + blocka[BLK - 7];
	}
	return sum;
}

/* block number arithmetic: 32-bit divide and modulo */
static ULONG work_div(void)
{
	ULONG i, sum = 0;

	for (i = 1; i < 20000; i++) {
		sum += i / 512;
		sum += i % 512;
		sum += (i * 7) / 31;
	}
	return sum;
}

/* name matching, as directory lookup does */
static ULONG work_names(void)
{
	ULONG i, j, sum = 0;
	int k;

	for (i = 0; i < NAMES; i++) {
		for (j = 0; j < 31; j++)
			names[i][j] = (UBYTE)('a' + ((i + j) % 26));
		names[i][31] = 0;
	}
	for (k = 0; k < 60; k++) {
		for (i = 0; i < NAMES; i++) {
			for (j = 0; j < NAMES; j++) {
				const UBYTE *p = names[i], *q = names[j];
				while (*p && *p == *q) { p++; q++; }
				sum += (ULONG)(*p - *q) & 1;
			}
		}
	}
	return sum;
}

/* LRU chain walking */
static ULONG work_list(void)
{
	ULONG sum = 0;
	int k, i;

	for (i = 0; i < 255; i++) {
		pool[i].next = &pool[i + 1];
		pool[i].blocknr = (ULONG)i * 3;
		pool[i].used = (UWORD)i;
	}
	pool[255].next = 0;
	for (k = 0; k < 400; k++) {
		struct node *n = pool;
		while (n) {
			if (n->used & 1)
				sum += n->blocknr;
			n = n->next;
		}
	}
	return sum;
}

/* free-block bitmap scanning */
static ULONG work_bitmap(void)
{
	ULONG i, sum = 0;
	int k;

	for (i = 0; i < BLK / 32; i++)
		bitmap[i] = 0xa5a5a5a5UL ^ i;
	for (k = 0; k < 900; k++) {
		for (i = 0; i < BLK / 32; i++) {
			ULONG w = bitmap[i];
			int b;
			for (b = 0; b < 32; b++) {
				if (w & (1UL << b))
					sum++;
			}
		}
	}
	return sum;
}

/* volatile, so no compiler can decide the whole benchmark is dead code */
volatile ULONG bench_result;

/* With no argument every workload runs. With one, only that workload does, so
 * bench.sh can attribute a difference to a kind of work rather than to the
 * benchmark as a whole. */
int BENCH_MAIN main(int argc, char **argv)
{
	ULONG s = 0;
	int only = 0;

	if (argc > 1 && argv[1][0] >= '1' && argv[1][0] <= '5')
		only = argv[1][0] - '0';

	if (only == 0 || only == 1) s += work_move();
	if (only == 0 || only == 2) s += work_div();
	if (only == 0 || only == 3) s += work_names();
	if (only == 0 || only == 4) s += work_list();
	if (only == 0 || only == 5) s += work_bitmap();
	bench_result = s;
	/* returned as the exit code so every build can be checked to have
	 * computed the same thing */
	return (int)(s % 100);
}
