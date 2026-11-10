/* LIVE @src volume.c @func SetPartitionLimits
 *
 * Proof of the fail-closed partition limits. Lifts the checkout's real
 * SetPartitionLimits and asserts CORRECT behaviour: a partition described by
 * an invalid DosEnvec/geometry must end up with firstblock > lastblock, so
 * InPartition()/BoundsCheck() reject every access. A tree that wraps the
 * 32-bit arithmetic (zero cylinder size, start beyond 2^32 sectors,
 * HighCyl 0xFFFFFFFF, LowCyl > HighCyl, partition smaller than one block)
 * yields limits that cover the whole disk (or another partition), and this
 * test FAILS. Valid partitions must keep their exact limits.
 *
 * Compiled with LIMIT_MAXTRANSFER undefined so the scsi.device block is out.
 */
#include <stdio.h>
#include <stdint.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef int16_t WORD; typedef uint16_t UWORD;

struct dosenvec { ULONG de_LowCyl, de_HighCyl; };
struct drivegeom { ULONG dg_CylSectors; };
typedef struct globaldata {
    struct dosenvec *dosenvec;
    struct drivegeom *geom;
    int blocklogshift;
    ULONG firstblocknative, lastblocknative, firstblock, lastblock, maxtransfermax;
} globaldata;

#include "gen/SetPartitionLimits.c"          /* the REAL function from ../../volume.c */

static int fails;

static void run(const char *what, ULONG lo, ULONG hi, ULONG cs, int shift,
                int valid, ULONG efirst, ULONG elast, ULONG efirstn, ULONG elastn)
{
    struct dosenvec e = { .de_LowCyl = lo, .de_HighCyl = hi };
    struct drivegeom geo = { .dg_CylSectors = cs };
    globaldata g = { .dosenvec = &e, .geom = &geo, .blocklogshift = shift,
                     .firstblock = 7, .lastblock = 9 };
    int ok;

    SetPartitionLimits(&g);
    if (valid)
        ok = g.firstblock == efirst && g.lastblock == elast &&
             g.firstblocknative == efirstn && g.lastblocknative == elastn;
    else
        ok = g.firstblock > g.lastblock && g.firstblocknative > g.lastblocknative;
    printf("  %-34s first=%u last=%u native=%u..%u  %s\n", what, g.firstblock,
           g.lastblock, g.firstblocknative, g.lastblocknative, ok ? "ok" : "BAD");
    if (!ok)
        fails++;
}

int main(void) {
    run("valid, 512-byte blocks",         2, 10, 1008, 0, 1, 2016, 11087, 2016, 11087);
    run("valid, unaligned start (1K)",    1, 1, 1007, 1, 1, 504, 1006, 1008, 2013);
    run("zero cylinder size",             0, 0xFFFFFFFF, 0, 0, 0, 0, 0, 0, 0);
    run("zero cylinder size, small",      3, 9, 0, 0, 0, 0, 0, 0, 0);
    run("LowCyl*CylSectors overflows",    70000, 80000, 65536, 0, 0, 0, 0, 0, 0);
    run("LowCyl > HighCyl",               100, 50, 1008, 0, 0, 0, 0, 0, 0);
    run("HighCyl 0xFFFFFFFF",             0, 0xFFFFFFFF, 1008, 0, 0, 0, 0, 0, 0);
    run("smaller than one 4K block",      0, 0, 4, 3, 0, 0, 0, 0, 0);
    if (fails) {
        printf("  FAIL: %d invalid partition(s) are not rejected (or a valid one changed)\n", fails);
        return 1;
    }
    printf("  PASS: invalid partitions fail closed, valid ones keep their limits\n");
    return 0;
}
