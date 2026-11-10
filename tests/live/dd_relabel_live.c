/* LIVE @src dd_funcs.c @func dd_Relabel
 *
 * Lifts the checkout's real dd_Relabel and asserts, with a mock rootblock
 * write and allocator:
 *
 *  - the new BSTR replaces dl_Name and the old one is released. The old name
 *    comes from MakeDosEntry (AllocVec) and FreeDosEntry frees it with FreeVec,
 *    so the new name must be an AllocVec block and the old one must go through
 *    FreeVec, never AllocMem/FreeMem;
 *  - a failing rootblock write leaves dl_Name alone, reports the error and
 *    frees the block it allocated;
 *  - the label rules: 1..30 characters, no ':' or '/', never an embedded NUL,
 *    and an empty name is refused before anything is allocated or written;
 *  - a volume that fails CheckVolume allocates and writes nothing.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef intptr_t SIPTR; typedef uint32_t ULONG; typedef uint8_t UBYTE; typedef uint16_t UWORD;
typedef int BOOL; typedef intptr_t BPTR; typedef UBYTE *DSTR; typedef char *STRPTR;
#define DOSTRUE (-1)
#define DOSFALSE 0
#define DNSIZE 32
#define MULTIUSER 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR  2
#define ERROR_NO_FREE_STORE 103
#define ERROR_REQUIRED_ARG_MISSING 116
#define ERROR_INVALID_COMPONENT_NAME 210
#define ERROR_DEVICE_NOT_MOUNTED 218
#define ERROR_DISK_WRITE_PROTECTED 214

#define BADDR(x) ((void *)((intptr_t)(x) << 2))
#define MKBADDR(x) ((BPTR)((intptr_t)(x) >> 2))
#define BARG1(p) BADDR((p)->dp_Arg1)

struct DosPacket { SIPTR dp_Arg1; SIPTR dp_Res2; };
struct DeviceList { BPTR dl_Name; };
struct volumedata { struct DeviceList *devlist; };
typedef struct globaldata { struct volumedata *currentvolume; } globaldata;

static int vol_ok = 1, rename_calls, vec_alloc, mem_alloc, vec_free, mem_free, old_via_vec, old_via_mem;
static ULONG rename_err;
static char renamed_to[DNSIZE];
static void *oldlabel_ptr;

static BOOL CheckVolume(struct volumedata *v, BOOL write, SIPTR *err, globaldata *g)
{
	(void)v; (void)write; (void)g;
	if (vol_ok) return 1;
	*err = ERROR_DEVICE_NOT_MOUNTED;
	return 0;
}
static ULONG RenameDisk(UBYTE *label, globaldata *g)
{
	(void)g;
	rename_calls++;
	strncpy(renamed_to, (char *)label, sizeof(renamed_to) - 1);
	return rename_err;
}
static void *AllocVec(ULONG size, ULONG flags) { (void)flags; vec_alloc++; return calloc(1, size); }
static void  FreeVec(void *p) { vec_free++; if (p == oldlabel_ptr) old_via_vec++; }
static void *AllocMem(ULONG size, ULONG flags) { (void)flags; mem_alloc++; return calloc(1, size); }
static void  FreeMem(void *p, ULONG size) { (void)size; mem_free++; if (p == oldlabel_ptr) old_via_mem++; }
static void  CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }
static UBYTE *BCPLtoCString(STRPTR dest, ULONG capacity, DSTR src)
{
	UWORD len = *src, i;
	if (len >= capacity) { *dest = 0; return NULL; }
	for (i = 0; i < len; i++) dest[i] = src[i + 1];
	dest[len] = 0;
	return (UBYTE *)dest;
}

#include "gen/dd_Relabel.c"                  /* the REAL function from ../../dd_funcs.c */

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static UBYTE bstr[64] __attribute__((aligned(16)));

