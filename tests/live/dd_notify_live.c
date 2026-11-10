/* LIVE @src dd_support.c @func RemoveNotify @func FreeNotifyObject
 *
 * Lifts the checkout's real RemoveNotify (what ACTION_REMOVE_NOTIFY and Quit
 * use) and asserts that a notify request is unlinked and freed wherever it is
 * queued. The caller (dos.library) frees the NotifyRequest right after the
 * packet returns, so a request left on any volume's list is a dangling
 * pointer for the next notification.
 *
 *  - on the current volume;
 *  - on an older volume that is no longer current (kept alive by locks);
 *  - with no current volume at all (the disk was removed);
 *  - on Quit's own list of notify objects taken over from the volumes;
 *  - a request that was never queued, or NULL: nothing is touched;
 *  - several requests on one volume: only the matching one goes;
 *  - our messages still queued at the client's port are taken back and
 *    freed, and the counters follow; other messages stay.
 *
 * A tree that only searches the current volume (and refuses the packet when
 * there is none) leaves the request queued in the second and third case.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef intptr_t SIPTR; typedef uint32_t ULONG; typedef uint8_t UBYTE; typedef int BOOL;

#define NRF_SEND_MESSAGE 1
#define NRF_MAGIC        (1UL << 31)

struct Node { struct Node *ln_Succ, *ln_Pred; };
struct List { struct Node *lh_Head; struct Node *lh_Tail; struct Node *lh_TailPred; };
struct MinList { void *mlh_Head; void *mlh_Tail; void *mlh_TailPred; };
struct MsgPort { struct List mp_MsgList; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; };
struct NotifyRequest {
	ULONG nr_Flags;
	ULONG nr_MsgCount;
	union { struct { struct MsgPort *nr_Port; } nr_Msg; } nr_stuff;
};
struct NotifyMessage { struct Message nm_ExecMessage; struct NotifyRequest *nm_NReq; };
struct notifyobject {
	struct notifyobject *next, *prev;
	struct NotifyRequest *req;
	UBYTE *namemem, *objectname;
};
struct volumedata { struct volumedata *next, *prev; struct MinList notifylist; };
typedef struct globaldata {
	struct MinList volumes;
	struct volumedata *currentvolume;
	struct MsgPort *notifyport;
	ULONG notifymsgs;
	void (*freememp)(void *, struct globaldata *);
} globaldata;

#define HeadOf(list) ((void *)((list)->mlh_Head))
#define MinRemove(node) Remove((struct Node *)node)
#define FreeMemP(mem,g) ((g->freememp)(mem,g))
#define Forbid() ((void)0)
#define Permit() ((void)0)

/* the exec list layout: an empty list is head=&tail, tail=NULL, tailpred=&head */
static void list_init(struct MinList *l)
{
	l->mlh_Head = &l->mlh_Tail; l->mlh_Tail = NULL; l->mlh_TailPred = &l->mlh_Head;
}
static void list_addtail(struct MinList *l, void *n_)
{
	struct Node *n = n_, *tp = l->mlh_TailPred;
	n->ln_Succ = (struct Node *)&l->mlh_Tail; n->ln_Pred = tp;
	tp->ln_Succ = n; l->mlh_TailPred = n;
}
static void Remove(struct Node *n)
{
	n->ln_Pred->ln_Succ = n->ln_Succ; n->ln_Succ->ln_Pred = n->ln_Pred;
}

static void *freed[64];
static int nfreed;
static void freememp(void *m, globaldata *g) { (void)g; freed[nfreed++] = m; }

#include "gen/FreeNotifyObject.c"            /* the REAL functions from ../../dd_support.c */
#include "gen/RemoveNotify.c"

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct notifyobject *queue2(struct MinList *l, struct NotifyRequest *r)
{
	struct notifyobject *no = calloc(1, sizeof(*no));
	no->req = r; no->namemem = malloc(1); no->objectname = malloc(1);
	list_addtail(l, no);
	return no;
}
static struct notifyobject *queue(struct volumedata *v, struct NotifyRequest *r)
{
	return queue2(&v->notifylist, r);
}
static int count2(struct MinList *l)
{
	struct notifyobject *no; int n = 0;
	for (no = HeadOf(l); no->next; no = no->next) n++;
	return n;
}
static int count(struct volumedata *v)
{
	return count2(&v->notifylist);
}
static int queued(struct volumedata *v, struct NotifyRequest *r)
{
	struct notifyobject *no;
	for (no = HeadOf(&v->notifylist); no->next; no = no->next) if (no->req == r) return 1;
	return 0;
}
static int was_freed(void *p)
{
	int i;
	for (i = 0; i < nfreed; i++) if (freed[i] == p) return 1;
	return 0;
}

/* two volumes on the volume list: A (older) and B; cur selects the current one */
static struct MsgPort notifyport;
static void setup(globaldata *g, struct volumedata *a, struct volumedata *b, struct volumedata *cur)
{
	list_init(&g->volumes); list_init(&a->notifylist); list_init(&b->notifylist);
	list_addtail(&g->volumes, a); list_addtail(&g->volumes, b);
	g->currentvolume = cur; g->freememp = freememp;
	g->notifyport = &notifyport; g->notifymsgs = 0;
	nfreed = 0;
}

static void remove_req(globaldata *g, struct NotifyRequest *r, struct MinList *extra)
{
	RemoveNotify(r, extra, g);
}

