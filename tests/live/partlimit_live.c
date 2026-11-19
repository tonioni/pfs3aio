/* LIVE @src volume.c @func SetPartitionLimits
 *
 * Proof of the 2TB partition-end wrap fix. Lifts the checkout's real
 * SetPartitionLimits and asserts CORRECT behaviour: for a partition high on a
 * disk larger than 2^32 sectors, (de_HighCyl+1)*dg_CylSectors overflows 32
 * bits; the result must still leave lastblock >= firstblock (a bounded,
 * non-inverted partition).
 *
 * A tree that computes the product without saturating wraps to a lastblock
 * below firstblock -- an inverted partition, InPartition() rejects everything --
 * and this test FAILS. With the saturating fix it PASSES.
 *
 * Compiled with LIMIT_MAXTRANSFER undefined so the scsi.device block is out.
 */
#include <stdio.h>
#include <stdint.h>
#include "live.h"

struct dosenvec { ULONG de_LowCyl, de_HighCyl; };
struct drivegeom { ULONG dg_CylSectors; };
typedef struct globaldata {
    struct dosenvec *dosenvec;
    struct drivegeom *geom;
    int blocklogshift;
    ULONG firstblocknative, lastblocknative, firstblock, lastblock, maxtransfermax;
} globaldata;

#include "gen/SetPartitionLimits.c"          /* the REAL function from ../../volume.c */

int main(void) {
    /* partition high on a >2TB disk: (HighCyl+1)*CylSectors > 2^32 */
    struct dosenvec e = { .de_LowCyl = 60000, .de_HighCyl = 70000 };
    struct drivegeom geo = { .dg_CylSectors = 65536 };
    globaldata g = { .dosenvec = &e, .geom = &geo, .blocklogshift = 0 };
    SetPartitionLimits(&g);

    printf("  firstblock=%u  lastblock=%u\n", g.firstblock, g.lastblock);
    if (g.lastblock >= g.firstblock) {
        printf("  PASS: partition stays bounded (saturated, not wrapped)\n");
        return 0;
    }
    printf("  FAIL: lastblock < firstblock -- inverted partition, "
           "bounds check broken\n");
    return 1;
}
