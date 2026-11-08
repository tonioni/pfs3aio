/* LIVE @src fsresource.c @func AddToFSResource
 *
 * AddToFSResource registers the handler in FileSystem.resource so the
 * system can load it for other partitions of the same DosType. Without a
 * DosType or a SegList there is nothing useful to register: a zero DosType
 * entry would match every unset/garbage DosType and a NULL SegList entry
 * would hand out a handler that cannot be started. Lifts the real
 * AddToFSResource; exec (Forbid/Permit/OpenResource/AllocMem/AddHead/
 * AddTail) is a small mock working on real List/Node structures, so the
 * resource is inspected as the system would see it. Proven:
 *   - dostype == 0, seglist == 0, or both: return at once - no Forbid, no
 *     OpenResource, no allocation, nothing on the resource list (also when
 *     a resource with another entry already exists: it is left untouched);
 *   - a valid pair with no resource creates FileSystem.resource (named,
 *     NT_RESOURCE, on the ResourceList, empty list head) and one entry with
 *     DosType, version, patch flags 0x180, SegList, GlobalVec -1;
 *   - a second DosType is added at the head; the same DosType again (even
 *     with another SegList) creates nothing and keeps the old entry;
 *   - an existing empty resource gets the entry;
 *   - allocation failure leaves no half-initialised entry and Forbid/
 *     Permit stay balanced; every exec call happens inside Forbid.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint8_t UBYTE; typedef int8_t BYTE;
typedef intptr_t BPTR; typedef intptr_t BSTR; typedef void *CPTR; typedef void *APTR;
#define CONST const
#define MKBADDR(x) ((BPTR)(x))
#define NT_RESOURCE 8
#define MEMF_PUBLIC (1UL << 0)
#define MEMF_CLEAR (1UL << 16)
#define FSRNAME "FileSystem.resource"
#define VERNUM 20
#define REVNUM 0

struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type, l_pad; };
struct FileSysResource {
    struct Node fsr_Node; char *fsr_Creator; struct List fsr_FileSysEntries;
};
struct FileSysEntry {
    struct Node fse_Node; ULONG fse_DosType, fse_Version, fse_PatchFlags, fse_Type;
    CPTR fse_Task; BPTR fse_Lock; BSTR fse_Handler; ULONG fse_StackSize; LONG fse_Priority;
    BPTR fse_Startup, fse_SegList, fse_GlobalVec;
};
struct ExecBase { struct List ResourceList; };

/* as in fsresource.c */
#define NewList(list) \
({ struct List *_l = (list); _l->lh_TailPred = (struct Node *)_l; \
   _l->lh_Head = (struct Node *)&_l->lh_Tail; _l->lh_Tail = 0; })

CONST UBYTE shortname[] = "pfs3aio";

/* exec mock */
static struct ExecBase eb;
static int nest, forbids, permits, openres, outside, allocs, alloc_fail, heads, tails;
static ULONG alloc_size[8], alloc_flags[8];

static void Forbid(void) { nest++; forbids++; }
static void Permit(void) { nest--; permits++; }

static struct FileSysResource *OpenResource(const char *name)
{
    struct Node *n;

    openres++;
    if (!nest) outside++;
    if (strcmp(name, FSRNAME)) return NULL;
    for (n = eb.ResourceList.lh_Head; n->ln_Succ; n = n->ln_Succ)
        if (n->ln_Name && !strcmp(n->ln_Name, name))
            return (struct FileSysResource *)n;
    return NULL;
}

static void *AllocMem(ULONG size, ULONG flags)
{
    void *p;

    if (!nest) outside++;
    if (allocs < 8) { alloc_size[allocs] = size; alloc_flags[allocs] = flags; }
    allocs++;
    if (alloc_fail == allocs) return NULL;
    p = malloc(size);
    memset(p, (flags & MEMF_CLEAR) ? 0 : 0xA5, size);
    return p;
}

