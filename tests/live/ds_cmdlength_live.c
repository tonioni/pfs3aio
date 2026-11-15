/* LIVE @src disk.c @func DoSCSICommand
 *
 * DoSCSICommand takes the CDB length from the opcode: group 0 (below 0x20) is
 * 6 bytes, groups 1 and 2 are 10. Lifts the checkout's real DoSCSICommand with
 * a stubbed DoIO and checks the length it passes for every opcode the handler
 * sends: TEST UNIT READY, INQUIRY, MODE SENSE(6), READ CAPACITY(10), READ(10)
 * and WRITE(10).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uint8_t UBYTE; typedef int8_t BYTE;
typedef int BOOL; typedef void *APTR;
#define TRUE  1
#define FALSE 0

#define HD_SCSICMD      28
#define SCSIF_WRITE     0
#define SCSIF_READ      1
#define SCSIF_AUTOSENSE 2

struct SCSICmd {
    UWORD *scsi_Data; ULONG scsi_Length, scsi_Actual; UBYTE *scsi_Command; UWORD scsi_CmdLength, scsi_CmdActual;
    UBYTE scsi_Flags, scsi_Status; UBYTE *scsi_SenseData; UWORD scsi_SenseLength, scsi_SenseActual;
};
struct IORequest { ULONG io_Length; APTR io_Data; UWORD io_Command; };
struct IOExtTD { struct IORequest iotd_Req; };
typedef struct globaldata {
    struct IOExtTD *request; UBYTE sense[20]; struct SCSICmd scsicmd;
} globaldata;

static UWORD sent_len;
static int sent_cmd;
static BYTE DoIO(struct IORequest *r)
{
    struct SCSICmd *c = (struct SCSICmd *)r->io_Data;
    sent_cmd = r->io_Command;
    sent_len = c->scsi_CmdLength;
    c->scsi_Status = 0;
    c->scsi_Actual = c->scsi_Length;
    return 0;
}

#include "gen/DoSCSICommand.c"               /* the REAL function from ../../disk.c */

int main(void)
{
    static const struct { UBYTE op; UWORD len; const char *name; } cmds[] = {
        { 0x00, 6, "TEST UNIT READY" }, { 0x12, 6, "INQUIRY" }, { 0x1a, 6, "MODE SENSE(6)" },
        { 0x25, 10, "READ CAPACITY(10)" }, { 0x28, 10, "READ(10)" }, { 0x2a, 10, "WRITE(10)" },
    };
    struct IOExtTD req;
    globaldata g;
    UBYTE cdb[10], buf[512];
    unsigned i;
    int failures = 0;

    memset(&g, 0, sizeof(g));
    g.request = &req;
    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
    {
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = cmds[i].op;
        sent_len = 0; sent_cmd = 0;
        if (!DoSCSICommand(buf, sizeof(buf), 0, cdb, SCSIF_READ, &g) || sent_cmd != HD_SCSICMD || sent_len != cmds[i].len)
        {
            printf("  FAIL: %s (0x%02x) sent with a %u byte CDB, want %u\n", cmds[i].name, cmds[i].op, sent_len, cmds[i].len);
            failures++;
        }
    }
    if (failures)
        return 1;
    printf("  PASS: DoSCSICommand sends 6 byte CDBs for group 0 opcodes and 10 byte CDBs for READ CAPACITY, READ and WRITE\n");
    return 0;
}
