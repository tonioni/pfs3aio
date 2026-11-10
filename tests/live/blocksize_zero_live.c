/* LIVE @src volume.c @func CalculateBlockSize
 *
 * Proof of the zero block-size fix. Lifts the checkout's real
 * CalculateBlockSize and asserts CORRECT behaviour: a mountlist with
 * de_SizeBlock == 0 (or one so large that the shift wraps to 0) must not make
 * the function spin forever; it falls back to 512 byte sectors.
 *
 * A tree that keeps `bs = 0` loops in `while (bs < g->blocksize) bs <<= 1`;
 * the alarm turns that hang into a FAIL. With the fix it PASSES.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef int16_t WORD; typedef uint16_t UWORD;
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

static void hung(int sig) {
    (void)sig;
    fflush(stdout);
    printf("  FAIL: CalculateBlockSize does not terminate with a zero sector size\n");
    fflush(stdout);
    _exit(1);
}

static int check(ULONG sizeblock, ULONG spb) {
    struct dosenvec e = { .de_SizeBlock = sizeblock, .de_SectorPerBlock = spb };
    globaldata g = { .dosenvec = &e };
    int ok;

    alarm(5);
    CalculateBlockSize(&g, 0, 0);
    alarm(0);
    ok = g.blocksize >= 512 && g.blocksize <= 4096 && !(g.blocksize & (g.blocksize - 1)) &&
         g.blocksize == (g.blocksize_phys << g.blocklogshift);
    printf("  SizeBlock=%u spb=%u -> blocksize=%u phys=%u logshift=%d  %s\n", sizeblock,
           spb, g.blocksize, g.blocksize_phys, g.blocklogshift, ok ? "ok" : "BAD");
    return ok;
}

int main(void) {
    int ok = 1;

    signal(SIGALRM, hung);
    ok &= check(0, 0);
    ok &= check(0, 2);
    ok &= check(0x40000000, 1);              /* << 2 wraps to 0 */
    ok &= check(128, 1);                     /* regular 512 byte block unchanged */
    if (!ok) {
        printf("  FAIL: block size is not a sane power of two\n");
        return 1;
    }
    printf("  PASS: zero/overflowed sector size terminates with a sane block size\n");
    return 0;
}
