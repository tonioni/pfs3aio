/* LIVE @src dostohandlerinterface.c @func PacketMutatesFilesystem @func RejectMetadataMutation @func NormalCommands
 *
 * Lifts the checkout's real PacketMutatesFilesystem, RejectMetadataMutation and
 * NormalCommands (the packet dispatcher) over mock packet handlers that record
 * their calls:
 *
 *  - ACTION_SET_ROLLOVER reads its rolloverinfo from dp_Arg3 as a plain pointer,
 *    NOT from BADDR(dp_Arg2): set != 0 is a mutation, a NULL dp_Arg3 is none, and
 *    dp_Arg2 is not looked at at all (it is garbage, or points at a valid info
 *    of the opposite sense, to catch a handler that still reads it);
 *  - with a volume whose metadata_error was raised while the handler ran, the
 *    reply of ACTION_FINDUPDATE / FINDOUTPUT / CREATE_DIR stays what the handler
 *    returned (it checks the flag itself and returns a new entry or lock that
 *    would leak if the reply were failed afterwards), while every other mutating
 *    packet is failed with PFS_ERROR_INCONSISTENT (WRITE and SET_FILE_SIZE with
 *    -1, the rest with DOSFALSE);
 *  - with the flag already set, every mutating packet is failed before its
 *    handler is called; non-mutating packets and a handler-less volume are
 *    never failed.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
typedef intptr_t SIPTR; typedef int32_t LONG; typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef uint8_t UBYTE; typedef int16_t BOOL; typedef intptr_t BPTR;
#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0
#define BADDR(x) ((void *)((intptr_t)(x) << 2))
#define MKBADDR(x) ((BPTR)((intptr_t)(x) >> 2))

#define ERROR_OBJECT_IN_USE 202
#define ERROR_DISK_NOT_VALIDATED 213
#define PFS_ERROR_INCONSISTENT ERROR_DISK_NOT_VALIDATED

/* the build of the standard (not large file) handler */
#define ROLLOVER 1
#define LARGE_FILE_SIZE 0
#define EXTENDED_PACKETS_OS4 0
#define EXTENDED_PACKETS_MORPHOS 0

#define ACTION_LOCATE_OBJECT 8
#define ACTION_RENAME_DISK 9
#define ACTION_FREE_LOCK 15
#define ACTION_DELETE_OBJECT 16
#define ACTION_RENAME_OBJECT 17
#define ACTION_MORE_CACHE 18
#define ACTION_SET_PROTECT 21
#define ACTION_CREATE_DIR 22
#define ACTION_EXAMINE_OBJECT 23
#define ACTION_SET_COMMENT 28
#define ACTION_SET_DATE 34
#define ACTION_SAME_LOCK 40
#define ACTION_WRITE 'W'
#define ACTION_READ 'R'
#define ACTION_FINDUPDATE 1004
#define ACTION_FINDINPUT 1005
#define ACTION_FINDOUTPUT 1006
#define ACTION_MAKE_LINK 1021
#define ACTION_SET_FILE_SIZE 1022
#define ACTION_SET_OWNER 1036
#define ACTION_LOCK_RECORD 2008
#define ACTION_FREE_RECORD 2009
#define ACTION_ADD_NOTIFY 4097
#define ACTION_REMOVE_NOTIFY 4098
#define ACTION_SERIALIZE_DISK 4200
#define ACTION_SLEEP 2200
#define ACTION_REMOVE_DIRENTRY 2204
#define ACTION_CREATE_ROLLOVER 2205
#define ACTION_SET_ROLLOVER 2206
#define ACTION_IS_PFS2 2211
#define ACTION_ADD_IDLE_SIGNAL 2220
#define ACTION_SET_DELDIR 2221
#define ACTION_SET_FNSIZE 2222
#define ACTION_SET_LARGEFILE 2223
#define ACTION_KILL_EMPTY 3000

/* struct.h: used by ACTION_SET_ROLLOVER; 68k alignment, as the handler sees it */
#pragma pack(2)
struct rolloverinfo
{
	BOOL set;           /* 0 -> read; 1 -> write */
	ULONG realsize;
	ULONG virtualsize;
	ULONG rollpointer;
};
#pragma pack()

struct DosPacket { LONG dp_Type; SIPTR dp_Res1, dp_Res2, dp_Arg1, dp_Arg2, dp_Arg3, dp_Arg4, dp_Arg5; };
struct volumedata { BOOL metadata_error; };
typedef struct globaldata { struct volumedata *currentvolume; UWORD timeout; } globaldata;

