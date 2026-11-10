/* LIVE @src dd_funcs.c @func dd_CheckCustomPacket @func dd_SignalIdle
 *
 * Lifts the checkout's real dd_SignalIdle (ACTION_ADD_IDLE_SIGNAL) over a real
 * exec-style list in g->idlelist and counting AllocMemP/FreeMemP/Remove mocks:
 *
 *  - add: an allocation failure reports ERROR_NO_FREE_STORE and queues nothing;
 *    a valid add fills the handle (task, clean/dirty signal) and queues it at
 *    the head;
 *  - remove: the handle is unlinked and freed from the head, the middle and the
 *    tail of a list of three, leaving the other two linked in order;
 *  - a packet without the PFS2 id does nothing.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef intptr_t SIPTR; typedef int32_t LONG; typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef uint8_t UBYTE; typedef int BOOL;
#define DOSTRUE (-1)
#define DOSFALSE 0
#define ID_AFS_DISK (0x41465301L)
#define ID_PFS2_DISK (0x50465302L)
#define ERROR_NO_FREE_STORE 103

struct DosPacket { SIPTR dp_Arg1, dp_Arg2, dp_Arg3, dp_Arg4, dp_Res2; };
struct Task { int dummy; };

/* exec lists: an empty list is head=&tail, tail=NULL, tailpred=&head */
struct Node { struct Node *ln_Succ, *ln_Pred; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct MinList { void *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct idlehandle
{
	struct idlehandle *next;
	struct idlehandle *previous;
	struct Task *task;
	UWORD cleansignal;
	UWORD dirtysignal;
};
#define MinAddHead(list, node) AddHead((struct List *)(list), (struct Node *)(node))
#define MinRemove(node) Remove((struct Node *)node)
#define HeadOf(list) ((void *)((list)->mlh_Head))

typedef struct globaldata
{
	struct MinList idlelist;
	void *(*allocmemp)(ULONG, struct globaldata *);
	void (*freememp)(void *, struct globaldata *);
} globaldata;
#define AllocMemP(size,g) ((g->allocmemp)(size,g))
#define FreeMemP(mem,g) ((g->freememp)(mem,g))

static int alloc_calls, alloc_fails, nfreed, unlinked;
static ULONG alloc_size;
static void *freed[8];

static void AddHead(struct List *l, struct Node *n)
{
	n->ln_Succ = l->lh_Head; n->ln_Pred = (struct Node *)&l->lh_Head;
	l->lh_Head->ln_Pred = n; l->lh_Head = n;
}
static void Remove(struct Node *n)
{
	unlinked++;
	n->ln_Pred->ln_Succ = n->ln_Succ; n->ln_Succ->ln_Pred = n->ln_Pred;
}
static void *allocmemp(ULONG size, globaldata *g)
{
	(void)g;
	alloc_calls++; alloc_size = size;
	return alloc_fails ? NULL : calloc(1, size);
}
static void freememp(void *m, globaldata *g) { (void)g; if (nfreed < 8) freed[nfreed] = m; nfreed++; }

#include "gen/dd_CheckCustomPacket.c"        /* the REAL functions from ../../dd_funcs.c */
#include "gen/dd_SignalIdle.c"

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static globaldata gd;
static struct DosPacket pkt;
static struct Task task;

static void reset(void)
{
	alloc_calls = alloc_fails = nfreed = unlinked = 0;
	alloc_size = 0;
	memset(&pkt, 0, sizeof(pkt));
	gd.allocmemp = allocmemp; gd.freememp = freememp;
	gd.idlelist.mlh_Head = &gd.idlelist.mlh_Tail; gd.idlelist.mlh_Tail = NULL;
	gd.idlelist.mlh_TailPred = &gd.idlelist.mlh_Head;
}

static struct idlehandle *add_idle(SIPTR signals, SIPTR task_arg)
{
	pkt.dp_Arg1 = ID_PFS2_DISK; pkt.dp_Arg2 = DOSTRUE; pkt.dp_Arg3 = signals; pkt.dp_Arg4 = task_arg;
	pkt.dp_Res2 = 0;
	return (struct idlehandle *)dd_SignalIdle(&pkt, &gd);
}
static SIPTR remove_idle(SIPTR handle)
{
	pkt.dp_Arg1 = ID_PFS2_DISK; pkt.dp_Arg2 = DOSFALSE; pkt.dp_Arg3 = handle; pkt.dp_Arg4 = 0;
	pkt.dp_Res2 = 0;
	return dd_SignalIdle(&pkt, &gd);
}

/* the list holds exactly want[0..n-1], head to tail, with consistent back links */
static int idle_is(struct idlehandle **want, int n)
{
	struct idlehandle *h = HeadOf(&gd.idlelist), *prev = (struct idlehandle *)&gd.idlelist.mlh_Head;
	int i;
	for (i = 0; i < n; i++)
	{
		if (h != want[i] || h->previous != prev) return 0;
		prev = h; h = h->next;
	}
	return h->next == NULL && h->previous == prev && gd.idlelist.mlh_TailPred == (void *)prev;
}

/* a fresh list of three handles, queued head to tail as list3[0..2] */
static void three(struct idlehandle **list3)
{
	struct idlehandle *a, *b, *c;
	reset();
	a = add_idle(0x00010002, (SIPTR)&task); b = add_idle(0x00030004, (SIPTR)&task); c = add_idle(0x00050006, (SIPTR)&task);
	list3[0] = c; list3[1] = b; list3[2] = a;       /* new handles go to the head */
}

int main(void)
{
	struct idlehandle *list3[3], *two[2], *h;
	static const char *const where[] = { "head", "middle", "tail" };
	SIPTR res;
	int which, k, j;

	setvbuf(stdout, NULL, _IONBF, 0);

	reset(); alloc_fails = 1;
	res = (SIPTR)add_idle(0x00030005, (SIPTR)&task);
	printf("  add, no memory:          res=%d err=%d allocs=%d\n", (int)res, (int)pkt.dp_Res2, alloc_calls);
	check(res == DOSFALSE && pkt.dp_Res2 == ERROR_NO_FREE_STORE, "an allocation failure -> ERROR_NO_FREE_STORE");
	check(alloc_calls == 1 && idle_is(NULL, 0), "nothing is queued when the allocation fails");

	reset();
	h = add_idle(0x00030005, (SIPTR)&task);
	printf("  add, valid:              handle=%s allocs=%d size=%u\n", h ? "yes" : "NULL", alloc_calls, (unsigned)alloc_size);
	check(h && h->task == &task && h->cleansignal == 3 && h->dirtysignal == 5, "the handle holds the task and both signals");
	check(alloc_calls == 1 && alloc_size == sizeof(struct idlehandle), "one handle-sized block is allocated");
	list3[0] = h;
	check(idle_is(list3, 1), "the handle is queued");

	/* a packet that is not a PFS2 packet is ignored */
	reset();
	pkt.dp_Arg1 = 0x1234; pkt.dp_Arg2 = DOSTRUE; pkt.dp_Arg3 = 0x00010002; pkt.dp_Arg4 = (SIPTR)&task;
	res = dd_SignalIdle(&pkt, &gd);
	check(res == DOSFALSE && alloc_calls == 0 && idle_is(NULL, 0), "a packet without the PFS2 id must do nothing");

	for (which = 0; which < 3; which++)
	{
		three(list3);
		for (k = j = 0; k < 3; k++) if (k != which) two[j++] = list3[k];
		res = remove_idle((SIPTR)list3[which]);
		printf("  remove the %-6s handle: res=%d freed=%d unlinked=%d\n", where[which], (int)res, nfreed, unlinked);
		check(res == DOSTRUE, "removing a queued handle must succeed");
		check(nfreed == 1 && freed[0] == list3[which] && unlinked == 1, "exactly that handle is unlinked and freed");
		check(idle_is(two, 2), "the other two handles stay queued, in order");
	}

	if (failures == 0) { printf("  PASS: SignalIdle reports a failed allocation and queues and unlinks its handles\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
