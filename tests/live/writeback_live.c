/* LIVE @src boot.c @func SetTimer @func HandleTimer @func CloseTimer
 *
 * Exercise the real handler timer code with a relative timer.device mock.
 * Packet traffic refreshes the ordinary activity timeout just as file opens,
 * reads, seeks and writes do. Persistence must not depend on that traffic
 * stopping, or on the adjustable system clock.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define TR_ADDREQUEST 9

struct Message { int unused; };
struct IORequest { int io_Command; void *io_Device; };
struct timerequest {
    struct IORequest tr_node;
    struct { ULONG tv_secs, tv_micro; } tr_time;
    uint64_t due;
    BOOL pending, queued;
};
struct MsgPort {
    struct Message *messages[4];
    unsigned count;
};
struct idlehandle {
    struct idlehandle *next;
    void *task;
    unsigned dirtysignal, cleansignal;
};
struct MinList { struct idlehandle *head; };
#define HeadOf(list) ((list)->head)

typedef struct globaldata {
    struct MsgPort *timeport;
    struct timerequest *trequest, *flushrequest;
    struct MinList idlelist;
    unsigned timeout, inhibitcount;
    BOOL dirty, softprotect, timeron, flushtimeron;
} globaldata;

static globaldata g;
static struct timerequest activity, writeback;
static struct MsgPort port;
static struct idlehandle tail;
static uint64_t now, first_commit;
static unsigned updates, commits, fail_updates, motors_off;
static unsigned deadline_starts;
static unsigned aborts, waits, closes, deletes;

static void SendIO(struct IORequest *io)
{
    struct timerequest *request = (struct timerequest *)io;
    assert(!request->pending && !request->queued);
    assert(io->io_Command == TR_ADDREQUEST);
    request->due = now + request->tr_time.tv_secs * 1000000ULL +
        request->tr_time.tv_micro;
    assert(request->due > now);
    request->pending = TRUE;
    if (request == &writeback)
        deadline_starts++;
}

static struct Message *GetMsg(struct MsgPort *reply_port)
{
    struct Message *message;
    struct timerequest *request;
    unsigned i;
    if (!reply_port->count)
        return NULL;
    message = reply_port->messages[0];
    for (i = 1; i < reply_port->count; i++)
        reply_port->messages[i - 1] = reply_port->messages[i];
    reply_port->count--;
    request = (struct timerequest *)message;
    request->queued = FALSE;
    return message;
}

static BOOL UpdateDisk(globaldata *state)
{
    assert(state->dirty && !state->softprotect && !state->inhibitcount);
    updates++;
    if (fail_updates) {
        fail_updates--;
        return FALSE;
    }
    if (!commits)
        first_commit = now;
    commits++;
    state->dirty = FALSE;
    return TRUE;
}

static void UpdateAndMotorOff(globaldata *state)
{
    (void)state;
    motors_off++;
}

static void Signal(void *task, long mask)
{
    (void)task;
    (void)mask;
}

static BOOL CheckIO(struct IORequest *io)
{
    return !((struct timerequest *)io)->pending;
}

static void AbortIO(struct IORequest *io)
{
    struct timerequest *request = (struct timerequest *)io;
    assert(request->pending);
    aborts++;
    request->pending = FALSE;
}

static void WaitIO(struct IORequest *io)
{
    struct timerequest *request = (struct timerequest *)io;
    assert(!request->pending);
    request->queued = FALSE;
    waits++;
}

static void CloseDevice(struct IORequest *io)
{
    struct timerequest *request = (struct timerequest *)io;
    assert(io->io_Device && !request->pending && !request->queued);
    closes++;
    io->io_Device = NULL;
}

static void DeleteIORequest(struct IORequest *io)
{
    struct timerequest *request = (struct timerequest *)io;
    assert(!io->io_Device && !request->pending && !request->queued);
    deletes++;
}

#include "gen/SetTimer.c"
#include "gen/HandleTimer.c"
#include "gen/CloseTimer.c"

static void Reset(void)
{
    memset(&g, 0, sizeof(g));
    memset(&activity, 0, sizeof(activity));
    memset(&writeback, 0, sizeof(writeback));
    memset(&port, 0, sizeof(port));
    memset(&tail, 0, sizeof(tail));
    g.timeport = &port;
    g.trequest = &activity;
    g.flushrequest = &writeback;
    g.idlelist.head = &tail;
    now = first_commit = 0;
    updates = commits = fail_updates = motors_off = deadline_starts = 0;
    aborts = waits = closes = deletes = 0;
}

static void Complete(struct timerequest *request)
{
    if (request->pending && request->due <= now) {
        request->pending = FALSE;
        request->queued = TRUE;
        assert(port.count < 4);
        port.messages[port.count++] = (struct Message *)request;
    }
}

/* Finish a packet at the given relative time, optionally doing read/write
 * activity. Completions are serviced even if the DOS queue never empties.
 */
static void Packet(uint64_t time, BOOL read, BOOL write)
{
    now = time;
    Complete(&activity);
    Complete(&writeback);
    if (read || write)
        g.timeout |= 3;
    if (write)
        g.dirty = TRUE;
    HandleTimer(&g);
}