struct functable
{
	SIPTR (*function)(struct DosPacket *, globaldata *);
	ULONG timeout;
};

/* every handler is the same recording mock */
static int calls;
static LONG last_type;
static int raise_error;          /* the handler loses a metadata save while it runs */
static SIPTR handler_res1, handler_res2;

static SIPTR handle(struct DosPacket *a, globaldata *g)
{
	calls++; last_type = a->dp_Type;
	if (raise_error && g->currentvolume) g->currentvolume->metadata_error = TRUE;
	a->dp_Res2 = handler_res2;
	return handler_res1;
}
#define HANDLER(n) static SIPTR n(struct DosPacket *a, globaldata *g) { return handle(a, g); }
HANDLER(NotKnown) HANDLER(NotYetImplemented) HANDLER(dd_WriteSFS) HANDLER(dd_SeekRead)
HANDLER(dd_AddNotify) HANDLER(dd_RemoveNotify) HANDLER(dd_KillEmpty) HANDLER(dd_RemoveDirEntry)
HANDLER(dd_Sleep) HANDLER(dd_MakeRollover) HANDLER(dd_SetRollover) HANDLER(dd_IsPFS2)
HANDLER(dd_SignalIdle) HANDLER(dd_SetDeldir) HANDLER(dd_SetFileSize)

static struct functable functiontable0[ACTION_SAME_LOCK + 1];
static struct functable functiontable1000[ACTION_SET_OWNER - 1000 + 1];

#include "gen/PacketMutatesFilesystem.c"        /* the REAL functions from ../../dostohandlerinterface.c */
#include "gen/RejectMetadataMutation.c"
#include "gen/NormalCommands.c"

