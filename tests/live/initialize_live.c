/* LIVE @src init.c @func PadDosEnvec @func Initialize
 *
 * The early checks of Initialize, proven on the driver's real PadDosEnvec and
 * Initialize. Every callee of Initialize (exec, memory pools, CalculateBlockSize,
 * the device opening helpers, InitDataCache, AddToFSResource, ...) is a mock that
 * records its calls; with all mocks succeeding Initialize returns TRUE, so what
 * it does with the DosEnvec table can be observed on the success path too.
 * DOS tables are handed in through fssm_Environ; short ones sit flush against a
 * PROT_NONE page (a stray read past the table crashes, which is reported as a
 * failure), and a snapshot shows the DOS-owned table is not written.
 *
 *  (a) a table too short to locate the partition (de_TableSize < 10) is
 *      refused: FALSE, nothing allocated or opened;
 *  (b) de_SizeBlock must be 1..1024 longs: 0, 1025, 4096, 0x80000001 and
 *      0xFFFFFFFF are refused (nothing allocated); 1, 2, 1023 and 1024 pass,
 *      both in a full and in a padded short table;
 *  (c) de_MaxTransfer == 0 becomes 0x20000 (before CalculateBlockSize sees it);
 *      non-zero values are left alone;
 *  (d) g->allocmemp/freememp/allocbufmem/freebufmem are set before EVERY
 *      failure return (Quit() uses them after a failed init): NULL/empty
 *      mountname, NULL fssm, device, environ or devnode, a refused table,
 *      a later failure;
 *  (e) a table without a DosType (de_TableSize < DE_DOSTYPE) is replaced by
 *      g->envcopy (padded to at least DE_MASK, defaults filled in, DosType
 *      zero) and g->dosenvec points at it - the DOS-owned table is never
 *      written; a table that holds the DosType is used in place;
 *  (f) AddToFSResource is called only if de_TableSize >= DE_DOSTYPE, with that
 *      DosType and the SegList of the device node; never for a shorter table.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef uint8_t UBYTE;
typedef int8_t BYTE; typedef int BOOL; typedef intptr_t BPTR; typedef intptr_t BSTR;
typedef void *APTR; typedef const char *CONST_STRPTR; typedef UBYTE *DSTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC (1UL << 0)
#define MEMF_CLEAR (1UL << 16)
#define BADDR(x) ((void *)(x))
#define MKBADDR(x) ((BPTR)(x))
#define TIMERNAME "timer.device"
#define UNIT_VBLANK 1
#define EXTRAPACKETS 1
#define ENTER(x)
#define EXIT(x)
#define DB(x)

struct DosEnvec {
    ULONG de_TableSize, de_SizeBlock, de_SecOrg, de_Surfaces, de_SectorPerBlock,
          de_BlocksPerTrack, de_Reserved, de_PreAlloc, de_Interleave, de_LowCyl,
          de_HighCyl, de_NumBuffers, de_BufMemType, de_MaxTransfer, de_Mask,
          de_BootPri, de_DosType, de_Baud, de_Control, de_BootBlocks;
};
#define DE_UPPERCYL    10
#define DE_NUMBUFFERS  11
#define DE_MEMBUFTYPE  12
#define DE_MAXTRANSFER 13
#define DE_MASK        14
#define DE_DOSTYPE     16

struct MsgPort { int x; };
struct Process { int x; };
struct ExecBase { int x; };
struct IORequest { int x; };
struct IOExtTD { struct IORequest iotd_Req; };
struct timerequest { struct IORequest tr_node; };
struct DriveGeometry { ULONG dg_Fields[9]; };
struct volumedata { int x; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type, l_pad; };
struct MinList { struct Node *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct FileSysStartupMsg { ULONG fssm_Unit; BSTR fssm_Device; BPTR fssm_Environ; ULONG fssm_Flags; };
/* DeviceNode and DosList overlay each other; dn_Startup/dol_Startup are BPTR so
 * the overlay also lines up with 8 byte pointers on this host */
