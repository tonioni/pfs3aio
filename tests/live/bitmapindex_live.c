/* LIVE @src volume.c @func CheckBitmapIndex
 *
 * Proof of the mount-time bitmap index validation. Lifts the checkout's real
 * CheckBitmapIndex and asserts CORRECT behaviour: in a supermode rootblock
 * every non-zero bitmap index entry must point into the reserved area, because
 * the allocator reads and writes whatever block the entry names. Rootblocks
 * without split anodes do not use the field as a bitmap index. Layouts
 * written by the real formatter must be accepted unchanged.
 *
 * A tree without the check has no such function and this test FAILS.
 */
#include <stdio.h>
#include <stdint.h>
typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAXBITMAPINDEX 103
#define MODE_SPLITTED_ANODES 2

/* the real rootblock_t is exactly 512 bytes; keep only the fields used */
typedef struct rootblock {
    ULONG options, lastreserved, firstreserved;
    union { struct { ULONG bitmapindex[MAXBITMAPINDEX + 1]; } large; } idx;
} rootblock_t;

#include "gen/CheckBitmapIndex.c"           /* the REAL function from ../../volume.c */

static int fails;

static void base(struct rootblock *r)
{
    int i;

    r->options = MODE_SPLITTED_ANODES;
    r->firstreserved = 2;
    r->lastreserved = 65;
    for (i = 0; i <= MAXBITMAPINDEX; i++)
        r->idx.large.bitmapindex[i] = 0;
}

static void expect(const char *what, struct rootblock *r, int want)
{
    int got = CheckBitmapIndex(r) ? 1 : 0;

    printf("  %-44s -> %s %s\n", what, got ? "accept" : "reject", got == want ? "ok" : "BAD");
    if (got != want)
        fails++;
}

int main(void)
{
    struct rootblock r;

    base(&r);
    expect("empty index", &r, 1);
    base(&r); r.idx.large.bitmapindex[3] = 40;
    expect("entry inside the reserved area", &r, 1);
    base(&r); r.idx.large.bitmapindex[0] = 2; r.idx.large.bitmapindex[MAXBITMAPINDEX] = 65;
    expect("entries on both edges of the area", &r, 1);
    base(&r); r.idx.large.bitmapindex[3] = 5000;
    expect("entry past the reserved area", &r, 0);
    base(&r); r.idx.large.bitmapindex[7] = 1;
    expect("entry before the reserved area", &r, 0);
    base(&r); r.idx.large.bitmapindex[MAXBITMAPINDEX] = 66;
    expect("last entry one past the area", &r, 0);
    base(&r); r.options = 0; r.idx.large.bitmapindex[3] = 5000;
    expect("old layout: idx is not a bitmap index", &r, 1);

    if (fails) {
        printf("  FAIL: %d check(s) wrong\n", fails);
        return 1;
    }
    printf("  PASS: bitmap index entries outside the reserved area rejected\n");
    return 0;
}
