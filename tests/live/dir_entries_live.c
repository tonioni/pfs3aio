/* LIVE @src directory.c @func SearchInDir @func SearchInDeldir @func EntryFileSize @func FileSize32 @func FillInData @func ExamineAll
 *
 * Lifts the checkout's real SearchInDir, SearchInDeldir, FillInData and
 * ExamineAll.
 *
 * FillInData (one directory entry -> ExAllData):
 *  - an entry that does not hold its own name and comment (the name or the
 *    comment length byte lies beyond direntry->next, or the comment is longer
 *    than what is left of the entry) is CORRUPT: return 0 with
 *    *error = PFS_ERROR_INCONSISTENT. That differs from "does not fit in the
 *    buffer": return 0 with *error untouched. The corruption test comes first,
 *    so it holds for every ED_ type and also when there is no space at all;
 *  - valid entries with and without a comment return their (even) size, the
 *    exact fit works, one byte less does not fit.
 *
 * ExamineAll:
 *  - a corrupt entry makes the call fail with PFS_ERROR_INCONSISTENT (entries
 *    before it are still delivered) instead of DOSTRUE: the caller of ExAll
 *    otherwise asks again for the same entry for ever. The test plays that
 *    caller with a bounded loop;
 *  - a valid entry that does not fit is DOSTRUE with *error == 0, and it is
 *    delivered once the buffer is large enough.
 *
 * SearchInDir / SearchInDeldir: the name is converted with ctodstr
 * (length byte + chars + NUL) into a local array. The longest name a caller
 * can pass (255 chars, a BSTR) must fit: the lifted code is built with a
 * ctodstr that knows sizeof(destination) (the macro expands inside the real
 * function, so it sees the real array) and records an overflow instead of
 * writing past it. SearchInDeldir only has to hold the part before the
 * separator (<= 253 chars), its array is a regression guard.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef intptr_t SIPTR; typedef intptr_t IPTR;
typedef int BOOL; typedef char *STRPTR; typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define DELDIR 1
#define UCOUNT WORD						/* struct.h */
#define PATHSIZE 256					/* blocks.h */
#define FILENAMESIZE (g->fnsize)
#define FNSIZE 108
#define DELENTRIES_PER_BLOCK 31
#define MAXDELDIR 31
#define DELENTRY_SEP '@'
#define SPECIAL_DELDIR 1
#define SPECIAL_DELFILE 2
#define ENTER(x)
#define DB(x)
#define ED_NAME 1						/* dos/exall.h */
#define ED_TYPE 2
#define ED_SIZE 3
#define ED_PROTECTION 4
#define ED_DATE 5
#define ED_COMMENT 6
#define ED_OWNER 7
#define ERROR_BAD_NUMBER 115			/* dos/dos.h */
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_DISK_NOT_VALIDATED 213
#define ERROR_NO_MORE_ENTRIES 232
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED		/* struct.h */
#define ST_USERDIR 2
#define ST_FILE (-3)

#pragma pack(push, 2)
struct direntry {
	UBYTE next; BYTE type; ULONG anode; ULONG fsize;
	UWORD creationday, creationminute, creationtick;
	UBYTE protection; UBYTE nlength; UBYTE startofname; UBYTE pad;
};
#pragma pack(pop)
struct dirblock { UWORD id, not_used; ULONG datestamp; ULONG anodenr; ULONG parent; UBYTE entries[0]; };
struct cdirblock { ULONG blocknr, oldblocknr; UWORD used; UBYTE changeflag; struct dirblock blk; };
struct deldirentry {
	ULONG anodenr; ULONG fsize; UWORD creationday, creationminute, creationtick;
	UBYTE filename[18];
};
struct deldirblock { UWORD id, not_used; ULONG datestamp, seqnr; struct deldirentry entries[0]; };
struct cdeldirblock { UWORD used; UBYTE changeflag; struct deldirblock blk; };
struct extrafields { ULONG link; UWORD uid, gid; ULONG prot; ULONG virtualsize, rollpointer; };
struct canode { ULONG clustersize, blocknr, next, nr; };
struct fileinfo { struct direntry *direntry; struct cdirblock *dirblock; };
struct deldirinfo { ULONG special; void *volume; };
struct delfileinfo { ULONG special; ULONG slotnr; };
union objectinfo { struct fileinfo file; struct deldirinfo deldir; struct delfileinfo delfile; };
union listtype { struct { unsigned pad:11, dir:1, type:2, access:2; } flags; UWORD value; };
typedef union listtype listtype;
typedef struct listentry { listtype type; union objectinfo info; } listentry_t;
typedef struct lockentry {
	listentry_t le;
	struct fileinfo nextentry;
	ULONG nextdirblocknr;
	SIPTR nexterror;
} lockentry_t;
struct crootblockextension { struct { UWORD deldirsize; } blk; };
struct volumedata { struct crootblockextension *rblkextension; };
typedef struct globaldata {
	struct volumedata *currentvolume; int fnsize; UBYTE dirextension; UWORD locknr;
} globaldata;