static int failures;
static void check(int cond, const char *what)
{
	if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

static struct volumedata vol;
static globaldata gd;
static struct DosPacket pkt;

/* BPTRs need 4 byte aligned data */
static struct rolloverinfo ri_set __attribute__((aligned(8))) = { 1, 100, 200, 300 };
static struct rolloverinfo ri_get __attribute__((aligned(8))) = { 0, 100, 200, 300 };
#define BOGUS_BPTR 0x1234567L      /* BADDR() of it points into nowhere */

static void reset(void)
{
	int i;
	for (i = 0; i <= ACTION_SAME_LOCK; i++) functiontable0[i].function = handle;
	for (i = 0; i <= ACTION_SET_OWNER - 1000; i++) functiontable1000[i].function = handle;
	calls = 0; last_type = 0; raise_error = 0; handler_res1 = 0x4321; handler_res2 = 77;
	vol.metadata_error = FALSE;
	gd.currentvolume = &vol; gd.timeout = 0;
}

static void fill(LONG type, SIPTR a1, SIPTR a2, SIPTR a3)
{
	memset(&pkt, 0, sizeof(pkt));
	pkt.dp_Type = type; pkt.dp_Arg1 = a1; pkt.dp_Arg2 = a2; pkt.dp_Arg3 = a3;
}

static void send(LONG type, SIPTR a1, SIPTR a2, SIPTR a3)
{
	fill(type, a1, a2, a3);
	pkt.dp_Res1 = 0x5a5a; pkt.dp_Res2 = 0x7777;      /* NormalCommands must clear Res2 */
	NormalCommands(&pkt, &gd);
}

/* mutating packets with the arguments that make them mutate */
struct mpkt { const char *name; LONG type; SIPTR a1, a2, a3; };
static const struct mpkt mutating[] =
{
	{ "WRITE", ACTION_WRITE, 0, 0, 0 },
	{ "SET_FILE_SIZE", ACTION_SET_FILE_SIZE, 0, 0, 0 },
	{ "DELETE_OBJECT", ACTION_DELETE_OBJECT, 0, 0, 0 },
	{ "RENAME_OBJECT", ACTION_RENAME_OBJECT, 0, 0, 0 },
	{ "RENAME_DISK", ACTION_RENAME_DISK, 0, 0, 0 },
	{ "SET_PROTECT", ACTION_SET_PROTECT, 0, 0, 0 },
	{ "SET_COMMENT", ACTION_SET_COMMENT, 0, 0, 0 },
	{ "SET_DATE", ACTION_SET_DATE, 0, 0, 0 },
	{ "SET_OWNER", ACTION_SET_OWNER, 0, 0, 0 },
	{ "MAKE_LINK", ACTION_MAKE_LINK, 0, 0, 0 },
	{ "KILL_EMPTY", ACTION_KILL_EMPTY, 0, 0, 0 },
	{ "REMOVE_DIRENTRY", ACTION_REMOVE_DIRENTRY, 0, 0, 0 },
	{ "CREATE_ROLLOVER", ACTION_CREATE_ROLLOVER, 0, 0, 0 },
	{ "MORE_CACHE 5", ACTION_MORE_CACHE, 5, 0, 0 },
	{ "SET_DELDIR 0", ACTION_SET_DELDIR, 0, 0, 0 },
	{ "SET_FNSIZE 1", ACTION_SET_FNSIZE, 0, 1, 0 },
	{ "SET_ROLLOVER set", ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_set },
};
#define NMUT ((int)(sizeof(mutating) / sizeof(mutating[0])))

/* the three whose reply is a new entry or lock the handler checks itself */
static const struct mpkt selfchecking[] =
{
	{ "FINDUPDATE", ACTION_FINDUPDATE, 0, 0, 0 },
	{ "FINDOUTPUT", ACTION_FINDOUTPUT, 0, 0, 0 },
	{ "CREATE_DIR", ACTION_CREATE_DIR, 0, 0, 0 },
};
#define NSELF 3

/* packets that never change the volume, also with the arguments of a no-op */
static const struct mpkt readonly[] =
{
	{ "LOCATE_OBJECT", ACTION_LOCATE_OBJECT, 0, 0, 0 },
	{ "FREE_LOCK", ACTION_FREE_LOCK, 0, 0, 0 },
	{ "EXAMINE_OBJECT", ACTION_EXAMINE_OBJECT, 0, 0, 0 },
	{ "SAME_LOCK", ACTION_SAME_LOCK, 0, 0, 0 },
	{ "FINDINPUT", ACTION_FINDINPUT, 0, 0, 0 },
	{ "READ", ACTION_READ, 0, 0, 0 },
	{ "ADD_NOTIFY", ACTION_ADD_NOTIFY, 0, 0, 0 },
	{ "IS_PFS2", ACTION_IS_PFS2, 0, 0, 0 },
	{ "SLEEP", ACTION_SLEEP, 0, 0, 0 },
	{ "ADD_IDLE_SIGNAL", ACTION_ADD_IDLE_SIGNAL, 0, 0, 0 },
	{ "MORE_CACHE 0", ACTION_MORE_CACHE, 0, 0, 0 },
	{ "SET_DELDIR -1", ACTION_SET_DELDIR, 0, -1, 0 },
	{ "SET_FNSIZE 0", ACTION_SET_FNSIZE, 0, 0, 0 },
	{ "SET_LARGEFILE 0", ACTION_SET_LARGEFILE, 0, 0, 0 },
	{ "SET_ROLLOVER read", ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_get },
	{ "SET_ROLLOVER NULL", ACTION_SET_ROLLOVER, 0, 0, 0 },
};
#define NRO ((int)(sizeof(readonly) / sizeof(readonly[0])))

static SIPTR rejected_res1(LONG type)
{
	return (type == ACTION_WRITE || type == ACTION_SET_FILE_SIZE) ? -1 : DOSFALSE;
}

static int mutates(LONG type, SIPTR a1, SIPTR a2, SIPTR a3)
{
	fill(type, a1, a2, a3);
	return PacketMutatesFilesystem(&pkt) != 0;
}

static void test_setrollover_arg(void)
{
	/* the info is in dp_Arg3; dp_Arg2 is dead */
	check(mutates(ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_set), "Arg3 -> a rolloverinfo with set=1 is a mutation");
	check(mutates(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_get), (SIPTR)&ri_set), "set=1 in Arg3 stays a mutation whatever Arg2 points at");
	check(mutates(ACTION_SET_ROLLOVER, 0, BOGUS_BPTR, (SIPTR)&ri_set), "set=1 in Arg3 stays a mutation with a garbage Arg2");
	check(!mutates(ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_get), "Arg3 -> a rolloverinfo with set=0 is no mutation");
	check(!mutates(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_set), (SIPTR)&ri_get),
		"set=0 in Arg3 is no mutation, whatever a BPTR in Arg2 says");
	check(!mutates(ACTION_SET_ROLLOVER, 0, 0, 0), "a NULL Arg3 is no mutation");
	check(!mutates(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_set), 0), "a NULL Arg3 is no mutation even if Arg2 is a BPTR to set=1");
	printf("  SET_ROLLOVER: Arg3 set=1 -> %d  Arg3 set=0 -> %d  Arg3 NULL, Arg2 set=1 -> %d\n",
		mutates(ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_set), mutates(ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_get),
		mutates(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_set), 0));
}

