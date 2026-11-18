/* LIVE @src disk.c @func RawReadWrite_DS
 *
 * Proof of the DirectSCSI maxtransfer units fix. Lifts the checkout's real
 * RawReadWrite_DS and asserts CORRECT behaviour: with 4KB logical blocks and a
 * 16KiB MaxTransfer, a transfer must succeed (issue SCSI commands), not fail.
 *
 * A tree that derives maxtransfer in filesystem-block units rounds 16KiB down
 * to 0 (>> BLOCKSHIFT then & ~7) and returns ERROR_BAD_NUMBER -- every
 * DirectSCSI transfer fails -- so this test FAILS. With the native-sector fix
 * the transfer succeeds and it PASSES.
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

static int ncmd;
static BOOL BoundsCheck(BOOL w, ULONG b, ULONG n, globaldata *g){(void)w;(void)b;(void)n;(void)g;return 1;}
static BOOL DoSCSICommand(UBYTE *d, ULONG dl, ULONG ml, UBYTE *c, UBYTE dir, globaldata *g){
    (void)d;(void)dl;(void)ml;(void)c;(void)dir;(void)g; ncmd++; return 1; }
static BOOL ErrorRequest(BOOL w, UWORD s, ULONG b, ULONG t, globaldata *g){
    (void)w;(void)s;(void)b;(void)t;(void)g; return 0; }

#include "gen/RawReadWrite_DS.c"             /* the REAL function from ../../disk.c */

int main(void) {
    struct dosenvec e = { .de_MaxTransfer = 0x4000 };   /* 16 KiB */
    globaldata g = { .firstblock = 0, .blocklogshift = 3, .blockshift = 12,  /* 4096-byte blocks */
                     .maxtransfermax = 0x7ffffffe, .softprotect = 0, .dosenvec = &e };
    UBYTE buf[1 << 20];
    ULONG rc = RawReadWrite_DS(0 /*write*/, buf, 64, 1000, &g);

    printf("  4KB blocks, MaxTransfer=16KiB: rc=%u, %d SCSI commands issued\n", rc, ncmd);
    if (rc == 0 && ncmd > 0) { printf("  PASS: transfer succeeded\n"); return 0; }
    printf("  FAIL: transfer rejected (maxtransfer rounded to 0 in fs-block units)\n");
    return 1;
}