/* the volume starts out labelled "Old", allocated like MakeDosEntry does */
static SIPTR relabel(const char *name, int len, SIPTR *err, BPTR *dl_name_after, BPTR *dl_name_before)
{
	struct DeviceList dl;
	struct volumedata vol = { &dl };
	globaldata g = { &vol };
	UBYTE *old = calloc(1, 8);
	struct DosPacket pkt;
	SIPTR res;

	old[0] = 3; memcpy(old + 1, "Old", 3);
	oldlabel_ptr = old;
	dl.dl_Name = MKBADDR(old);
	*dl_name_before = dl.dl_Name;

	if (name) { bstr[0] = (UBYTE)len; memcpy(bstr + 1, name, len); pkt.dp_Arg1 = MKBADDR(bstr); }
	else pkt.dp_Arg1 = 0;
	pkt.dp_Res2 = 0;
	rename_calls = vec_alloc = mem_alloc = vec_free = mem_free = old_via_vec = old_via_mem = 0;
	renamed_to[0] = 0;

	res = dd_Relabel(&pkt, &g);
	*err = pkt.dp_Res2;
	*dl_name_after = dl.dl_Name;
	return res;
}

static void rejected(const char *what, const char *name, int len, SIPTR want_err)
{
	SIPTR err, res; BPTR after, before;
	res = relabel(name, len, &err, &after, &before);
	printf("  %-30s res=%d err=%d renames=%d allocs=%d\n", what, (int)res, (int)err, rename_calls, vec_alloc + mem_alloc);
	check(res == DOSFALSE && err == want_err, what);
	check(rename_calls == 0 && vec_alloc + mem_alloc == 0, "a refused name must not be written or allocated");
	check(after == before, "a refused name must leave dl_Name alone");
}

int main(void)
{
	SIPTR err, res; BPTR after, before;
	char longname[40];

	setvbuf(stdout, NULL, _IONBF, 0);

	res = relabel("Work", 4, &err, &after, &before);
	printf("  valid name:                    res=%d allocvec=%d allocmem=%d freevec(old)=%d freemem(old)=%d\n",
		(int)res, vec_alloc, mem_alloc, old_via_vec, old_via_mem);
	check(res == DOSTRUE, "valid relabel must succeed");
	check(strcmp(renamed_to, "Work") == 0, "the rootblock must be written with the new name");
	{
		UBYTE *n = BADDR(after);
		check(after != before && n[0] == 4 && memcmp(n + 1, "Work", 4) == 0, "dl_Name must hold the new BSTR");
	}
	check(vec_alloc == 1 && mem_alloc == 0, "the new name must be an AllocVec block (FreeDosEntry uses FreeVec)");
	check(old_via_vec == 1 && old_via_mem == 0 && mem_free == 0, "the old name must be released with FreeVec exactly once");

	memset(longname, 'x', sizeof(longname));
	res = relabel(longname, DNSIZE - 2, &err, &after, &before);
	printf("  longest valid name (30):       res=%d\n", (int)res);
	check(res == DOSTRUE, "a 30 character name is valid");

	rejected("name too long (31)", longname, DNSIZE - 1, ERROR_INVALID_COMPONENT_NAME);
	rejected("empty name", "", 0, ERROR_INVALID_COMPONENT_NAME);
	rejected("name with ':'", "a:b", 3, ERROR_INVALID_COMPONENT_NAME);
	rejected("name with '/'", "a/b", 3, ERROR_INVALID_COMPONENT_NAME);
	rejected("name with embedded NUL", "a\0b", 3, ERROR_INVALID_COMPONENT_NAME);

	rename_err = 29;
	res = relabel("Work", 4, &err, &after, &before);
	printf("  rootblock write fails:         res=%d err=%d allocs=%d frees=%d\n", (int)res, (int)err, vec_alloc + mem_alloc, vec_free + mem_free);
	check(res == DOSFALSE && err == 29, "a failed rootblock write must be reported");
	check(after == before && old_via_vec + old_via_mem == 0, "a failed write must leave dl_Name and the old name alone");
	check(vec_alloc + mem_alloc == 1 && vec_free + mem_free == 1 && mem_alloc == 0 && mem_free == 0,
		"a failed write must free the block it allocated with FreeVec");
	rename_err = 0;

	vol_ok = 0;
	res = relabel("Work", 4, &err, &after, &before);
	printf("  CheckVolume fails:             res=%d err=%d renames=%d allocs=%d\n", (int)res, (int)err, rename_calls, vec_alloc + mem_alloc);
	check(res == DOSFALSE && err == ERROR_DEVICE_NOT_MOUNTED, "a bad volume must be reported");
	check(rename_calls == 0 && vec_alloc + mem_alloc == 0 && after == before, "a bad volume must allocate and write nothing");
	vol_ok = 1;

	if (failures == 0) { printf("  PASS: relabel validates the name, writes it and releases the old one with FreeVec\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