static void test_classification(void)
{
	int i;
	for (i = 0; i < NMUT; i++)
		if (!mutates(mutating[i].type, mutating[i].a1, mutating[i].a2, mutating[i].a3))
			{ printf("  FAIL: %s must count as a mutation\n", mutating[i].name); failures++; }
	for (i = 0; i < NSELF; i++)
		if (!mutates(selfchecking[i].type, 0, 0, 0))
			{ printf("  FAIL: %s must count as a mutation\n", selfchecking[i].name); failures++; }
	for (i = 0; i < NRO; i++)
		if (mutates(readonly[i].type, readonly[i].a1, readonly[i].a2, readonly[i].a3))
			{ printf("  FAIL: %s must not count as a mutation\n", readonly[i].name); failures++; }
}

/* the error shows up while the handler runs */
static void test_error_during_handler(void)
{
	int i;

	for (i = 0; i < NSELF; i++)
	{
		reset(); raise_error = 1;
		send(selfchecking[i].type, 0, 0, 0);
		printf("  %-16s error raised by the handler: res1=0x%lx res2=%ld calls=%d\n", selfchecking[i].name,
			(long)pkt.dp_Res1, (long)pkt.dp_Res2, calls);
		check(calls == 1 && vol.metadata_error, "the handler ran and raised the error");
		if (pkt.dp_Res1 != 0x4321 || pkt.dp_Res2 != 77)
			{ printf("  FAIL: %s must keep the handler's reply (it returns a new entry or lock)\n", selfchecking[i].name); failures++; }
	}

	for (i = 0; i < NMUT; i++)
	{
		reset(); raise_error = 1;
		send(mutating[i].type, mutating[i].a1, mutating[i].a2, mutating[i].a3);
		printf("  %-16s error raised by the handler: res1=%ld res2=%ld calls=%d\n", mutating[i].name,
			(long)pkt.dp_Res1, (long)pkt.dp_Res2, calls);
		if (calls != 1 || pkt.dp_Res1 != rejected_res1(mutating[i].type) || pkt.dp_Res2 != PFS_ERROR_INCONSISTENT)
			{ printf("  FAIL: %s must be failed with PFS_ERROR_INCONSISTENT after the handler raised the error\n", mutating[i].name); failures++; }
	}

	for (i = 0; i < NRO; i++)
	{
		reset(); raise_error = 1;
		send(readonly[i].type, readonly[i].a1, readonly[i].a2, readonly[i].a3);
		if (calls != 1 || pkt.dp_Res1 != 0x4321 || pkt.dp_Res2 != 77)
			{ printf("  FAIL: %s must keep its reply with a metadata error\n", readonly[i].name); failures++; }
	}
}

static void test_no_error(void)
{
	int i;
	for (i = 0; i < NMUT + NSELF; i++)
	{
		const struct mpkt *m = i < NMUT ? &mutating[i] : &selfchecking[i - NMUT];
		reset();
		send(m->type, m->a1, m->a2, m->a3);
		if (calls != 1 || pkt.dp_Res1 != 0x4321 || pkt.dp_Res2 != 77)
			{ printf("  FAIL: %s must keep its reply on a healthy volume\n", m->name); failures++; }
	}
	reset();
	send(ACTION_DELETE_OBJECT, 0, 0, 0);
	check(pkt.dp_Res2 == 77, "the handler's Res2 is what comes back");
	reset(); handler_res2 = 0;
	send(ACTION_DELETE_OBJECT, 0, 0, 0);
	check(pkt.dp_Res2 == 0 && last_type == ACTION_DELETE_OBJECT, "Res2 is cleared before the handler runs");
}

