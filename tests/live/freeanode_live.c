/* LIVE @src anodes.c @func FreeAnode
 *
 * Proof of the FreeAnode split-mode seqnr fix. Lifts the checkout's real
 * FreeAnode and asserts CORRECT behaviour on a NON-split-anode volume: freeing
 * an anode must set the has-free-slot bit for the anodeblock that actually held
 * it, whose sequence number is anodenr / anodesperblock.
 *
 * A tree that uses anodenr>>16 unconditionally marks the wrong anodeblock in
 * non-split mode, so the correct bit is never set -- this test FAILS. With the
 * mode-aware fix it PASSES.
 */
#include <stdio.h>
#include <stdint.h>
typedef uint32_t ULONG; typedef uint8_t UBYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0

#define ANODE_USERFIRST 6
struct canode { ULONG clustersize, blocknr, next, nr; };
typedef struct globaldata { UBYTE anodesplitmode; } globaldata;
struct { ULONG *anblkbitmap; ULONG anodesperblock; ULONG anblkbitmapsize; } andata;
static BOOL SaveAnode(struct canode *a, ULONG nr, globaldata *g) { (void)a;(void)nr;(void)g; return TRUE; }

#include "gen/FreeAnode.c"                   /* the REAL function from ../../anodes.c */

int main(void) {
    ULONG bitmap[256] = {0};
    andata.anblkbitmap = bitmap;
    andata.anblkbitmapsize = 256;
    andata.anodesperblock = 41;
    globaldata g = { .anodesplitmode = 0 };  /* non-split volume */

    ULONG seqnr = 100, offset = 5;
    ULONG anodenr = seqnr * andata.anodesperblock + offset;   /* 4105 */
    FreeAnode(anodenr, &g);

    int correct = (bitmap[seqnr / 32] >> (31 - (seqnr % 32))) & 1;   /* block 100 -> [3] bit 27 */
    int wrong   = (bitmap[(anodenr >> 16) / 32] >> (31 - ((anodenr >> 16) % 32))) & 1;
    printf("  anodenr=%u (block seqnr=%u): correct-bit set=%d  anodenr>>16 bit set=%d\n",
           anodenr, seqnr, correct, wrong);
    if (correct) { printf("  PASS: the right anodeblock's free bit was set\n"); return 0; }
    printf("  FAIL: wrong anodeblock marked (used anodenr>>16 in non-split mode)\n");
    return 1;
}