struct DeviceNode {
    BPTR dn_Next; ULONG dn_Type; struct MsgPort *dn_Task; BPTR dn_Lock; BSTR dn_Handler;
    ULONG dn_StackSize; LONG dn_Priority; BPTR dn_Startup; BPTR dn_SegList; BPTR dn_GlobalVec;
    BSTR dn_Name;
};
struct DosList {
    BPTR dol_Next; LONG dol_Type; struct MsgPort *dol_Task; BPTR dol_Lock;
    union { struct { BSTR dol_Handler; LONG dol_StackSize; LONG dol_Priority; BPTR dol_Startup;
                     BPTR dol_SegList; BPTR dol_GlobVec; } dol_handler; } dol_misc;
    BSTR dol_Name;
};

typedef struct globaldata {
    LONG (*ErrorMsg)(CONST_STRPTR, APTR, ULONG, struct globaldata *);
    void *(*allocmemp)(ULONG, struct globaldata *);
    void (*freememp)(void *, struct globaldata *);
    void *(*allocbufmem)(ULONG, struct globaldata *);
    void (*freebufmem)(void *, struct globaldata *);
    struct DosEnvec *dosenvec;
    struct DosEnvec envcopy;
    DSTR mountname;
    struct MsgPort *notifyport, *msgport, *port, *timeport, *sleepport;
    struct Process *myproc;
    struct DeviceNode *devnode;
    struct FileSysStartupMsg *startup;
    struct volumedata *currentvolume;
    void *mainPool, *bufferPool;
    ULONG blocksize_phys, blocksize;
    struct DriveGeometry *geom;
    struct MinList volumes, idlelist;
    struct IOExtTD *request;
    BOOL trackdisk, scsidevice, harddiskmode, removable;
    struct timerequest *trequest, *flushrequest;
} globaldata;
#define AllocMemP(size, g) ((g->allocmemp)(size, g))

static const UBYTE AFS_ERROR_MEMORY_POOL[] = "pool";
static struct ExecBase execbase_obj;
static struct ExecBase *SysBase = &execbase_obj;

/* ---- mocks ---------------------------------------------------------- */
enum { C_ALLOCR, C_PORT, C_FINDTASK, C_POOL, C_CBS, C_POOLVEC, C_OPENDISK, C_SEMA, C_DCACHE,
       C_TIMER, C_CIO, C_OPENDEV, C_DELIO, C_REMOVABLE, C_DCINST, C_RESET, C_AFSR, C_ERRMSG,
       C_COUNT };
static int n[C_COUNT];
static int fail_port;                    /* bit k: the k-th CreateMsgPort fails */
static ULONG cb_phys, cb_bs;             /* what CalculateBlockSize produces */
static ULONG cb_sizeblock, cb_maxtransfer;
static struct DosEnvec *cb_env;
static ULONG pool_flags[4];
static ULONG afsr_dostype; static BPTR afsr_seg; static struct ExecBase *afsr_base;
static struct MsgPort port_obj[2], task_port, timer_port;
static struct Process task_obj;
static struct IOExtTD request_obj;
static struct timerequest trequest_obj, flush_obj;
static char pool_obj[2];
static UBYTE geom_obj[sizeof(struct DriveGeometry)];

static LONG _NormalErrorMsg(CONST_STRPTR m, APTR a, ULONG f, globaldata *g) { return 0; }
static void *AllocPooledVec(ULONG size, globaldata *g) { n[C_POOLVEC]++; return geom_obj; }
static void FreePooledVec(void *m, globaldata *g) { }
static void *AllocPooledBuf(ULONG size, globaldata *g) { return NULL; }
static void FreePooledBuf(void *m, globaldata *g) { }
static void *AllocMemR(ULONG size, ULONG flags, globaldata *g) { n[C_ALLOCR]++; return calloc(1, size + 8); }
static struct MsgPort *CreateMsgPort(void)
{
    int k = n[C_PORT]++;

