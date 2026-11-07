/* LIVE @src directory.c @func LoadDirBlock @func ProtectFile @func GetExtraFields @func AddExtraFields
 * Load real directory entries, then exercise a consumer of the cached block.
 * Mocks supply I/O/cache plumbing; parsing and protection updates are live code.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG;
typedef uint16_t UWORD;
typedef uint8_t UBYTE;
typedef intptr_t SIPTR;
typedef intptr_t IPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DOSFALSE 0
#define DOSTRUE 1
#define ERROR_DISK_FULL 221
#define AFS_ERROR_DNV_WRONG_DIRID 0
#define FNSIZE 108
#define CMSIZE 80
#define MAX_ENTRYSIZE (sizeof(struct direntry) + FNSIZE + CMSIZE + 34)
#define DBLKID 0x4442
#define HASHM_DIR 0
#define RESCLUSTER 1
#define DB_ENTRYSPACE (g->blocksize - sizeof(struct dirblock))
#define ENTER(...)
#define EXIT(...)
#define DB(...)
#define ReservedAreaIsLocked 0
#define FIRSTENTRY(b) ((struct direntry *)((b)->blk.entries))
#define NEXTENTRY(e) ((struct direntry *)((UBYTE *)(e) + (e)->next))
#define COMMENT(e) (&(e)->startofname + (e)->nlength)
#define MakeBlockDirty(...) (++dirty_calls)

#pragma pack(push, 2)
struct direntry {
    UBYTE next, type;
    ULONG anode, fsize;
    UWORD day, minute, tick;
    UBYTE protection, nlength, startofname, pad;
};
struct dirblock { UWORD id; UBYTE padding[18], entries[]; };
struct extrafields {
    ULONG link;
    UWORD uid, gid;
    ULONG prot, virtualsize, rollpointer;
    UWORD fsizex;
};
#pragma pack(pop)
union extrawords {
    struct extrafields fields;
    UWORD w[sizeof(struct extrafields) / 2];
};
struct volumedata { void *dirblks; };
struct cdirblock {
    struct volumedata *volume;
    ULONG blocknr;
    int used, changeflag;
    struct dirblock blk;
};
typedef struct {
    struct volumedata *currentvolume;
    int dirextension;
    ULONG blocksize;
} globaldata;
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
union objectinfo { struct fileinfo file; };
static UBYTE disk[4096];
static struct cdirblock *cached;
static unsigned allocations, frees, hashes, dirty_calls, changes;

#define CheckCache(...) cached
static struct cdirblock *AllocLRU(globaldata *g)
{
    ++allocations;
    return calloc(1, offsetof(struct cdirblock, blk) + g->blocksize);
}
#define FreeLRU(b) (++frees, free(b))
static int RawRead(UBYTE *data, int count, ULONG block, globaldata *g)
{
    (void)count;
    (void)block;
    memcpy(data, disk, g->blocksize);
    return 0;
}
#define Hash(b, ...) (++hashes, cached = (b))
#define UpdateReference(...)
#define ErrorMsg(...)
static int GetParent(union objectinfo *file, union objectinfo *dir,
    SIPTR *error, globaldata *g)
{
    (void)file; (void)dir; (void)error; (void)g;
    return 1;
}
static int ChangeDirEntry(struct fileinfo from, struct direntry *to,
    union objectinfo *parent, struct fileinfo *result, globaldata *g)
{
    (void)from; (void)parent; (void)result; (void)g;
    assert(to->next <= MAX_ENTRYSIZE);
    ++changes;
    return 1;
}
#include "gen/GetExtraFields.c"
#include "gen/AddExtraFields.c"
#include "gen/LoadDirBlock.c"
#include "gen/ProtectFile.c"

static struct direntry *entry(ULONG offset, UBYTE size, UBYTE name, UBYTE comment)
{
    struct direntry *e = (struct direntry *)(disk + sizeof(struct dirblock) + offset);
    memset(e, 0, size);
    e->next = size;
    e->nlength = name;
    if (offsetof(struct direntry, startofname) + name < size)
        *COMMENT(e) = comment;
    return e;
}

static void fresh(void)
{
    memset(disk, 0, sizeof(disk));
    ((struct dirblock *)disk)->id = DBLKID;
    cached = NULL;
    allocations = frees = hashes = dirty_calls = changes = 0;
}

static void check(globaldata *g, BOOL accepted, BOOL protect)
{
    struct cdirblock *b = LoadDirBlock(100, g);
    assert(allocations == 1);
    if (!accepted) {
        assert(!b && !cached && frees == 1 && hashes == 0);
        assert(!dirty_calls && !changes);
        return;
    }
    assert(b && !frees && hashes == 1);
    assert(LoadDirBlock(100, g) == b && allocations == 1);
    if (protect) {
        struct fileinfo file = { FIRSTENTRY(b), b };
        SIPTR error = 0;
        assert(ProtectFile(&file, 0x12340000, &error, g));
        assert(dirty_calls == 1);
        assert(changes == (unsigned)g->dirextension);
    }
    free(b);
    cached = NULL;
}

int main(void)
{
    struct volumedata volume = { NULL };
    globaldata g = { &volume, 1, 1024 };
    assert(sizeof(struct direntry) == 20 && sizeof(struct dirblock) == 20);
    assert(sizeof(struct extrafields) == 22 && MAX_ENTRYSIZE == 242);

    for (g.blocksize = 512; g.blocksize <= 4096; g.blocksize *= 2) {
        fresh(); /* empty directory */
        check(&g, TRUE, FALSE);
        fresh();
        entry(0, 254, 1, 0); /* used to pass the loader, overflow ProtectFile */
        check(&g, FALSE, FALSE);
        fresh();
        entry(0, 242, 107, 79); /* largest supported strings, spare padding */
        check(&g, TRUE, TRUE);
        fresh();
        struct direntry *full = entry(0, 230, 107, 79);
        UWORD *fields = (UWORD *)((UBYTE *)full + 206);
        for (unsigned i = 0; i < 11; ++i)
            fields[i] = 1;
        fields[11] = 0x7ff; /* all known extra fields present */
        check(&g, TRUE, TRUE);
        fresh();
        entry(0, 22, 1, 0);
        entry(22, 254, 1, 0); /* validate every entry, not only the first */
        check(&g, FALSE, FALSE);
    }
    g.blocksize = 1024;
    for (unsigned ext = 0; ext != 2; ++ext) {
        g.dirextension = ext;
        fresh();
        entry(0, 20 + ext * 2, 1, 0);
        check(&g, TRUE, TRUE);
        fresh();
        entry(0, 242, 108, 0);
        check(&g, FALSE, FALSE);
        fresh();
        entry(0, 22, 4, 0); /* comment length byte lies outside the entry */
        check(&g, FALSE, FALSE);
        fresh();
        entry(0, 22, 1, 3); /* comment data runs into the next entry */
        check(&g, FALSE, FALSE);
        fresh();
        entry(0, 242, 1, 80);
        check(&g, FALSE, FALSE);
        fresh();
        entry(0, 21, 1, 0);
        check(&g, FALSE, FALSE);
        fresh();
        entry(0, 18, 0, 0);
        check(&g, FALSE, FALSE);
    }
    puts("  PASS: directory entry/string bounds and protection rewrites");
    return 0;
}
