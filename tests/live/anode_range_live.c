/* LIVE @src anodes.c @func GetAnode @func SaveAnodeInternal
 *
 * The anode number range check of GetAnode and SaveAnodeInternal (non-split
 * mode). The old test was `anodenr / anodesperblock > 0xffff`; it became
 * `(UWORD)(anodenr >> 16) >= anodesperblock`, which avoids a 32 bit divide on
 * the 68000. The rule behind both: divide() is a DIVU, so the quotient
 * anodenr / anodesperblock (= the anode block seqnr) must fit in 16 bits,
 * i.e. anodenr < anodesperblock * 65536.
 *
 * Note: the result alone cannot tell the guard from "no guard". When the
 * quotient overflows, divide() leaves d0 unchanged, so the offset
 * (d0 >> 16 == the high word >= anodesperblock) fails the later
 * `anodeoffset < anodesperblock` test anyway. What the guard really
 * guarantees is that a DIVU never sees an overflowing quotient; the mock
 * divide() counts those, and the test requires none (and no divide at all
 * for a rejected number).
 *
 * Lifts the real GetAnode and SaveAnodeInternal from anodes.c; divide() (a
 * copy of the portable one in assroutines.c), GetAnodeBlock and
 * MakeBlockDirty are mocks. Proven:
 *  - brute force: for several anodesperblock values the lifted functions
 *    accept exactly the anode numbers the old condition accepted and exactly
 *    the ones of the intended rule (mathematical, 64 bit), for every high
 *    word and several low words; the exact boundary (limit - 1 accepted,
 *    limit rejected, 0xffffffff rejected) for ALL anodesperblock 1..0xffff
 *  - an accepted number reads/writes the right slot of the right anode block
 *  - a rejected number leaves the anode block untouched and never reaches
 *    GetAnodeBlock; GetAnode fills in the documented failure values
 *  - anodesperblock == 0 is rejected in both modes
 *  - split mode ignores the limit (seqnr/offset come from the two halves)
 *  - SaveAnodeInternal's rejection still latches metadata_error for SaveAnode
 *    style callers (latch_failure) and not for TrySaveAnode style ones
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uint8_t UBYTE; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define CBLK_DIRTY_PENDING 2
#define DBERR(x)

struct anode { ULONG clustersize, blocknr, next; };
typedef struct anode anode_t;
struct canode { ULONG clustersize, blocknr, next, nr; };

/* anodenr_t overlays the anode number as on the (big endian) Amiga: seqnr is
 * the HIGH word. Swap the members on a little endian host. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef struct { UWORD offset; UWORD seqnr; } anodenr_t;
#else
typedef struct { UWORD seqnr; UWORD offset; } anodenr_t;
#endif

struct cachedblock;
struct canodeblock
{
	UBYTE changeflag;
	struct { struct anode nodes[0x10000]; } blk;
};
struct volumedata { BOOL metadata_error; };

typedef struct globaldata
{
	struct { UWORD anodesperblock; } glob_anodedata;
	UBYTE anodesplitmode;
	BOOL uip, dirty;
	struct volumedata *currentvolume;
} globaldata;
#define andata (g->glob_anodedata)

/* divide(): copy of the portable version in assroutines.c (68k DIVU: on
 * overflow d0 is returned unchanged). It counts the calls and the ones whose
 * quotient does not fit in 16 bits: that is what the range check must keep
 * away from it. */
static ULONG divcalls, overflowed;
static ULONG divide(ULONG d0, UWORD d1)
{
	ULONG q = d0 / d1;
	divcalls++;
	if (q > 65535UL) { overflowed++; return d0; }
	return ((d0 % d1) << 16) | q;
}

static struct canodeblock BLK;
static ULONG blockcalls, lastseq, dirtycalls;
static int block_missing;		/* GetAnodeBlock returns NULL */
static int make_pending;		/* MakeBlockDirty fails: CBLK_DIRTY_PENDING */

static struct canodeblock *GetAnodeBlockFn(UWORD seqnr, globaldata *g)
{
	blockcalls++;
	lastseq = seqnr;
	return block_missing ? NULL : &BLK;
}
#define GetAnodeBlock(a, b) GetAnodeBlockFn(a, b)

static BOOL MakeBlockDirty(struct cachedblock *blk, globaldata *g)
{
	struct canodeblock *b = (struct canodeblock *)blk;
	dirtycalls++;
	g->dirty = TRUE;
	if (make_pending)
		b->changeflag = CBLK_DIRTY_PENDING;
	else
		b->changeflag = 1;
	return TRUE;
}