struct ExAllData {
	struct ExAllData *ed_Next; STRPTR ed_Name; LONG ed_Type; ULONG ed_Size; ULONG ed_Prot;
	ULONG ed_Days, ed_Mins, ed_Ticks; STRPTR ed_Comment; UWORD ed_OwnerUID, ed_OwnerGID;
};
struct Hook;
struct ExAllControl { ULONG eac_Entries; IPTR eac_LastKey; STRPTR eac_MatchString; struct Hook *eac_MatchFunc; };

#define DIRENTRYNAME(de) ((UBYTE *)(&(de->nlength)))
#define NEXTENTRY(de) ((struct direntry *)((UBYTE *)(de) + (de)->next))
#define LOCK(blk) ((blk)->used = g->locknr)
#define IsDelDir(oi) ((oi).deldir.special == SPECIAL_DELDIR)

/* assroutines.c: the real conversions, copied */
static void ctodstr_real(const unsigned char *a0, unsigned char *a1)
{
	unsigned char *lenp = a1++;
	unsigned int len = 0;
	while ((*a1++ = *a0++))
		len++;
	*lenp = len;
}
static void intltoupper(unsigned char *a0)
{
	unsigned char len = *a0++;
	while (len--) {
		switch (*a0) {
			case 0x61 ... 0x7a:
			case 0xe0 ... 0xf6:
			case 0xf8 ... 0xfe:
				*a0 -= 0x20;
			default:
				a0++;
				break;
		}
	}
}
static int intlcmp(const unsigned char *a0, const unsigned char *a1)
{
	unsigned char len = *a0++;
	if (len != *a1++)
		return 0;
	while (len--) {
		unsigned char c = *a0++;
		unsigned char d = *a1++;
		if ((d - c) > d) return 0;
		if (c == d) continue;
		if (d - c != 0x20) return 0;
		switch (c) {
			case 0x41 ... 0x5a:
			case 0xc0 ... 0xd6:
			case 0xd8 ... 0xde:
				break;
			default:
				return 0;
		}
	}
	return 1;
}

/* ctodstr that knows the size of the destination it is given: the macro
 * expands inside the lifted functions, where sizeof(array) is the real one.
 * The conversion needs strlen + 2 bytes; if they do not fit it is recorded
 * and nothing is written past the array.
 */
static int ctod_overflow; static size_t ctod_need, ctod_cap;
static void ctodstr_checked(const unsigned char *src, unsigned char *dst, size_t cap)
{
	size_t len = strlen((const char *)src);
	size_t copy = len;
	ctod_need = len + 2; ctod_cap = cap;
	if (len + 2 > cap) {
		ctod_overflow = 1;
		copy = cap - 2;
	}
	dst[0] = (unsigned char)len;
	memcpy(dst + 1, src, copy);
	len = copy;
	dst[1 + len] = 0;
}
#define ctodstr(src, dst) ctodstr_checked((const unsigned char *)(src), (dst), sizeof(dst))

/* ---- SearchInDir / SearchInDeldir collaborators ---- */
#define NBLK 4
static struct { struct cdirblock b; UBYTE space[1200]; } dblks[NBLK];
static int ga_ok, nblocks, loadfail_at, nextfail_blocknr;
static BOOL GetAnode(struct canode *a, ULONG nr, globaldata *g)
{
	(void)g;
	a->nr = nr; a->clustersize = 0; a->next = 0;
	if (!ga_ok) { a->blocknr = ~0UL; return FALSE; }
	a->blocknr = 100;
	return TRUE;
}
static struct cdirblock *LoadDirBlock(ULONG blocknr, globaldata *g)
{
	(void)g;
	if (blocknr < 100 || blocknr >= 100 + NBLK || (int)(blocknr - 100) == loadfail_at) return NULL;
	return &dblks[blocknr - 100].b;
}
static BOOL NextBlock(struct canode *a, ULONG *offset, globaldata *g)
{
	(void)g;
	if (nextfail_blocknr) { a->blocknr = ~0UL; return FALSE; }
	if (*offset + 1 < (ULONG)nblocks) { (*offset)++; return TRUE; }
	return FALSE;
}

