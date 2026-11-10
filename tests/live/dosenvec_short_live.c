/* LIVE @src init.c @func PadDosEnvec
 *
 * Proof of the short-DosEnvec handling. Lifts the checkout's real
 * PadDosEnvec and asserts CORRECT behaviour:
 *   - a table holding every field up to de_DosType is used as is (no copy);
 *   - a shorter table that still has the cylinder range is copied and
 *     padded with defaults; fields inside the table keep their values, and
 *     the DOS-owned table is never written. The table sits flush against a
 *     PROT_NONE guard page, so any read or write past its end crashes;
 *   - the padded copy reports a table size below de_DosType and holds a zero
 *     DosType, so the filesystem is not registered under, and the mount
 *     does not test, a DosType that is not in the table (tables of 14 and
 *     15 entries included);
 *   - a table too short to locate the partition is refused.
 * A tree that refuses short tables or reads past them FAILS this test.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD;
#define MEMF_PUBLIC 1

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

#include "gen/PadDosEnvec.c"                 /* the REAL function from ../../init.c */

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) { printf("  BAD   %s\n", what); fails++; }
}

/* n+1 longs ending exactly at an inaccessible page */
static ULONG *guarded(ULONG n, char **map, long *len)
{
    long pg = sysconf(_SC_PAGESIZE);

    *len = 2 * pg;
    *map = mmap(NULL, *len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (*map == MAP_FAILED || mprotect(*map + pg, pg, PROT_NONE)) {
        printf("  mmap failed\n");
        exit(2);
    }
    return (ULONG *)(*map + pg - (n + 1) * sizeof(ULONG));
}

static void run(ULONG n)
{
    ULONG ref[32];
    struct DosEnvec copy, *r;
    ULONG i, *buf;
    char *map;
    long len;
    char what[96];

    buf = guarded(n, &map, &len);
    buf[0] = n;
    for (i = 1; i <= n; i++) buf[i] = 1000 + i;
    memcpy(ref, buf, (n + 1) * sizeof(ULONG));
    memset(&copy, 0xA5, sizeof copy);

    r = PadDosEnvec((struct DosEnvec *)buf, &copy);
    sprintf(what, "TableSize %u", n);
    printf("  %-14s -> %s\n", what, r == NULL ? "refused" : r == (struct DosEnvec *)buf ? "as is" : "padded copy");

    check(!memcmp(buf, ref, (n + 1) * sizeof(ULONG)), "DOS-owned table written");
    if (n < DE_UPPERCYL) { check(r == NULL, "too-short table not refused"); goto done; }
    check(r != NULL, "usable short table refused");
    if (!r) goto done;
    if (n >= DE_DOSTYPE) { check(r == (struct DosEnvec *)buf, "full table was copied"); goto done; }

    check(r == &copy, "short table not copied");
    check(r->de_TableSize == (n >= DE_MASK ? n : DE_MASK) && r->de_TableSize < DE_DOSTYPE,
          "padded TableSize wrong");
    for (i = 1; i <= n; i++)
        check(((ULONG *)r)[i] == 1000 + i, "in-table field not preserved");
    check(r->de_NumBuffers  == (n >= DE_NUMBUFFERS  ? 1000 + DE_NUMBUFFERS  : 30), "NumBuffers");
    check(r->de_BufMemType  == (n >= DE_MEMBUFTYPE  ? 1000 + DE_MEMBUFTYPE  : MEMF_PUBLIC), "BufMemType");
    check(r->de_MaxTransfer == (n >= DE_MAXTRANSFER ? 1000 + DE_MAXTRANSFER : 0x20000), "MaxTransfer");
    check(r->de_Mask == (n >= DE_MASK ? 1000 + DE_MASK : 0xFFFFFFFE), "Mask");
    check(r->de_DosType == 0, "padded copy holds a DosType");
done:
    munmap(map, len);
}

int main(void)
{
    ULONG n;

    setvbuf(stdout, NULL, _IONBF, 0);
    check(offsetof(struct DosEnvec, de_HighCyl) == DE_UPPERCYL * 4 &&
          offsetof(struct DosEnvec, de_NumBuffers) == DE_NUMBUFFERS * 4 &&
          offsetof(struct DosEnvec, de_BufMemType) == DE_MEMBUFTYPE * 4 &&
          offsetof(struct DosEnvec, de_MaxTransfer) == DE_MAXTRANSFER * 4 &&
          offsetof(struct DosEnvec, de_Mask) == DE_MASK * 4 &&
          offsetof(struct DosEnvec, de_DosType) == DE_DOSTYPE * 4, "test struct layout");
    for (n = 0; n <= 19; n++)
        run(n);
    printf("== dosenvec_short: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