static void AddHead(struct List *l, struct Node *n)
{
    heads++;
    if (!nest) outside++;
    n->ln_Succ = l->lh_Head; n->ln_Pred = (struct Node *)&l->lh_Head;
    l->lh_Head->ln_Pred = n; l->lh_Head = n;
}
static void AddTail(struct List *l, struct Node *n)
{
    tails++;
    if (!nest) outside++;
    n->ln_Succ = (struct Node *)&l->lh_Tail; n->ln_Pred = l->lh_TailPred;
    l->lh_TailPred->ln_Succ = n; l->lh_TailPred = n;
}

#include "gen/AddToFSResource.c"             /* the REAL function from ../../fsresource.c */

static int fails;
static void check(int ok, const char *what)
{
    if (!ok) { printf("  BAD   %s\n", what); fails++; }
}

static void reset_calls(void)
{
    nest = forbids = permits = openres = outside = allocs = alloc_fail = heads = tails = 0;
}

static int count(struct List *l)
{
    struct Node *n; int c = 0;

    for (n = l->lh_Head; n->ln_Succ; n = n->ln_Succ) c++;
    return c;
}
static struct FileSysEntry *entry(struct List *l, int i)
{
    struct Node *n = l->lh_Head;

    while (i-- > 0 && n->ln_Succ) n = n->ln_Succ;
    return n->ln_Succ ? (struct FileSysEntry *)n : NULL;
}
static struct FileSysResource *resource(void)
{
    return eb.ResourceList.lh_Head->ln_Succ ? (struct FileSysResource *)eb.ResourceList.lh_Head : NULL;
}

static int untouched(void)
{
    return !forbids && !permits && !openres && !allocs && !heads && !tails && !outside;
}

#define DT_A 0x50465303u
#define DT_B 0x50465301u

static void rejected(const char *what, ULONG dt, BPTR seg)
{
    char m[120];
    struct FileSysResource *r;
    int n;

    r = resource(); n = r ? count(&r->fsr_FileSysEntries) : -1;
    reset_calls();
    AddToFSResource(dt, seg, &eb);
    sprintf(m, "%s: exec was called (forbid %d, open %d, alloc %d, add %d/%d)", what,
            forbids, openres, allocs, heads, tails);
    check(untouched(), m);
    sprintf(m, "%s: resource list changed", what);
    check((resource() ? count(&resource()->fsr_FileSysEntries) : -1) == n, m);
}