    if (fail_port & (1 << k)) return NULL;
    return &port_obj[k & 1];
}
static struct Task *FindTask(const char *name) { n[C_FINDTASK]++; return (struct Task *)&task_obj; }
static void *LibCreatePool(ULONG flags, ULONG puddle, ULONG thresh)
{
    if (n[C_POOL] < 4) pool_flags[n[C_POOL]] = flags;
    n[C_POOL]++;
    return &pool_obj[n[C_POOL] & 1];
}
static LONG ErrorMsg(CONST_STRPTR m, APTR a, globaldata *g) { n[C_ERRMSG]++; return 0; }
static void CalculateBlockSize(globaldata *g, ULONG spb, ULONG bs)
{
    n[C_CBS]++;
    cb_env = g->dosenvec;
    cb_sizeblock = g->dosenvec->de_SizeBlock;
    cb_maxtransfer = g->dosenvec->de_MaxTransfer;
    g->blocksize_phys = cb_phys;
    g->blocksize = cb_bs;
}
static void NewList(struct List *l) { l->lh_TailPred = (struct Node *)l; l->lh_Head = (struct Node *)&l->lh_Tail; l->lh_Tail = 0; }
static void CopyMem(const void *s, void *d, ULONG size) { memcpy(d, s, size); }
static BOOL OpenDiskDevice(struct FileSysStartupMsg *f, struct MsgPort **p, struct IOExtTD **r, BOOL *td, BOOL *sc, globaldata *g)
{
    n[C_OPENDISK]++;
    *p = &port_obj[0]; *r = &request_obj; *td = TRUE; *sc = FALSE;
    return TRUE;
}
static BOOL init_device_unit_sema(struct FileSysStartupMsg *f, globaldata *g) { n[C_SEMA]++; return TRUE; }
static BOOL InitDataCache(globaldata *g) { n[C_DCACHE]++; return TRUE; }
static BOOL OpenTimerDevice(struct MsgPort **p, struct timerequest **r, ULONG unit, globaldata *g)
{
    n[C_TIMER]++;
    *p = &timer_port; *r = &trequest_obj;
    return TRUE;
}
static struct IORequest *CreateIORequest(struct MsgPort *p, ULONG size) { n[C_CIO]++; return (struct IORequest *)&flush_obj; }
static BYTE OpenDevice(const char *name, ULONG unit, struct IORequest *io, ULONG flags) { n[C_OPENDEV]++; return 0; }
static void DeleteIORequest(struct IORequest *io) { n[C_DELIO]++; }
static BOOL TestRemovability(globaldata *g) { n[C_REMOVABLE]++; return FALSE; }
static BOOL InstallDiskChangeHandler(globaldata *g) { n[C_DCINST]++; return TRUE; }
static void InstallResetHandler(globaldata *g) { n[C_RESET]++; }
static void AddToFSResource(ULONG dostype, BPTR seglist, struct ExecBase *base)
{
    n[C_AFSR]++; afsr_dostype = dostype; afsr_seg = seglist; afsr_base = base;
}

static struct DosEnvec *PadDosEnvec(struct DosEnvec *, struct DosEnvec *);
#include "gen/PadDosEnvec.c"                 /* the REAL functions from ../../init.c */
#include "gen/Initialize.c"

/* ---- harness -------------------------------------------------------- */
static int fails;
static void check(int ok, const char *what)
{
    if (!ok) { printf("  BAD   %s\n", what); fails++; }
}

static sigjmp_buf jb;
static int crashed;
static void on_segv(int sig) { siglongjmp(jb, 1); }

static globaldata gdata;
static globaldata *g = &gdata;
static struct FileSysStartupMsg fssm;
static struct DeviceNode devnode;
static UBYTE mname_ok[] = "\011pfs:hello";
static const ULONG SEG = 0x1234560;

