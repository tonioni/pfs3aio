/* LIVE @src disk.c @func RawReadWrite_DS
 *
 * Proof of the DirectSCSI retry double-shift fix (#1). Lifts the checkout's
 * real RawReadWrite_DS, drives it with a stubbed SCSI layer that injects one
 * transient error to force a retry, and asserts CORRECT behaviour: every SCSI
 * command must address an LBA inside the partition's native-sector range.
 *
 * A tree whose retry: label sits above the fs-block->native-sector conversion
 * re-applies blocklogshift on retry and issues commands far outside the
 * partition -- this test FAILS. With the fix it restarts in range and PASSES.
 */
#include <stdio.h>
#include <stdint.h>
#include "live.h"

#define SCSIF_WRITE 0
#define SCSIF_READ  1
#define PROFILE_OFF()
#define PROFILE_ON()
#define min(a,b) ((a)<(b)?(a):(b))

struct dosenvec { ULONG de_MaxTransfer; };
struct scsicmd  { UWORD scsi_Status; };
typedef struct globaldata {
    ULONG firstblock; int blocklogshift, blockshift; ULONG maxtransfermax;
    int softprotect; struct dosenvec *dosenvec; struct scsicmd scsicmd;
} globaldata;
#define BLOCKSHIFT       (g->blockshift)
#define BLOCKNATIVESHIFT (g->blockshift - g->blocklogshift)

static ULONG nat_lo, nat_hi; static int oob, ncmd, callno, fault_fired, retries;
static BOOL BoundsCheck(BOOL w, ULONG b, ULONG n, globaldata *g){(void)w;(void)b;(void)n;(void)g;return 1;}
static BOOL DoSCSICommand(UBYTE *d, ULONG dl, ULONG ml, UBYTE *cmd, UBYTE dir, globaldata *g){
    (void)d;(void)dl;(void)ml;(void)dir;(void)g;
    ULONG lba = *((ULONG*)&cmd[2]);
    ncmd++; if (lba < nat_lo || lba >= nat_hi) oob++;
    if (++callno == 2 && !fault_fired) { fault_fired = 1; return 0; }   /* fail 2nd once */
    return 1;
}
static BOOL ErrorRequest(BOOL w, UWORD s, ULONG b, ULONG t, globaldata *g){
    (void)w;(void)s;(void)b;(void)t;(void)g; return (++retries <= 4); }   /* Retry, capped */

#include "gen/RawReadWrite_DS.c"             /* the REAL function from ../../disk.c */

int main(void) {
    struct dosenvec e = { .de_MaxTransfer = 0x10000 };
    globaldata g = { .firstblock = 0, .blocklogshift = 2, .blockshift = 11,
                     .maxtransfermax = 0x7ffffffe, .softprotect = 0, .dosenvec = &e };
    ULONG blocknr = 1000, blocks = 64;       /* 2048-byte logical blocks */
    nat_lo = blocknr << g.blocklogshift;
    nat_hi = nat_lo + (blocks << g.blocklogshift);
    UBYTE buf[1 << 20];
    RawReadWrite_DS(0 /*write*/, buf, blocks, blocknr, &g);

    printf("  native range [%u,%u): %d SCSI commands, %d outside\n", nat_lo, nat_hi, ncmd, oob);
    if (oob == 0) { printf("  PASS: every retried command stayed in the partition\n"); return 0; }
    printf("  FAIL: %d command(s) issued OUTSIDE the partition on retry\n", oob);
    return 1;
}