static int ddb_fail, delfile_valid; static struct cdeldirblock *dd_block; static ULONG dd_slotblk;
static struct { struct cdeldirblock b; struct deldirentry space[DELENTRIES_PER_BLOCK]; } ddblk;
static struct cdeldirblock *GetDeldirBlock(ULONG nr, globaldata *g)
{
	(void)g;
	dd_slotblk = nr;
	return ddb_fail ? NULL : &ddblk.b;
}
static BOOL IsDelfileValid(struct deldirentry *dde, struct cdeldirblock *b, SIPTR *error, globaldata *g)
{
	(void)dde; (void)b; (void)error; (void)g;
	return delfile_valid;
}

/* ---- FillInData / ExamineAll collaborators ---- */
static void GetExtraFields(struct direntry *de, struct extrafields *ef) { (void)de; memset(ef, 0, sizeof(*ef)); }
typedef ULONG FSIZE;

#define MAXENTS 8
static struct direntry *ents[MAXENTS]; static int nents, cur, getnext_calls, getfirst_calls, runaway;
static struct cdirblock dummyblk;
static void set_next(lockentry_t *le)
{
	if (cur < nents) { le->nextentry.direntry = ents[cur]; le->nextentry.dirblock = &dummyblk; }
	else { le->nextentry.direntry = NULL; le->nextentry.dirblock = NULL; }
}
static BOOL GetFirstEntry(lockentry_t *le, globaldata *g)
{
	(void)g;
	getfirst_calls++;
	le->nexterror = 0; le->nextdirblocknr = 0; cur = 0;
	set_next(le);
	return TRUE;
}
void GetNextEntry(lockentry_t *le, globaldata *g)
{
	(void)g;
	if (++getnext_calls > 1000) runaway = 1;
	if (cur < nents) cur++;
	set_next(le);
}
static struct deldirentry *GetDeldirEntry(IPTR *nr, SIPTR *error, globaldata *g) { (void)nr; (void)error; (void)g; return NULL; }
static ULONG FillInDDEData(struct ExAllData *b, LONG t, struct deldirentry *d, ULONG n, ULONG s, globaldata *g)
{
	(void)b; (void)t; (void)d; (void)n; (void)s; (void)g;
	return 0;
}
static BOOL CallHookPkt(struct Hook *h, void *o, void *m) { (void)h; (void)o; (void)m; return TRUE; }
static BOOL MatchPatternNoCase(STRPTR p, STRPTR s) { (void)p; (void)s; return TRUE; }

#include "gen/SearchInDir.c"			/* the REAL functions from ../../directory.c */
#include "gen/SearchInDeldir.c"
#include "gen/EntryFileSize.c"
#include "gen/FileSize32.c"
#include "gen/FillInData.c"
#include "gen/ExamineAll.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static struct volumedata vol;
static struct crootblockextension rext;
static globaldata gd = { &vol, FNSIZE, 0, 7 };
#define SENTINEL ((SIPTR)0x5555)

/* an entry as it lies in a directory block: the name at startofname, then the
 * comment length byte and the comment; next is the natural (even) length
 * unless the caller overrides it
 */
static void mk(UBYTE *e, const char *name, const char *comment, int next)
{
	size_t nl = strlen(name), cl = comment ? strlen(comment) : 0;
	struct direntry *de = (struct direntry *)e;

	memset(e, 0, 256);
	de->type = ST_FILE; de->anode = 99; de->fsize = 1234; de->protection = 3;
	de->creationday = 11; de->creationminute = 12; de->creationtick = 13;
	de->nlength = (UBYTE)nl;
	memcpy(e + offsetof(struct direntry, startofname), name, nl);
	e[offsetof(struct direntry, startofname) + nl] = (UBYTE)cl;
	if (cl) memcpy(e + offsetof(struct direntry, startofname) + nl + 1, comment, cl);
	de->next = next >= 0 ? (UBYTE)next : (UBYTE)((20 + nl + cl) & 0xfffe);
}
static size_t eco(const char *name) { return offsetof(struct direntry, startofname) + strlen(name); }