int main(void)
{
    uint64_t t, original_deadline;
    unsigned before;

    Reset();
    Packet(0, FALSE, TRUE);
    for (t = 100000; t <= 6000000; t += 100000)
        Packet(t, t % 1000000 == 0, FALSE);
    assert(commits == 1 && first_commit <= 5000000);
    puts("  PASS: one-second file reads cannot starve dirty writeback");

    Reset();
    Packet(0, FALSE, TRUE);
    for (t = 100000; t <= 16000000; t += 100000)
        Packet(t, TRUE, TRUE);
    assert(commits == 3 && first_commit == 5000000);
    puts("  PASS: continuously busy read/write packets commit successive batches");

    Reset();
    Packet(0, FALSE, TRUE);
    for (t = 100000; t <= 3000000; t += 100000)
        Packet(t, FALSE, FALSE);
    assert(commits == 1 && first_commit == 2300000);
    for (; t <= 7000000; t += 100000)
        Packet(t, FALSE, FALSE);
    assert(updates == 1 && !g.timeron && !g.flushtimeron);
    assert(deadline_starts == 1);
    puts("  PASS: normal idle writeback stays fast; clean deadlines stop");

    Reset();
    Packet(0, FALSE, TRUE);
    original_deadline = writeback.due;
    /* An explicit flush/checkpoint commits early. A subsequent batch may
     * inherit the earlier deadline, but must not extend it.
     */
    now = 1000000;
    UpdateDisk(&g);
    Packet(2000000, TRUE, TRUE);
    assert(writeback.due == original_deadline);
    Packet(5000000, TRUE, FALSE);
    assert(commits == 2 && !g.dirty);
    puts("  PASS: early commits and later dirty batches cannot extend a deadline");

    Reset();
    fail_updates = 1;
    Packet(0, FALSE, TRUE);
    for (t = 100000; t <= 5000000; t += 100000)
        Packet(t, TRUE, FALSE);
    assert(updates == 1 && !commits && g.dirty && g.flushtimeron);
    for (; t <= 10000000; t += 100000)
        Packet(t, TRUE, FALSE);
    assert(updates == 2 && commits == 1 && !g.dirty);
    puts("  PASS: failed deadline commits remain dirty and retry during reads");

    Reset();
    Packet(0, FALSE, TRUE);
    g.softprotect = TRUE;
    for (t = 100000; t <= 12000000; t += 100000)
        Packet(t, FALSE, FALSE);
    assert(!updates && g.dirty && !g.flushtimeron && !g.timeron);
    assert(deadline_starts == 1);
    g.softprotect = FALSE;
    Packet(t, TRUE, FALSE);
    for (before = commits; t <= 18000000 && commits == before; t += 100000)
        Packet(t, TRUE, FALSE);
    assert(commits == 1 && !g.dirty);
    puts("  PASS: soft protection suppresses retries; unprotect resumes writeback");

    Reset();
    Packet(0, FALSE, TRUE);
    g.inhibitcount = 1;
    Packet(6000000, TRUE, FALSE);
    assert(!updates && g.dirty && !g.timeron && !g.flushtimeron);
    g.inhibitcount = 0;
    Packet(6100000, TRUE, FALSE);
    assert(g.timeron && g.flushtimeron);
    Packet(11100000, TRUE, FALSE);
    assert(updates == 1 && commits == 1);
    puts("  PASS: inhibit drains both timers without writes or request reuse");

    Reset();
    Packet(0, FALSE, TRUE);
    /* Both messages can arrive before the next packet boundary. */
    g.timeout = 0;
    Packet(7000000, FALSE, FALSE);
    assert(updates == 1 && commits == 1 && port.count == 0);
    assert(g.timeron && !g.flushtimeron);
    puts("  PASS: simultaneous idle/deadline replies cause one commit");

    Reset();
    for (t = 0; t <= 20000000; t += 100000)
        Packet(t, TRUE, FALSE);
    assert(!updates && !deadline_starts && !g.flushtimeron);
    puts("  PASS: clean read-only traffic never arms writeback");

    Reset();
    CloseTimer(NULL, FALSE, &g);
    assert(!aborts && !waits && !closes && !deletes);
    CloseTimer(&activity, FALSE, &g); /* OpenDevice failed. */
    assert(!closes && deletes == 1);
    activity.tr_node.io_Device = &g;
    CloseTimer(&activity, FALSE, &g); /* Opened, never sent. */
    assert(!aborts && !waits && closes == 1 && deletes == 2);

    activity.tr_node.io_Device = &g;
    activity.pending = TRUE;
    CloseTimer(&activity, TRUE, &g);
    assert(aborts == 1 && waits == 1 && closes == 2 && deletes == 3);

    activity.tr_node.io_Device = &g;
    activity.queued = TRUE;
    CloseTimer(&activity, TRUE, &g);
    assert(aborts == 1 && waits == 2 && closes == 3 && deletes == 4);
    puts("  PASS: shutdown handles unsent, pending and queued timer requests");
    return 0;
}