/* the error is there before the packet arrives */
static void test_error_before_packet(void)
{
	int i;
	for (i = 0; i < NMUT + NSELF; i++)
	{
		const struct mpkt *m = i < NMUT ? &mutating[i] : &selfchecking[i - NMUT];
		reset(); vol.metadata_error = TRUE;
		send(m->type, m->a1, m->a2, m->a3);
		if (calls != 0 || pkt.dp_Res1 != rejected_res1(m->type) || pkt.dp_Res2 != PFS_ERROR_INCONSISTENT)
			{ printf("  FAIL: %s must be failed before its handler runs (calls=%d res1=%ld res2=%ld)\n", m->name,
				calls, (long)pkt.dp_Res1, (long)pkt.dp_Res2); failures++; }
	}
	for (i = 0; i < NRO; i++)
	{
		reset(); vol.metadata_error = TRUE;
		send(readonly[i].type, readonly[i].a1, readonly[i].a2, readonly[i].a3);
		if (calls != 1 || pkt.dp_Res1 != 0x4321 || pkt.dp_Res2 != 77)
			{ printf("  FAIL: %s must still run on a volume with a metadata error\n", readonly[i].name); failures++; }
	}
	reset(); vol.metadata_error = TRUE;
	printf("  error set before FINDOUTPUT: ");
	send(ACTION_FINDOUTPUT, 0, 0, 0);
	printf("res1=%ld res2=%ld calls=%d\n", (long)pkt.dp_Res1, (long)pkt.dp_Res2, calls);

	/* no volume -> nothing to fail */
	reset(); gd.currentvolume = NULL;
	send(ACTION_DELETE_OBJECT, 0, 0, 0);
	check(calls == 1 && pkt.dp_Res1 == 0x4321 && pkt.dp_Res2 == 77, "without a current volume a packet is dispatched and not failed");
}

/* ACTION_SET_ROLLOVER through the dispatcher, the volume already in error */
static void test_setrollover_dispatch(void)
{
	reset(); vol.metadata_error = TRUE;
	send(ACTION_SET_ROLLOVER, 0, 0, (SIPTR)&ri_set);
	printf("  SET_ROLLOVER write, volume in error: res1=%ld res2=%ld calls=%d\n", (long)pkt.dp_Res1, (long)pkt.dp_Res2, calls);
	check(calls == 0 && pkt.dp_Res1 == DOSFALSE && pkt.dp_Res2 == PFS_ERROR_INCONSISTENT, "a SET_ROLLOVER write (Arg3 -> set=1) is refused");

	reset(); vol.metadata_error = TRUE;
	send(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_get), (SIPTR)&ri_set);
	check(calls == 0 && pkt.dp_Res2 == PFS_ERROR_INCONSISTENT, "refused, whatever Arg2 holds");

	reset(); vol.metadata_error = TRUE;
	send(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_set), (SIPTR)&ri_get);
	check(calls == 1 && pkt.dp_Res1 == 0x4321 && pkt.dp_Res2 == 77, "a SET_ROLLOVER read (Arg3 -> set=0) is still served, whatever Arg2 holds");

	reset(); vol.metadata_error = TRUE;
	send(ACTION_SET_ROLLOVER, 0, MKBADDR(&ri_set), 0);
	check(calls == 1 && pkt.dp_Res1 == 0x4321 && pkt.dp_Res2 == 77, "a SET_ROLLOVER with a NULL Arg3 is passed on to the handler to refuse");

	/* last: the old code followed BADDR(Arg2) into nowhere here */
	reset(); vol.metadata_error = TRUE;
	send(ACTION_SET_ROLLOVER, 0, BOGUS_BPTR, 0);
	check(calls == 1 && pkt.dp_Res2 == 77, "Arg2 of SET_ROLLOVER is never dereferenced");
}

/* a classifier that follows the dead dp_Arg2 must fail the test, not hide in a core dump */
static void on_crash(int sig)
{
	static const char msg[] = "  FAIL: crashed following a pointer the packet classifier must not follow\n";
	(void)sig;
	if (write(1, msg, sizeof(msg) - 1) < 0) _exit(2);
	_exit(1);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGSEGV, on_crash);
	printf("PacketMutatesFilesystem: ACTION_SET_ROLLOVER reads dp_Arg3\n");
	reset();
	test_classification();
	test_setrollover_arg();
	printf("NormalCommands: reply of a packet whose handler raised a metadata error\n");
	test_error_during_handler();
	test_no_error();
	printf("NormalCommands: metadata error already set\n");
	test_error_before_packet();
	test_setrollover_dispatch();

	if (failures == 0) { printf("  PASS: SET_ROLLOVER is classified by dp_Arg3 and Open/CreateDir keep their reply\n"); return 0; }
	printf("  FAIL: %d check(s) failed\n", failures);
	return 1;
}