/* ExAllData size FillInData must report for the type */
static size_t expect_size(LONG type, const char *name, const char *comment)
{
	size_t s;
	switch (type) {
		case ED_NAME: s = offsetof(struct ExAllData, ed_Type); break;
		case ED_TYPE: s = offsetof(struct ExAllData, ed_Size); break;
		case ED_SIZE: s = offsetof(struct ExAllData, ed_Prot); break;
		case ED_PROTECTION: s = offsetof(struct ExAllData, ed_Days); break;
		case ED_DATE: s = offsetof(struct ExAllData, ed_Comment); break;
		case ED_COMMENT: s = offsetof(struct ExAllData, ed_OwnerUID); break;
		default: s = sizeof(struct ExAllData); break;
	}
	s += strlen(name) + 1;
	if (type >= ED_COMMENT) s += (comment ? strlen(comment) : 0) + 1;
	return (s + 1) & 0xfffe;
}

typedef union { struct ExAllData d; UBYTE raw[4096]; } outbuf_t;
static outbuf_t out;

static void test_fillindata(void)
{
	static UBYTE e[256];
	SIPTR error;
	ULONG r, want;
	LONG type;

	/* valid entries: size is returned, *error stays untouched, data is copied */
	for (type = ED_NAME; type <= ED_OWNER; type++) {
		mk(e, "src", NULL, -1);
		memset(&out, 0xAA, sizeof(out)); error = SENTINEL;
		want = (ULONG)expect_size(type, "src", NULL);
		r = FillInData(&out.d, type, (struct direntry *)e, sizeof(out), &error, &gd);
		CHECK(r == want, "type %d valid, no comment: size %u want %u", (int)type, r, want);
		CHECK(error == SENTINEL, "type %d valid: *error touched (%ld)", (int)type, (long)error);
		CHECK(r && !strcmp(out.d.ed_Name, "src"), "type %d valid: name copied", (int)type);

		mk(e, "src", "a comment", -1);
		memset(&out, 0xAA, sizeof(out)); error = SENTINEL;
		want = (ULONG)expect_size(type, "src", "a comment");
		r = FillInData(&out.d, type, (struct direntry *)e, sizeof(out), &error, &gd);
		CHECK(r == want, "type %d valid, comment: size %u want %u", (int)type, r, want);
		CHECK(error == SENTINEL, "type %d valid with comment: *error touched", (int)type);
		if (type >= ED_COMMENT)
			CHECK(r && !strcmp(out.d.ed_Comment, "a comment"), "type %d: comment copied", (int)type);
	}

	/* the largest ordinary entry: 107 char name, 79 char comment */
	{
		char nm[108], cm[80];
		memset(nm, 'n', 107); nm[107] = 0; memset(cm, 'c', 79); cm[79] = 0;
		mk(e, nm, cm, -1);
		memset(&out, 0xAA, sizeof(out)); error = SENTINEL;
		r = FillInData(&out.d, ED_OWNER, (struct direntry *)e, sizeof(out), &error, &gd);
		CHECK(r == expect_size(ED_OWNER, nm, cm) && error == SENTINEL, "largest entry accepted (r=%u)", r);
		CHECK(r && !strcmp(out.d.ed_Name, nm) && !strcmp(out.d.ed_Comment, cm), "largest entry copied");
	}

	/* does not fit: 0, *error untouched, the buffer too; the exact fit works */
	for (type = ED_NAME; type <= ED_OWNER; type++) {
		mk(e, "src", "cmt", -1);
		want = (ULONG)expect_size(type, "src", "cmt");

		memset(&out, 0xAA, sizeof(out)); error = SENTINEL;
		r = FillInData(&out.d, type, (struct direntry *)e, want - 1, &error, &gd);
		CHECK(r == 0, "type %d: one byte short must not fit (r=%u)", (int)type, r);
		CHECK(error == SENTINEL, "type %d: not fitting is not an error (*error=%ld)", (int)type, (long)error);
		{
			size_t i; int dirty = 0;
			for (i = 0; i < 64; i++) dirty |= out.raw[i] != 0xAA;
			CHECK(!dirty, "type %d: buffer written although nothing fit", (int)type);
		}

		memset(&out, 0xAA, sizeof(out)); error = SENTINEL;
		r = FillInData(&out.d, type, (struct direntry *)e, want, &error, &gd);
		CHECK(r == want && error == SENTINEL, "type %d: exact fit (r=%u want %u)", (int)type, r, want);

		error = SENTINEL;
		r = FillInData(&out.d, type, (struct direntry *)e, 0, &error, &gd);
		CHECK(r == 0 && error == SENTINEL, "type %d: no space at all is not an error", (int)type);
	}

	/* corrupt: name beyond the entry; boundary twin (next just large enough) is valid */
	for (type = ED_NAME; type <= ED_OWNER; type++) {
		mk(e, "src", NULL, 20);					/* comment length byte at 21 >= next */
		error = SENTINEL;
		r = FillInData(&out.d, type, (struct direntry *)e, sizeof(out), &error, &gd);
		CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT,
			"type %d: name beyond next: r=%u error=%ld", (int)type, r, (long)error);

		error = SENTINEL;
		r = FillInData(&out.d, type, (struct direntry *)e, 0, &error, &gd);
		CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT,
			"type %d: corrupt entry and no space: corruption must win (error=%ld)", (int)type, (long)error);
	}
	mk(e, "src", NULL, 22);
	error = SENTINEL;
	r = FillInData(&out.d, ED_NAME, (struct direntry *)e, sizeof(out), &error, &gd);
	CHECK(r != 0 && error == SENTINEL, "next just covers name + comment byte: valid (r=%u)", r);

	mk(e, "src", NULL, 0);
	error = SENTINEL;
	r = FillInData(&out.d, ED_NAME, (struct direntry *)e, sizeof(out), &error, &gd);
	CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT, "next == 0: error (r=%u error=%ld)", r, (long)error);

	/* corrupt: comment longer than what is left of the entry */
	mk(e, "src", "x", 22);						/* room for 0 comment chars */
	error = SENTINEL;
	r = FillInData(&out.d, ED_COMMENT, (struct direntry *)e, sizeof(out), &error, &gd);
	CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT,
		"comment 1 char, no room: r=%u error=%ld", r, (long)error);

	mk(e, "src", "xy", 24);						/* room for 2 */
	error = SENTINEL;
	r = FillInData(&out.d, ED_COMMENT, (struct direntry *)e, sizeof(out), &error, &gd);
	CHECK(r != 0 && error == SENTINEL && !strcmp(out.d.ed_Comment, "xy"), "comment exactly fills entry: valid (r=%u)", r);

	mk(e, "src", "xyz", 24);					/* one too many */
	error = SENTINEL;
	r = FillInData(&out.d, ED_COMMENT, (struct direntry *)e, sizeof(out), &error, &gd);
	CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT,
		"comment one char too long: r=%u error=%ld", r, (long)error);

	mk(e, "src", NULL, 30);						/* garbage comment length byte */
	e[eco("src")] = 0xFF;
	error = SENTINEL;
	r = FillInData(&out.d, ED_NAME, (struct direntry *)e, sizeof(out), &error, &gd);
	CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT, "comment length 255: r=%u error=%ld", r, (long)error);

	/* the same two failures are not mixed up: a corrupt entry is never "does not fit" */
	mk(e, "src", "xyz", 24);
	error = SENTINEL;
	r = FillInData(&out.d, ED_COMMENT, (struct direntry *)e, 1, &error, &gd);
	CHECK(r == 0 && error == PFS_ERROR_INCONSISTENT, "corrupt entry, 1 byte of space: still corrupt");
}