#include "gen/GetAnode.c"                  /* the REAL functions from ../../anodes.c */
#include "gen/SaveAnodeInternal.c"

static int fails;
static unsigned long nchecks;
#define CHECK(cond, ...) do { nchecks++; if (!(cond)) { if (fails++ < 25) { printf("  FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

static globaldata G;
static struct volumedata VOL;

static void init_blocks(void)
{
	ULONG k;
	for (k = 0; k < 0x10000; k++)
	{
		BLK.blk.nodes[k].clustersize = k + 1;
		BLK.blk.nodes[k].blocknr = 0x1000 + k;
		BLK.blk.nodes[k].next = k * 3;
	}
	BLK.changeflag = 0;
}

static void restore_slot(ULONG k)
{
	BLK.blk.nodes[k].clustersize = k + 1;
	BLK.blk.nodes[k].blocknr = 0x1000 + k;
	BLK.blk.nodes[k].next = k * 3;
	BLK.changeflag = 0;
}

static void setup(UWORD apb, int split)
{
	memset(&G, 0, sizeof G);
	G.glob_anodedata.anodesperblock = apb;
	G.anodesplitmode = split;
	G.currentvolume = &VOL;
	VOL.metadata_error = FALSE;
	blockcalls = dirtycalls = divcalls = overflowed = 0;
	block_missing = make_pending = 0;
}

/* intended rule (non-split): the block seqnr anodenr / apb fits in 16 bits */
static int intended(ULONG anodenr, UWORD apb)
{
	return apb && (uint64_t)anodenr < ((uint64_t)apb << 16);
}
/* the condition of origin/master */
static int old_accepts(ULONG anodenr, UWORD apb)
{
	return !(!apb || anodenr / apb > 0xffffUL);
}

static ULONG oldcount, intcount;

/* GetAnode vs SaveAnodeInternal(latch_failure=0) at one anode number */
static void probe(ULONG anodenr, UWORD apb, int deep)
{
	struct canode a, s;
	BOOL got, saved;
	ULONG calls, seq, off;
	int want = intended(anodenr, apb);

	setup(apb, 0);
	a.nr = 0x77; a.clustersize = a.blocknr = a.next = 0x77;
	got = GetAnode(&a, anodenr, &G);
	CHECK(got == want, "GetAnode(0x%08x) apb=%u: %s, intended rule says %s", (unsigned)anodenr, apb, got ? "accepted" : "rejected", want ? "accept" : "reject");
	CHECK(got == old_accepts(anodenr, apb), "GetAnode(0x%08x) apb=%u differs from the old condition", (unsigned)anodenr, apb);
	CHECK(a.nr == anodenr, "GetAnode(0x%08x): anode->nr not set", (unsigned)anodenr);
	CHECK(overflowed == 0 && (want || divcalls == 0), "GetAnode(0x%08x) apb=%u: rejected number reached the divide (%u calls, %u overflowing)", (unsigned)anodenr, apb, (unsigned)divcalls, (unsigned)overflowed);
	calls = blockcalls;
	if (want)
	{
		seq = anodenr / apb; off = anodenr % apb;
		CHECK(calls == 1 && lastseq == seq, "GetAnode(0x%08x) apb=%u: block seqnr %u, expected %u", (unsigned)anodenr, apb, (unsigned)lastseq, (unsigned)seq);
		CHECK(a.clustersize == off + 1 && a.blocknr == 0x1000 + off && a.next == off * 3,
			"GetAnode(0x%08x) apb=%u: wrong slot copied (offset %u)", (unsigned)anodenr, apb, (unsigned)off);
	}
	else
	{
		CHECK(calls == 0, "GetAnode(0x%08x) apb=%u: rejected number reached GetAnodeBlock", (unsigned)anodenr, apb);
		CHECK(a.clustersize == 0 && a.next == 0 && a.blocknr == 0xFFFFFFFFUL, "GetAnode(0x%08x) apb=%u: failure values not set", (unsigned)anodenr, apb);
	}
	if (intended(anodenr, apb) != old_accepts(anodenr, apb))
		oldcount++;
	intcount += want;

	/* SaveAnodeInternal */
	if (!deep)
		return;
	setup(apb, 0);
	s.clustersize = 0xA0A0A0A0; s.blocknr = 0xB0B0B0B0; s.next = 0xC0C0C0C0; s.nr = 0;
	saved = SaveAnodeInternal(&s, anodenr, &G, FALSE);
	CHECK(saved == want, "SaveAnodeInternal(0x%08x) apb=%u: %s, intended rule says %s", (unsigned)anodenr, apb, saved ? "accepted" : "rejected", want ? "accept" : "reject");
	CHECK(saved == old_accepts(anodenr, apb), "SaveAnodeInternal(0x%08x) apb=%u differs from the old condition", (unsigned)anodenr, apb);
	CHECK(overflowed == 0 && (want || divcalls == 0), "SaveAnodeInternal(0x%08x) apb=%u: rejected number reached the divide (%u calls, %u overflowing)", (unsigned)anodenr, apb, (unsigned)divcalls, (unsigned)overflowed);
	if (want)
	{
		off = anodenr % apb;
		CHECK(lastseq == anodenr / apb && blockcalls == 1 && dirtycalls == 1, "SaveAnodeInternal(0x%08x) apb=%u: block %u calls %u/%u", (unsigned)anodenr, apb, (unsigned)lastseq, (unsigned)blockcalls, (unsigned)dirtycalls);
		CHECK(BLK.blk.nodes[off].clustersize == 0xA0A0A0A0 && BLK.blk.nodes[off].blocknr == 0xB0B0B0B0 && BLK.blk.nodes[off].next == 0xC0C0C0C0,
			"SaveAnodeInternal(0x%08x) apb=%u: slot %u not written", (unsigned)anodenr, apb, (unsigned)off);
		CHECK(s.nr == anodenr, "SaveAnodeInternal: anode->nr not set");
		restore_slot(off);
	}
	else
	{
		CHECK(blockcalls == 0 && dirtycalls == 0, "SaveAnodeInternal(0x%08x) apb=%u: rejected number reached the block", (unsigned)anodenr, apb);
		CHECK(BLK.blk.nodes[0].clustersize == 1 && BLK.blk.nodes[1].clustersize == 2, "SaveAnodeInternal(0x%08x): block modified by rejection", (unsigned)anodenr);
	}
}