int main(void)
{
    struct FileSysResource *r;
    struct FileSysEntry *e;
    BPTR segA = MKBADDR(0x1000), segB = MKBADDR(0x2000), segC = MKBADDR(0x3000);

    setvbuf(stdout, NULL, _IONBF, 0);
    NewList(&eb.ResourceList);

    /* nothing to register: not even an empty resource appears */
    rejected("dostype 0", 0, segA);
    rejected("seglist NULL", DT_A, 0);
    rejected("both 0", 0, 0);
    check(resource() == NULL, "rejected call created the resource");

    /* valid pair: resource and entry created */
    reset_calls();
    AddToFSResource(DT_A, segA, &eb);
    r = resource();
    check(r != NULL, "valid pair: no resource");
    if (!r) goto done;
    check(count(&eb.ResourceList) == 1 && r->fsr_Node.ln_Type == NT_RESOURCE &&
          !strcmp(r->fsr_Node.ln_Name, FSRNAME), "resource not named / typed / listed");
    check(r->fsr_Creator == (char *)shortname, "resource creator");
    check(allocs == 2 && alloc_size[0] == sizeof(struct FileSysResource) &&
          alloc_size[1] == sizeof(struct FileSysEntry) &&
          (alloc_flags[0] & (MEMF_CLEAR | MEMF_PUBLIC)) == (MEMF_CLEAR | MEMF_PUBLIC) &&
          (alloc_flags[1] & (MEMF_CLEAR | MEMF_PUBLIC)) == (MEMF_CLEAR | MEMF_PUBLIC), "allocation size/flags");
    check(count(&r->fsr_FileSysEntries) == 1, "entry count after first registration");
    e = entry(&r->fsr_FileSysEntries, 0);
    check(e && e->fse_DosType == DT_A && e->fse_SegList == segA && e->fse_GlobalVec == -1 &&
          e->fse_PatchFlags == 0x180 && e->fse_Version == (((ULONG)VERNUM << 16) | REVNUM) &&
          e->fse_Node.ln_Name == (char *)shortname && !e->fse_Type && !e->fse_Task && !e->fse_Lock,
          "entry contents");
    check(forbids == 1 && permits == 1 && nest == 0 && !outside, "Forbid/Permit not balanced or exec used outside");

    /* a rejected call with a resource present leaves it alone */
    rejected("dostype 0, resource present", 0, segB);
    rejected("seglist NULL, resource present", DT_B, 0);
    check(count(&r->fsr_FileSysEntries) == 1, "rejected call added an entry");

    /* second DosType: at the head */
    reset_calls();
    AddToFSResource(DT_B, segB, &eb);
    check(count(&eb.ResourceList) == 1 && resource() == r, "second DosType: new resource created");
    check(allocs == 1 && alloc_size[0] == sizeof(struct FileSysEntry), "second DosType: allocation");
    check(count(&r->fsr_FileSysEntries) == 2, "second DosType: entry count");
    e = entry(&r->fsr_FileSysEntries, 0);
    check(e && e->fse_DosType == DT_B && e->fse_SegList == segB, "second DosType not at the head");
    e = entry(&r->fsr_FileSysEntries, 1);
    check(e && e->fse_DosType == DT_A && e->fse_SegList == segA, "first entry disturbed");
    check(forbids == 1 && permits == 1 && nest == 0 && !outside, "second DosType: Forbid/Permit");

    /* same DosType again: existing entry wins, nothing is created */
    reset_calls();
    AddToFSResource(DT_A, segC, &eb);
    check(allocs == 0 && count(&r->fsr_FileSysEntries) == 2, "duplicate DosType added an entry");
    e = entry(&r->fsr_FileSysEntries, 1);
    check(e && e->fse_DosType == DT_A && e->fse_SegList == segA, "duplicate DosType replaced the old SegList");
    reset_calls();
    AddToFSResource(DT_B, segC, &eb);
    check(allocs == 0 && count(&r->fsr_FileSysEntries) == 2 &&
          entry(&r->fsr_FileSysEntries, 0)->fse_SegList == segB, "duplicate (head entry) DosType changed the list");
    check(forbids == 1 && permits == 1 && nest == 0, "duplicate: Forbid/Permit");

    /* an existing but empty resource gets the entry */
    NewList(&r->fsr_FileSysEntries);
    reset_calls();
    AddToFSResource(DT_A, segC, &eb);
    check(allocs == 1 && count(&eb.ResourceList) == 1 && count(&r->fsr_FileSysEntries) == 1 &&
          entry(&r->fsr_FileSysEntries, 0)->fse_SegList == segC, "empty resource not filled");

    /* allocation failures: no half-built entry, Permit still done */
    NewList(&eb.ResourceList);
    reset_calls();
    alloc_fail = 1;                      /* the resource itself */
    AddToFSResource(DT_A, segA, &eb);
    check(resource() == NULL && heads == 0 && tails == 0, "failed resource allocation left something listed");
    check(forbids == 1 && permits == 1 && nest == 0, "resource alloc failure: Forbid/Permit");

    reset_calls();
    alloc_fail = 2;                      /* the entry */
    AddToFSResource(DT_A, segA, &eb);
    r = resource();
    check(r && count(&r->fsr_FileSysEntries) == 0 && heads == 0, "failed entry allocation added an entry");
    check(forbids == 1 && permits == 1 && nest == 0, "entry alloc failure: Forbid/Permit");

    reset_calls();
    alloc_fail = 1;                      /* resource exists: the entry fails */
    AddToFSResource(DT_B, segB, &eb);
    check(count(&r->fsr_FileSysEntries) == 0 && nest == 0 && heads == 0, "failed entry allocation (resource present)");

    reset_calls();
    AddToFSResource(DT_B, segB, &eb);
    check(count(&r->fsr_FileSysEntries) == 1 && entry(&r->fsr_FileSysEntries, 0)->fse_DosType == DT_B,
          "retry after failure did not register");
done:
    printf("== fsresource: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