static UBYTE ent[MAXENTS][256];
static struct ExAllControl ctrl;
static lockentry_t dir;
static UBYTE xbuf[4096];

static void setdir(int n, const char *const *names, const char *const *comments)
{
	int i;
	nents = n; cur = 0; getnext_calls = getfirst_calls = runaway = 0;
	for (i = 0; i < n; i++) { mk(ent[i], names[i], comments ? comments[i] : NULL, -1); ents[i] = (struct direntry *)ent[i]; }
	memset(&dir, 0, sizeof(dir));
	dir.le.type.flags.dir = 1;
	memset(&ctrl, 0, sizeof(ctrl));
}

static void test_examineall(void)
{
	static const char *const three[] = { "one", "two", "three" };
	SIPTR error;
	BOOL r;
	int i, n;

	/* all valid: every entry delivered and linked, then end of directory */
	setdir(3, three, NULL);
	error = SENTINEL;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_NAME, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == ERROR_NO_MORE_ENTRIES, "valid dir: NO_MORE_ENTRIES (r=%d error=%ld)", r, (long)error);
	CHECK(ctrl.eac_Entries == 3, "valid dir: 3 entries (%u)", ctrl.eac_Entries);
	{
		struct ExAllData *d = (struct ExAllData *)xbuf;
		int k = 0;
		for (; d && k < 5; d = d->ed_Next) { CHECK(k >= 3 || !strcmp(d->ed_Name, three[k]), "entry %d name", k); k++; }
		CHECK(k == 3, "valid dir: chain of %d entries", k);
	}

	/* corrupt middle entry: earlier entries delivered, then the error, once */
	setdir(3, three, NULL);
	ent[1][0] = 20;								/* name "two" no longer inside the entry */
	error = SENTINEL;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_NAME, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE, "corrupt middle: must fail, not DOSTRUE (r=%d)", r);
	CHECK(error == PFS_ERROR_INCONSISTENT, "corrupt middle: error %ld", (long)error);
	CHECK(ctrl.eac_Entries == 1, "corrupt middle: entry before it delivered (%u)", ctrl.eac_Entries);
	CHECK(getnext_calls == 1, "corrupt middle: stopped at the corrupt entry (GetNextEntry x%d)", getnext_calls);

	/* the caller of ExAll: asks again as long as it gets DOSTRUE. It must end. */
	setdir(3, three, NULL);
	ent[1][0] = 20;
	n = 0;
	for (i = 0; i < 50; i++) {
		error = 0;
		r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_NAME, &ctrl, &error, &gd);
		n++;
		if (!r) break;
	}
	CHECK(!r && n <= 2 && error == PFS_ERROR_INCONSISTENT, "caller loop on a corrupt entry: ended after %d calls, r=%d error=%ld", n, r, (long)error);

	/* asking again repeats the error, no entries, no progress */
	error = 0;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_NAME, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == PFS_ERROR_INCONSISTENT && ctrl.eac_Entries == 0,
		"corrupt entry asked again: r=%d error=%ld entries=%u", r, (long)error, ctrl.eac_Entries);

	/* corrupt first entry */
	setdir(3, three, NULL);
	ent[0][0] = 0;
	error = SENTINEL;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_NAME, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == PFS_ERROR_INCONSISTENT && ctrl.eac_Entries == 0,
		"corrupt first entry: r=%d error=%ld entries=%u", r, (long)error, ctrl.eac_Entries);

	/* corrupt comment of the last entry */
	setdir(3, three, NULL);
	ent[2][eco("three")] = 9;
	error = SENTINEL;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_COMMENT, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == PFS_ERROR_INCONSISTENT && ctrl.eac_Entries == 2,
		"corrupt last comment: r=%d error=%ld entries=%u", r, (long)error, ctrl.eac_Entries);

	/* corrupt entry and a buffer too small for anything: still the error */
	setdir(3, three, NULL);
	ent[0][0] = 0;
	error = SENTINEL;
	r = ExamineAll(&dir, xbuf, 0, ED_NAME, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == PFS_ERROR_INCONSISTENT, "corrupt entry, empty buffer: r=%d error=%ld", r, (long)error);

	/* valid entry that does not fit: DOSTRUE, no error; it comes once the buffer is large enough */
	{
		static const char *const names[] = { "a", "a-rather-long-file-name-that-needs-space", "z" };
		size_t s0 = expect_size(ED_NAME, names[0], NULL);
		size_t s1 = expect_size(ED_NAME, names[1], NULL);
		size_t s2 = expect_size(ED_NAME, names[2], NULL);

		setdir(3, names, NULL);
		error = SENTINEL;
		r = ExamineAll(&dir, xbuf, (ULONG)(s0 + s1 - 1), ED_NAME, &ctrl, &error, &gd);
		CHECK(r == DOSTRUE && error == 0 && ctrl.eac_Entries == 1,
			"first call, second entry does not fit: r=%d error=%ld entries=%u", r, (long)error, ctrl.eac_Entries);

		error = SENTINEL;
		r = ExamineAll(&dir, xbuf, (ULONG)(s1 - 1), ED_NAME, &ctrl, &error, &gd);
		CHECK(r == DOSTRUE && error == 0 && ctrl.eac_Entries == 0,
			"still too small: DOSTRUE, 0 entries, no error (r=%d error=%ld entries=%u)", r, (long)error, ctrl.eac_Entries);

		error = SENTINEL;
		r = ExamineAll(&dir, xbuf, (ULONG)(s1 + s2), ED_NAME, &ctrl, &error, &gd);
		CHECK(r == DOSFALSE && error == ERROR_NO_MORE_ENTRIES && ctrl.eac_Entries == 2,
			"large enough: both delivered then NO_MORE_ENTRIES (r=%d error=%ld entries=%u)", r, (long)error, ctrl.eac_Entries);
		CHECK(!strcmp(((struct ExAllData *)xbuf)->ed_Name, names[1]), "the entry that did not fit is delivered");
	}

	/* argument checks stay */
	setdir(1, three, NULL);
	dir.le.type.flags.dir = 0;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_NAME, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == ERROR_OBJECT_WRONG_TYPE, "not a directory");
	dir.le.type.flags.dir = 1;
	r = ExamineAll(&dir, xbuf, sizeof(xbuf), ED_OWNER + 1, &ctrl, &error, &gd);
	CHECK(r == DOSFALSE && error == ERROR_BAD_NUMBER, "bad type");
}