static void reset(void)
{
    memset(&gdata, 0, sizeof gdata);
    memset(n, 0, sizeof n);
    memset(pool_flags, 0, sizeof pool_flags);
    fail_port = 0;
    cb_phys = 512; cb_bs = 1024;
    cb_sizeblock = cb_maxtransfer = 0; cb_env = NULL;
    afsr_dostype = 0; afsr_seg = 0; afsr_base = NULL;
    memset(&fssm, 0, sizeof fssm);
    fssm.fssm_Device = MKBADDR("trackdisk.device");
    memset(&devnode, 0, sizeof devnode);
    devnode.dn_Task = &task_port;
    devnode.dn_SegList = SEG;
}

static BOOL go(UBYTE *mname, struct FileSysStartupMsg *f, struct DeviceNode *d, const char *what)
{
    BOOL rc = -1;
    char m[160];

    crashed = 0;
    if (sigsetjmp(jb, 1)) {
        crashed = 1;
        sprintf(m, "%s: CRASHED (read/write outside the DOS table or NULL use)", what);
        check(0, m);
        return -1;
    }
    rc = Initialize(mname, f, d, g);
    return rc;
}

static int calls(void)
{
    int i, t = 0;

    for (i = 0; i < C_COUNT; i++) t += n[i];
    return t;
}

static int allocators_set(void)
{
    return g->allocmemp == AllocPooledVec && g->freememp == FreePooledVec &&
           g->allocbufmem == AllocPooledBuf && g->freebufmem == FreePooledBuf;
}

/* DOS table: guarded = flush against a PROT_NONE page; else plain memory with
 * a poison pattern after the table */
static const ULONG tmpl[20] = {
    19, 128, 0, 2, 1, 32, 2, 0, 0, 0, 100, 40, MEMF_PUBLIC | 4, 0x10000, 0x7FFFFFFE, 0,
    0x50465303, 0, 0, 0 };
struct tbl { ULONG *t; ULONG n, words, snap[24]; char *map; long len; };