/* a notification message for r sits in port's queue, replies go to g->notifyport */
static struct NotifyMessage *post(struct MsgPort *port, struct NotifyRequest *r, struct MsgPort *reply, globaldata *g)
{
	struct NotifyMessage *m = calloc(1, sizeof(*m));
	m->nm_NReq = r; m->nm_ExecMessage.mn_ReplyPort = reply;
	list_addtail((struct MinList *)&port->mp_MsgList, m);
	if (reply == g->notifyport) { r->nr_MsgCount++; g->notifymsgs++; }
	return m;
}
static int inport(struct MsgPort *port, struct NotifyMessage *m)
{
	struct Node *n;
	for (n = port->mp_MsgList.lh_Head; n->ln_Succ; n = n->ln_Succ) if (n == (struct Node *)m) return 1;
	return 0;
}

int main(void)
{
	struct volumedata A, B;
	globaldata g;
	struct NotifyRequest r1 = {0}, r2 = {0}, r3 = {0}, r4 = {0}, r5 = {0}, r6 = {0};
	struct notifyobject *no;
	struct MinList extra;

	setup(&g, &A, &B, &B);
	no = queue(&B, &r1);
	remove_req(&g, &r1, NULL);
	printf("  current volume: queued=%d freed=%d\n", queued(&B, &r1), nfreed);
	check(!queued(&B, &r1), "current volume: request still queued");
	check(nfreed == 3 && was_freed(no) && was_freed(no->namemem) && was_freed(no->objectname),
		"current volume: notifyobject and both name buffers must be freed");

	setup(&g, &A, &B, &B);
	no = queue(&A, &r2);
	remove_req(&g, &r2, NULL);
	printf("  older volume:   queued=%d freed=%d\n", queued(&A, &r2), nfreed);
	check(!queued(&A, &r2), "older volume: request left queued (dangling after the caller frees it)");
	check(nfreed == 3 && was_freed(no), "older volume: notifyobject must be freed");

	setup(&g, &A, &B, NULL);
	no = queue(&A, &r3);
	remove_req(&g, &r3, NULL);
	printf("  no current vol: queued=%d freed=%d\n", queued(&A, &r3), nfreed);
	check(!queued(&A, &r3), "no current volume: request left queued (dangling after the caller frees it)");
	check(nfreed == 3 && was_freed(no), "no current volume: notifyobject must be freed");

	setup(&g, &A, &B, NULL);
	list_init(&extra);
	no = queue2(&extra, &r1);
	queue(&A, &r2);
	remove_req(&g, &r1, &extra);
	printf("  extra list:     extra=%d A=%d freed=%d\n", count2(&extra), count(&A), nfreed);
	check(count2(&extra) == 0 && nfreed == 3 && was_freed(no),
		"extra list (Quit's takeover): the request must be freed from it");
	check(count(&A) == 1, "extra list: the volumes' requests must be left alone");

	setup(&g, &A, &B, &B);
	queue(&A, &r1); queue(&B, &r2);
	remove_req(&g, &r3, NULL);
	remove_req(&g, NULL, NULL);
	printf("  not queued:     A=%d B=%d freed=%d\n", count(&A), count(&B), nfreed);
	check(count(&A) == 1 && count(&B) == 1 && nfreed == 0, "unknown request or NULL: nothing may be touched");

	setup(&g, &A, &B, &B);
	queue(&B, &r4); queue(&B, &r5); queue(&B, &r6);
	remove_req(&g, &r5, NULL);
	printf("  middle of 3:    left=%d freed=%d\n", count(&B), nfreed);
	check(count(&B) == 2, "several requests: exactly one is removed");
	check(queued(&B, &r4) && !queued(&B, &r5) && queued(&B, &r6),
		"several requests: only the matching one may go, the others stay in order");

	{
		struct MsgPort client, other;
		struct NotifyRequest q = {0}, s = {0};
		struct NotifyMessage *mine1, *mine2, *theirs, *foreign;

		setup(&g, &A, &B, &B);
		list_init((struct MinList *)&client.mp_MsgList); list_init((struct MinList *)&other.mp_MsgList);
		q.nr_Flags = NRF_SEND_MESSAGE | NRF_MAGIC; q.nr_stuff.nr_Msg.nr_Port = &client;
		s.nr_Flags = NRF_SEND_MESSAGE;             s.nr_stuff.nr_Msg.nr_Port = &client;
		queue(&B, &q);
		mine1 = post(&client, &q, g.notifyport, &g);
		theirs = post(&client, &s, g.notifyport, &g);       /* another request, same port */
		foreign = post(&client, &q, &other, &g);            /* not replied to our port */
		mine2 = post(&client, &q, g.notifyport, &g);
		nfreed = 0;
		remove_req(&g, &q, NULL);
		printf("  queued msgs:    q.count=%u s.count=%u notifymsgs=%u freed=%d\n",
			(unsigned)q.nr_MsgCount, (unsigned)s.nr_MsgCount, (unsigned)g.notifymsgs, nfreed);
		check(!inport(&client, mine1) && !inport(&client, mine2),
			"queued messages: ours for the removed request must be taken back");
		check(inport(&client, theirs) && inport(&client, foreign),
			"queued messages: another request's, and one not replied to us, must stay");
		check(q.nr_MsgCount == 0 && s.nr_MsgCount == 1 && g.notifymsgs == 1,
			"queued messages: nr_MsgCount and notifymsgs must follow");
		check(!(q.nr_Flags & NRF_MAGIC), "queued messages: NRF_MAGIC must be cleared");
		check(nfreed == 5 && was_freed(mine1) && was_freed(mine2),
			"queued messages: both messages and the notifyobject must be freed");
		check(!queued(&B, &q), "queued messages: the request itself must be unlinked");
	}

	if (failures == 0) { printf("  PASS: a notify request is unlinked wherever it is queued\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