/* ---- SearchInDir ---- */
static void put_entry(int blk, int idx_off, const char *name)
{
	UBYTE *base = dblks[blk].b.blk.entries + idx_off;
	size_t nl = strlen(name);
	struct direntry *de = (struct direntry *)base;

	memset(base, 0, sizeof(struct direntry) + nl + 4);
	de->type = ST_FILE; de->anode = 50 + idx_off;
	de->nlength = (UBYTE)nl;
	memcpy(base + offsetof(struct direntry, startofname), name, nl);
	base[offsetof(struct direntry, startofname) + nl] = 0;
	de->next = (UBYTE)((20 + nl) & 0xfffe);
}
static int next_off(const char *name) { return (int)((20 + strlen(name)) & 0xfffe); }

static void sid_reset(void)
{
	memset(dblks, 0, sizeof(dblks));
	ga_ok = 1; nblocks = 1; loadfail_at = -1; nextfail_blocknr = 0;
	ctod_overflow = 0; ctod_need = ctod_cap = 0;
	gd.locknr = 7;
}

static void test_searchindir(void)
{
	union objectinfo info;
	char longname[300], ucname[120];
	LONG r;
	int i;

	/* ordinary lookups: case-insensitive hit, miss, empty name, 2nd block */
	sid_reset();
	put_entry(0, 0, "Alpha");
	put_entry(0, next_off("Alpha"), "beta");
	memset(&info, 0, sizeof(info));
	r = SearchInDir(5, "BETA", &info, &gd);
	CHECK(r == 1 && info.file.direntry && info.file.dirblock == &dblks[0].b, "hit BETA (r=%ld)", (long)r);
	CHECK(info.file.direntry && info.file.direntry->anode == 50 + (UBYTE)next_off("Alpha"), "hit: the right entry");
	CHECK(dblks[0].b.used == 7, "hit: block locked");
	CHECK(!ctod_overflow, "ordinary name: no overflow");
	r = SearchInDir(5, "gamma", &info, &gd);
	CHECK(r == 0, "miss -> 0 (r=%ld)", (long)r);

	sid_reset();
	put_entry(0, 0, "");
	r = SearchInDir(5, "", &info, &gd);
	CHECK(r == 1, "empty name finds the empty entry (r=%ld)", (long)r);

	sid_reset(); nblocks = 2;
	put_entry(0, 0, "first");
	put_entry(1, 0, "second");
	memset(&info, 0, sizeof(info));
	r = SearchInDir(5, "SECOND", &info, &gd);
	CHECK(r == 1 && info.file.dirblock == &dblks[1].b, "hit in the second block (r=%ld)", (long)r);

	/* failures: dir anode unreadable, block unreadable, next anode unreadable */
	sid_reset(); ga_ok = 0;
	CHECK(SearchInDir(5, "x", &info, &gd) == -1, "GetAnode failed -> -1");
	sid_reset(); loadfail_at = 0;
	CHECK(SearchInDir(5, "x", &info, &gd) == -1, "LoadDirBlock failed -> -1");
	sid_reset(); put_entry(0, 0, "a"); nextfail_blocknr = 1;
	CHECK(SearchInDir(5, "x", &info, &gd) == -1, "NextBlock failed on read -> -1");

	/* the longest name a caller can pass: 255 chars (BCPLtoCString into 256 bytes) */
	for (i = 254; i <= 255; i++) {
		sid_reset();
		memset(longname, 'a', i); longname[i] = 0;
		memset(ucname, 'A', 107); ucname[107] = 0;			/* names are cut to FILENAMESIZE-1 */
		put_entry(0, 0, ucname);
		memset(&info, 0, sizeof(info));
		r = SearchInDir(5, longname, &info, &gd);
		CHECK(!ctod_overflow, "%d char name: ctodstr overflows intl_name (needs %zu bytes, array has %zu)",
			i, ctod_need, ctod_cap);
		CHECK(ctod_need == (size_t)i + 2, "%d char name: ctodstr needed %zu bytes", i, ctod_need);
		CHECK(r == 1, "%d char name cut to 107 chars finds the 107 char entry (r=%ld)", i, (long)r);
	}
}