static void tbl_make(struct tbl *b, ULONG nn, int guard)
{
    ULONG i;
    long pg = sysconf(_SC_PAGESIZE);

    b->n = nn;
    if (guard) {
        b->len = 2 * pg;
        b->map = mmap(NULL, b->len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (b->map == MAP_FAILED || mprotect(b->map + pg, pg, PROT_NONE)) { printf("  mmap failed\n"); exit(2); }
        b->t = (ULONG *)(b->map + pg - (nn + 1) * sizeof(ULONG));
        b->words = nn + 1;
    } else {
        b->map = NULL;
        b->t = malloc(24 * sizeof(ULONG));
        for (i = 0; i < 24; i++) b->t[i] = 0xDEADBEEFu;
        b->words = 24;
    }
    for (i = 0; i <= nn; i++) b->t[i] = tmpl[i];
    b->t[0] = nn;
    memcpy(b->snap, b->t, b->words * sizeof(ULONG));
}
static void tbl_set(struct tbl *b, ULONG idx, ULONG v) { b->t[idx] = v; b->snap[idx] = v; }
static void tbl_free(struct tbl *b)
{
    if (b->map) munmap(b->map, b->len); else free(b->t);
}
/* the only legal write into a DOS-owned table: the MaxTransfer default of a
 * table that holds the DosType and is used in place */
static int tbl_unchanged(struct tbl *b)
{
    ULONG i;

    for (i = 0; i < b->words; i++) {
        if (i == DE_MAXTRANSFER && b->n >= DE_DOSTYPE) continue;
        if (b->t[i] != b->snap[i]) return 0;
    }
    return 1;
}

static BOOL run_tbl(struct tbl *b, const char *what)
{
    reset();
    fssm.fssm_Environ = MKBADDR(b->t);
    return go(mname_ok, &fssm, &devnode, what);
}

/* a rejected call: FALSE, allocators set, nothing allocated or opened */
static void expect_rejected(BOOL rc, const char *what)
{
    char m[160];

    sprintf(m, "%s: returned %d, want FALSE", what, rc);                    check(rc == FALSE, m);
    sprintf(m, "%s: memory function pointers not set (Quit() needs them)", what);
    check(allocators_set(), m);
    sprintf(m, "%s: something was allocated/opened (%d calls)", what, calls());
    check(calls() == 0, m);
}

int main(void)
{
    struct sigaction sa;
    struct tbl b;
    char what[120], m[200];
    ULONG nn, i, v;
    BOOL rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_segv; sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
    check(offsetof(struct DeviceNode, dn_SegList) ==
          offsetof(struct DosList, dol_misc.dol_handler.dol_SegList), "test struct overlay");

    /* (d) every early return sets the allocator pointers first */
    tbl_make(&b, 19, 1);
    {
        UBYTE empty[] = "\0";
        struct FileSysStartupMsg nodev = { 0, 0, MKBADDR(b.t), 0 };
        struct FileSysStartupMsg noenv = { 0, MKBADDR("dev"), 0, 0 };
        struct { const char *what; UBYTE *mn; struct FileSysStartupMsg *f; struct DeviceNode *d; } c[] = {
            { "NULL mountname", NULL, &fssm, &devnode },
            { "empty mountname", empty, &fssm, &devnode },
            { "NULL fssm", mname_ok, NULL, &devnode },
            { "no fssm_Device", mname_ok, &nodev, &devnode },
            { "no fssm_Environ", mname_ok, &noenv, &devnode },
            { "NULL devnode", mname_ok, &fssm, NULL },
        };
        for (i = 0; i < sizeof c / sizeof c[0]; i++) {
            reset();
            fssm.fssm_Environ = MKBADDR(b.t);
            rc = go(c[i].mn, c[i].f, c[i].d, c[i].what);
            expect_rejected(rc, c[i].what);
            check(g->ErrorMsg == _NormalErrorMsg, "ErrorMsg pointer not set");
        }
    }
    tbl_free(&b);

    /* (a) tables too short to locate the partition */
    for (nn = 0; nn < DE_UPPERCYL; nn++) {
        tbl_make(&b, nn, 1);
        sprintf(what, "TableSize %u", nn);
        rc = run_tbl(&b, what);
        expect_rejected(rc, what);
        sprintf(m, "%s: DOS table written", what); check(tbl_unchanged(&b), m);
        tbl_free(&b);
    }
    tbl_make(&b, DE_UPPERCYL, 1);                /* shortest usable table */
    rc = run_tbl(&b, "TableSize 10");
    check(rc == TRUE && cb_env == &g->envcopy, "TableSize 10 (shortest usable) not accepted");
    tbl_free(&b);

    /* (b) sector size 1..1024 longs, in a full and in a padded table */
    {
        static const ULONG bad[] = { 0, 1025, 4096, 0x40000000u, 0x80000001u, 0xFFFFFFFFu };
        static const ULONG good[] = { 1, 2, 128, 1023, 1024 };
        static const ULONG sizes[] = { 19, 10 };
        int k;

        for (k = 0; k < 2; k++) {
            for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
                tbl_make(&b, sizes[k], 1);
                tbl_set(&b, 1, bad[i]);
                sprintf(what, "SizeBlock %u (TableSize %u)", bad[i], sizes[k]);
                rc = run_tbl(&b, what);
                expect_rejected(rc, what);
                sprintf(m, "%s: DOS table written", what); check(tbl_unchanged(&b), m);
                tbl_free(&b);
            }
            for (i = 0; i < sizeof good / sizeof good[0]; i++) {
                tbl_make(&b, sizes[k], 1);
                tbl_set(&b, 1, good[i]);
                sprintf(what, "SizeBlock %u (TableSize %u)", good[i], sizes[k]);
                rc = run_tbl(&b, what);
                sprintf(m, "%s: refused (rc %d)", what, rc);                  check(rc == TRUE, m);
                sprintf(m, "%s: CalculateBlockSize not reached/seen %u", what, cb_sizeblock);
                check(n[C_CBS] == 1 && cb_sizeblock == good[i], m);
                tbl_free(&b);
            }
        }
    }

    /* (c) MaxTransfer default */
    {
        static const ULONG tsz[] = { 19, 16, 15, 14, 13 };   /* MaxTransfer is inside these tables */
        static const ULONG keep[] = { 1, 0x4000, 0x20000, 0xFFFFFFFFu };
        int k;

        for (k = 0; k < 5; k++) {
            tbl_make(&b, tsz[k], 1);
            tbl_set(&b, DE_MAXTRANSFER, 0);
            sprintf(what, "MaxTransfer 0 (TableSize %u)", tsz[k]);
            rc = run_tbl(&b, what);
            sprintf(m, "%s: not defaulted (cb saw %#x, g has %#x)", what, cb_maxtransfer, g->dosenvec->de_MaxTransfer);
            check(rc == TRUE && cb_maxtransfer == 0x20000 && g->dosenvec->de_MaxTransfer == 0x20000, m);
            if (tsz[k] < DE_DOSTYPE) {
                sprintf(m, "%s: DOS table written", what);
                check(tbl_unchanged(&b) && b.t[DE_MAXTRANSFER] == 0, m);
            }
            tbl_free(&b);
            for (i = 0; i < sizeof keep / sizeof keep[0]; i++) {
                tbl_make(&b, tsz[k], 1);
                tbl_set(&b, DE_MAXTRANSFER, keep[i]);
                sprintf(what, "MaxTransfer %#x (TableSize %u)", keep[i], tsz[k]);
                rc = run_tbl(&b, what);
                sprintf(m, "%s: changed to %#x", what, g->dosenvec->de_MaxTransfer);
                check(rc == TRUE && cb_maxtransfer == keep[i] && g->dosenvec->de_MaxTransfer == keep[i], m);
                tbl_free(&b);
            }
        }
        tbl_make(&b, 10, 1);                              /* too short to hold it: default */
        rc = run_tbl(&b, "MaxTransfer, TableSize 10");
        check(rc == TRUE && cb_maxtransfer == 0x20000, "MaxTransfer of a TableSize 10 table not 0x20000");
        tbl_free(&b);
    }

    /* (e) padded copy versus in-place use */
    for (nn = DE_UPPERCYL; nn <= 19; nn++) {
        tbl_make(&b, nn, 1);
        sprintf(what, "TableSize %u", nn);
        rc = run_tbl(&b, what);
        sprintf(m, "%s: refused", what);                                      check(rc == TRUE, m);
        sprintf(m, "%s: DOS table written", what);                            check(tbl_unchanged(&b), m);
        if (nn < DE_DOSTYPE) {
            sprintf(m, "%s: g->dosenvec is not g->envcopy", what);
            check(g->dosenvec == &g->envcopy && cb_env == &g->envcopy, m);
            sprintf(m, "%s: padded TableSize %u", what, g->dosenvec->de_TableSize);
            check(g->dosenvec->de_TableSize == (nn >= DE_MASK ? nn : DE_MASK), m);
            for (i = 1; i <= nn; i++)
                if (i != DE_MAXTRANSFER) {
                    sprintf(m, "%s: field %u not copied", what, i);
                    check(((ULONG *)g->dosenvec)[i] == tmpl[i], m);
                }
            check(g->dosenvec->de_NumBuffers == (nn >= DE_NUMBUFFERS ? tmpl[DE_NUMBUFFERS] : 30),
                  "padded NumBuffers");
            v = nn >= DE_MEMBUFTYPE ? tmpl[DE_MEMBUFTYPE] : MEMF_PUBLIC;
            sprintf(m, "%s: BufMemType %#x, buffer pool created with %#x, want %#x", what,
                    g->dosenvec->de_BufMemType, pool_flags[1], v);
            check(g->dosenvec->de_BufMemType == v && pool_flags[1] == v, m);
            sprintf(m, "%s: Mask %#x", what, g->dosenvec->de_Mask);
            check(g->dosenvec->de_Mask == (nn >= DE_MASK ? tmpl[DE_MASK] : 0xFFFFFFFEu), m);
            sprintf(m, "%s: padded copy holds DosType %#x", what, g->dosenvec->de_DosType);
            check(g->dosenvec->de_DosType == 0, m);
        } else {
            sprintf(m, "%s: full table not used in place", what);
            check(g->dosenvec == (struct DosEnvec *)b.t && cb_env == (struct DosEnvec *)b.t, m);
            sprintf(m, "%s: buffer pool memory type %#x", what, pool_flags[1]);
            check(pool_flags[1] == tmpl[DE_MEMBUFTYPE], m);
        }
        tbl_free(&b);
    }

    /* (f) the filesystem is registered only if the table holds the DosType */
    for (nn = DE_UPPERCYL; nn <= 19; nn++) {
        tbl_make(&b, nn, 0);                     /* poison after the table */
        sprintf(what, "TableSize %u", nn);
        rc = run_tbl(&b, what);
        check(rc == TRUE, "AddToFSResource case refused");
        if (nn >= DE_DOSTYPE) {
            sprintf(m, "%s: AddToFSResource called %d times, want 1", what, n[C_AFSR]);
            check(n[C_AFSR] == 1, m);
            sprintf(m, "%s: AddToFSResource(%#x, %#lx)", what, afsr_dostype, (long)afsr_seg);
            check(afsr_dostype == tmpl[DE_DOSTYPE] && afsr_seg == (BPTR)SEG && afsr_base == &execbase_obj, m);
        } else {
            sprintf(m, "%s: AddToFSResource(%#x) called for a table without DosType", what, afsr_dostype);
            check(n[C_AFSR] == 0, m);
        }
        tbl_free(&b);
    }
    tbl_make(&b, 19, 1);                         /* a different DosType and SegList */
    tbl_set(&b, DE_DOSTYPE, 0x44534B01u);
    reset();
    fssm.fssm_Environ = MKBADDR(b.t);
    devnode.dn_SegList = 0x777000;
    rc = go(mname_ok, &fssm, &devnode, "other DosType");
    check(rc == TRUE && n[C_AFSR] == 1 && afsr_dostype == 0x44534B01u && afsr_seg == 0x777000,
          "AddToFSResource arguments follow the table and the device node");
    tbl_free(&b);

    /* success path: everything wired up, names copied */
    tbl_make(&b, 19, 1);
    rc = run_tbl(&b, "success");
    check(rc == TRUE, "success path returned FALSE");
    check(allocators_set() && g->harddiskmode == TRUE, "success: allocators / harddiskmode");
    check(g->mountname && g->mountname[0] == 3 && !memcmp(g->mountname + 1, "pfs", 3), "success: mountname");
    check(g->startup == &fssm && g->devnode == &devnode && g->msgport == &task_port &&
          g->notifyport && g->mainPool && g->bufferPool && g->geom && g->flushrequest,
          "success: globals not filled in");
    check(n[C_POOLVEC] == 1 && n[C_OPENDISK] == 1 && n[C_SEMA] == 1 && n[C_DCACHE] == 1 &&
          n[C_TIMER] == 1 && n[C_CIO] == 1 && n[C_OPENDEV] == 1 && n[C_PORT] == 2 &&
          n[C_REMOVABLE] == 1 && n[C_RESET] == 1 && n[C_CBS] == 1, "success: call sequence");
    tbl_free(&b);

    /* (d) later failures keep the pointers too, and stop where they should */
    tbl_make(&b, 19, 1);
    reset();
    fssm.fssm_Environ = MKBADDR(b.t);
    fail_port = 1;                               /* the notify port */
    rc = go(mname_ok, &fssm, &devnode, "no notifyport");
    check(rc == FALSE && allocators_set() && n[C_POOL] == 0 && n[C_AFSR] == 0, "notifyport failure");
    reset();
    fssm.fssm_Environ = MKBADDR(b.t);
    cb_phys = 520;                               /* sector size not a power of two */
    rc = go(mname_ok, &fssm, &devnode, "odd sector size");
    check(rc == FALSE && allocators_set() && n[C_OPENDISK] == 0 && n[C_AFSR] == 0, "odd sector size accepted");
    tbl_free(&b);

    printf("== initialize: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
