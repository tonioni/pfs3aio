/* LIVE @src volume.c @func CalculateBlockSize
 *
 * Proof of the block-size normalization fix (#15). Lifts the checkout's real
 * CalculateBlockSize and asserts CORRECT behaviour: a computed logical block
 * size must be normalized to a power of two, so that blocksize, blockshift
 * (log2 blocksize) and blocklogshift (log2 blocksize/sector) all agree.
 *
 * A tree that still accepts 1536 (= 512-byte sectors * SectorsPerBlock 3)
 * yields blockshift=10 (1024) and blocklogshift=2 (2048) -- the trackdisk and
 * DirectSCSI paths would address different sectors -- and this test FAILS.
 * With the fix (normalize to 1024) it PASSES.
 */
#include <stdio.h>
#include <stdint.h>
#include "live.h"
#define ACCESS_DETECT 1                      /* compile out the #if ACCESS_DETECT==0 block */

struct dosenvec { ULONG de_SizeBlock; ULONG de_SectorPerBlock; };
typedef struct globaldata {
    struct dosenvec *dosenvec;
    ULONG blocksize_phys, blocksize;
    int blocklogshift, blockshift;
    ULONG directsize;
} globaldata;
#define BLOCKSIZE (g->blocksize)
static void SetPartitionLimits(globaldata *g) { (void)g; }

#include "gen/CalculateBlockSize.c"          /* the REAL function from ../../volume.c */

int main(void) {
    struct dosenvec e = { .de_SizeBlock = 128, .de_SectorPerBlock = 3 };  /* 512*3 = 1536 */
    globaldata g = { .dosenvec = &e };
    CalculateBlockSize(&g, 0, 0);

    int pow2   = (g.blocksize & (g.blocksize - 1)) == 0;
    int consistent = (g.blocksize == (1u << g.blockshift)) &&
                     (g.blocksize == (g.blocksize_phys << g.blocklogshift));
    printf("  blocksize=%u  blockshift=%d(=%u)  blocklogshift=%d(=%u)\n",
           g.blocksize, g.blockshift, 1u << g.blockshift,
           g.blocklogshift, g.blocksize_phys << g.blocklogshift);
    if (pow2 && consistent) { printf("  PASS: normalized to a consistent power of two\n"); return 0; }
    printf("  FAIL: block size is not a consistent power of two "
           "(TD and DS paths disagree)\n");
    return 1;
}