/* ---- SearchInDeldir ---- */
static void test_searchindeldir(void)
{
	union objectinfo info;
	char name[300];
	struct deldirentry *dde, *r;
	SIPTR error;

	memset(&ddblk, 0, sizeof(ddblk));
	vol.rblkextension = &rext; rext.blk.deldirsize = 1;
	gd.currentvolume = &vol;
	ctod_overflow = 0; ddb_fail = 0; delfile_valid = 1;
	gd.locknr = 9;

	dde = &ddblk.b.blk.entries[3];
	dde->anodenr = 77; dde->filename[0] = 3; memcpy(&dde->filename[1], "FOO", 3);

	strcpy(name, "foo@3");
	memset(&info, 0, sizeof(info)); error = SENTINEL;
	r = SearchInDeldir(name, &info, &error, &gd);
	CHECK(r == dde, "foo@3 found");
	CHECK(info.delfile.special == SPECIAL_DELFILE && info.delfile.slotnr == 3, "foo@3: delfile info");
	CHECK(!strcmp(name, "foo@3"), "name restored after the search (%s)", name);
	CHECK(ddblk.b.used == 9, "delfile block locked");

	strcpy(name, "bar@3");
	r = SearchInDeldir(name, &info, &error, &gd);
	CHECK(r == NULL, "bar@3: other name not found");
	strcpy(name, "foo@40");
	r = SearchInDeldir(name, &info, &error, &gd);
	CHECK(r == NULL, "foo@40: slot beyond deldir");
	strcpy(name, "foo");
	CHECK(SearchInDeldir(name, &info, &error, &gd) == NULL, "no separator");
	strcpy(name, "foo@3x");
	CHECK(SearchInDeldir(name, &info, &error, &gd) == NULL, "non digit");
	ddb_fail = 1; error = SENTINEL; strcpy(name, "foo@3");
	r = SearchInDeldir(name, &info, &error, &gd);
	CHECK(r == NULL && error == PFS_ERROR_INCONSISTENT, "deldir block unreadable: error");
	ddb_fail = 0;

	/* longest possible name: 255 chars = 253 + "@0"; the part before '@' needs 255 bytes */
	memset(name, 'a', 253); strcpy(name + 253, "@0");
	ctod_overflow = 0; error = SENTINEL;
	r = SearchInDeldir(name, &info, &error, &gd);
	CHECK(!ctod_overflow, "253 char deldir name: ctodstr overflows intl_name (needs %zu, array has %zu)",
		ctod_need, ctod_cap);
	CHECK(ctod_need == 255, "253 char deldir name: ctodstr needed %zu bytes", ctod_need);
	CHECK(r == NULL && error == SENTINEL, "253 char deldir name: no match, no error");
}

int main(void)
{
	test_fillindata();
	test_examineall();
	test_searchindir();
	test_searchindeldir();
	if (failures) { printf("FAIL: %d checks failed\n", failures); return 1; }
	printf("PASS: FillInData error vs does-not-fit; ExamineAll ends on a corrupt entry; SearchInDir/SearchInDeldir name buffers hold the longest name\n");
	return 0;
}