int main(void)
{
	static const UWORD apbs[] = { 1, 2, 3, 85, 170, 255, 256, 1000, 0x7FFF, 0x8000, 0xFFFF };
	static const ULONG los[] = { 0, 1, 0x8000, 0xFFFF };
	unsigned i, j;
	ULONG hi, a;
	UWORD apb;
	struct canode an;

	init_blocks();

	/* 1. brute force: every high word x some low words x several apb */
	for (i = 0; i < sizeof apbs / sizeof apbs[0]; i++)
		for (hi = 0; hi <= 0xFFFF; hi++)
			for (j = 0; j < 4; j++)
				probe((hi << 16) | los[j], apbs[i], (hi & 0x3F) == 0 || (hi >= apbs[i] - 1 && hi <= apbs[i] + 1U));
	CHECK(oldcount == 0, "%lu anode numbers where the old and the intended rule differ", (unsigned long)oldcount);
	CHECK(intcount > 0, "nothing was ever accepted");

	/* 2. the exact boundary for ALL anodesperblock */
	for (i = 1; i <= 0xFFFF; i++)
	{
		apb = (UWORD)i;
		probe(0, apb, i < 600);
		probe(((ULONG)apb << 16) - 1, apb, i < 600);		/* last accepted */
		probe((ULONG)apb << 16, apb, i < 600);				/* first rejected, only when it fits in 32 bits */
		probe(((ULONG)apb << 16) + 1, apb, i < 600);
		probe(0xFFFFFFFFUL, apb, i < 600);
		probe(0xFFFF0000UL, apb, i < 600);
		probe(0x10000UL, apb, i < 600);						/* second block: accepted only for apb >= 2 */
	}

	/* 3. explicit values for the documented examples */
	setup(1, 0);
	CHECK(GetAnode(&an, 0xFFFF, &G) && !GetAnode(&an, 0x10000, &G), "apb 1: 0xffff accepted, 0x10000 rejected");
	setup(2, 0);
	CHECK(GetAnode(&an, 0x1FFFF, &G) && !GetAnode(&an, 0x20000, &G), "apb 2: 0x1ffff accepted, 0x20000 rejected");
	setup(85, 0);
	CHECK(GetAnode(&an, 85UL * 65536 - 1, &G) && !GetAnode(&an, 85UL * 65536, &G), "apb 85 boundary");
	setup(170, 0);
	CHECK(GetAnode(&an, 170UL * 65536 - 1, &G) && !GetAnode(&an, 170UL * 65536, &G), "apb 170 boundary");
	setup(0xFFFF, 0);
	CHECK(GetAnode(&an, 0xFFFEFFFFUL, &G) && !GetAnode(&an, 0xFFFF0000UL, &G) && !GetAnode(&an, 0xFFFFFFFFUL, &G), "apb 0xffff boundary");

	/* 4. anodesperblock == 0 is rejected, in both modes, without touching the block */
	for (i = 0; i < 2; i++)
	{
		struct canode s = { 5, 6, 7, 0 };
		setup(0, i);
		CHECK(!GetAnode(&an, 0, &G) && !GetAnode(&an, 0x10000, &G), "apb 0, split %u: GetAnode accepted", i);
		CHECK(!SaveAnodeInternal(&s, 0, &G, FALSE) && !SaveAnodeInternal(&s, 1, &G, FALSE), "apb 0, split %u: SaveAnodeInternal accepted", i);
		CHECK(blockcalls == 0, "apb 0, split %u: reached GetAnodeBlock", i);
	}

	/* 5. split mode ignores the limit: seqnr = high word, offset = low word */
	{
		static const struct { ULONG nr; UWORD apb; int ok; } sp[] = {
			{ 0x00050003UL, 1, 0 },			/* offset 3 >= apb 1: rejected by the offset check */
			{ 0x00050000UL, 1, 1 },			/* seqnr 5 >= apb 1: the non-split limit would reject */
			{ 0x00FF0001UL, 2, 1 },
			{ 0xFFFF0054UL, 85, 1 },			/* seqnr 0xffff, offset 84 */
			{ 0xFFFF0055UL, 85, 0 },			/* offset 85 == apb */
			{ 0x1234AAAAUL, 0xFFFF, 1 },
		};
		for (i = 0; i < sizeof sp / sizeof sp[0]; i++)
		{
			struct canode s = { 11, 12, 13, 0 };
			setup(sp[i].apb, 1);
			init_blocks();
			CHECK(GetAnode(&an, sp[i].nr, &G) == sp[i].ok, "split GetAnode(0x%08x) apb=%u: expected %s", (unsigned)sp[i].nr, sp[i].apb, sp[i].ok ? "accept" : "reject");
			if (sp[i].ok)
				CHECK(lastseq == (sp[i].nr >> 16) && an.clustersize == (sp[i].nr & 0xFFFF) + 1, "split GetAnode(0x%08x): seqnr/offset taken from the wrong halves", (unsigned)sp[i].nr);
			setup(sp[i].apb, 1);
			CHECK(SaveAnodeInternal(&s, sp[i].nr, &G, FALSE) == sp[i].ok, "split SaveAnodeInternal(0x%08x) apb=%u: expected %s", (unsigned)sp[i].nr, sp[i].apb, sp[i].ok ? "accept" : "reject");
			if (sp[i].ok)
				CHECK(lastseq == (sp[i].nr >> 16) && BLK.blk.nodes[sp[i].nr & 0xFFFF].blocknr == 12, "split SaveAnodeInternal(0x%08x): wrong block/slot", (unsigned)sp[i].nr);
		}
	}

	/* 6. a missing anode block (GetAnodeBlock NULL) still fails inside the range */
	setup(85, 0);
	block_missing = 1;
	CHECK(!GetAnode(&an, 3, &G) && an.blocknr == 0xFFFFFFFFUL, "GetAnode must fail if there is no anode block");
	CHECK(!SaveAnodeInternal(&an, 3, &G, FALSE), "SaveAnodeInternal must fail if there is no anode block");

	/* 7. failure latching of a rejected number: only for latch_failure callers */
	{
		struct canode s = { 1, 2, 3, 0 };
		setup(85, 0);
		CHECK(!SaveAnodeInternal(&s, 85UL << 16, &G, TRUE) && VOL.metadata_error && G.dirty, "latch_failure: out of range number must latch metadata_error");
		setup(85, 0);
		CHECK(!SaveAnodeInternal(&s, 85UL << 16, &G, FALSE) && !VOL.metadata_error && !G.dirty, "TrySaveAnode style: must not latch metadata_error");
		setup(85, 0);
		CHECK(SaveAnodeInternal(&s, (85UL << 16) - 1, &G, TRUE) && !VOL.metadata_error && dirtycalls == 1, "in range number must be saved without latching");
		/* the dirty-pending failure of a TrySaveAnode must not change the slot */
		setup(85, 0);
		init_blocks();
		make_pending = 1;
		CHECK(!SaveAnodeInternal(&s, 4, &G, FALSE) && BLK.blk.nodes[4].clustersize == 5, "dirty pending: slot must be left untouched");
	}

	if (fails)
	{
		printf("  %d of %lu check(s) failed\n", fails, nchecks);
		return 1;
	}
	printf("  PASS: anode range check == intended and old rule (%lu checks, %lu accepted numbers), split mode unaffected\n", nchecks, (unsigned long)intcount);
	return 0;
}
