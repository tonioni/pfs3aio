/* The full format of pfsformat: writes every sector of the partition through
 * the device layer of pfsdoctor, and with VERIFY reads it back. It works in
 * native sectors, the unit of the partition limits in the DosEnvec.
 */
#define __USE_SYSBASE
#include <string.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "pfs3.h"
#include "doctor.h"

/* sectors written per transfer */
#define CHUNK 128

volume_t volume;

/* the cache of the device layer is not used here */
void adderror(char *message, ...)
{
	(void)message;
}

void *pfscalloc(size_t n, size_t size)
{
	return AllocVec(n * size, MEMF_CLEAR);
}

void pfsfree(void *mem)
{
	FreeVec(mem);
}

static void ShowMsg(const char *format, ...)
{
	VPrintf((STRPTR)format, (LONG *)(&format + 1));
}

static BOOL Break(void)
{
	if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
	{
		PutStr("\nbreak\n");
		return TRUE;
	}
	return FALSE;
}

/* fills buffer of n sectors with the format pattern, counting on from *marker */
static void Fill(ULONG *buffer, ULONG n, ULONG sectorsize, ULONG *marker)
{
	ULONG i, j, longs = sectorsize / 4;

	for (i = 0; i < n * longs; i += longs)
	{
		strcpy((char *)&buffer[i], "Disk formatted with PFS3 format");
		for (j = i + 8; j < i + longs; j++)
			buffer[j] = (*marker)++;
	}
}

/* a trackdisk command without data; FALSE on an error */
static BOOL TDCommand(UWORD command, ULONG length)
{
	volume.request->iotd_Req.io_Command = command;
	volume.request->iotd_Req.io_Length = length;
	return !DoIO((struct IORequest *)volume.request);
}

static struct DosEnvec env;
static BOOL trackdisk, opened;

/* the partition of dl in native sectors; FALSE if its geometry is not usable */
static BOOL SetupPartition(struct DosList *dl)
{
	struct DosEnvec *de;
	ULONG cylsectors, sectorsize, b;

	memset(&volume, 0, sizeof(volume));
	volume.fssm = (struct FileSysStartupMsg *)BADDR(dl->dol_misc.dol_handler.dol_Startup);
	de = (struct DosEnvec *)BADDR(volume.fssm->fssm_Environ);
	/* a short DosEnvec gets the defaults of the handler */
	memset(&env, 0, sizeof(env));
	env.de_BufMemType = MEMF_PUBLIC;
	env.de_MaxTransfer = 0x20000;
	env.de_Mask = 0xfffffffe;
	CopyMem(de, &env, (de->de_TableSize < DE_MASK ? de->de_TableSize : DE_MASK) * 4 + 4);
	volume.dosenvec = &env;
	volume.showmsg = ShowMsg;
	cylsectors = env.de_Surfaces * env.de_BlocksPerTrack;
	sectorsize = env.de_SizeBlock << 2;
	if (!cylsectors || sectorsize < 512 || sectorsize > 4096 || (sectorsize & (sectorsize - 1)) ||
		env.de_MaxTransfer < sectorsize || (env.de_HighCyl + 1) > 0xFFFFFFFFUL / cylsectors)
		return FALSE;
	/* one logical block is one native sector here */
	volume.blocksize = sectorsize;
	for (b = sectorsize, volume.blockshift = -1; b; b >>= 1)
		volume.blockshift++;
	volume.blocklogshift = 0;
	volume.firstblock = volume.firstblocknative = env.de_LowCyl * cylsectors;
	volume.lastblock = volume.lastblocknative = (env.de_HighCyl + 1) * cylsectors - 1;
	return TRUE;
}

/* opens the device of the partition and finds its access mode, buffer holds
 * one sector; FALSE with a message
 */
static BOOL OpenPartition(UBYTE *buffer)
{
	if (!(opened = OpenDiskDevice(volume.fssm, &volume.port, &volume.request, &trackdisk)))
	{
		PutStr("Device could not be opened\n");
		return FALSE;
	}
	if (!DetectAccessmode(buffer, FALSE))
	{
		PutStr("No access mode reaches the end of the partition\n");
		return FALSE;
	}
	if (volume.accessmode == ACCESS_DS)
	{
		volume.getrawblocks = dev_GetBlocksDS;
		volume.writerawblocks = dev_WriteBlocksDS;
	}
	else
	{
		volume.td64mode = volume.accessmode == ACCESS_TD64;
		volume.nsdmode = volume.accessmode == ACCESS_NSD;
		volume.getrawblocks = dev_GetBlocks;
		volume.writerawblocks = dev_WriteBlocks;
	}
	return TRUE;
}

/* every transfer is a DoIO, so nothing is pending */
static void ClosePartition(void)
{
	if (opened)
		CloseDevice((struct IORequest *)volume.request);
	if (volume.request)
		DeleteIORequest((struct IORequest *)volume.request);
	if (volume.port)
		DeleteMsgPort(volume.port);
	opened = FALSE;
	volume.request = NULL;
	volume.port = NULL;
}

/* reads the rootblock of the partition of dl into rb, and the size of its
 * logical blocks into blocksize
 */
BOOL ReadRootBlock(struct DosList *dl, rootblock_t *rb, ULONG *blocksize)
{
	ULONG per, first, *buffer;
	BOOL ok = FALSE;

	if (!SetupPartition(dl))
		return FALSE;
	/* the rootblock is logical block 2, of de_SectorPerBlock sectors each,
	 * counted from the first sector on a block boundary as the handler does */
	per = env.de_SectorPerBlock ? env.de_SectorPerBlock : 1;
	first = (volume.firstblock + per - 1) / per * per;
	if (!(buffer = AllocBufMem(per * volume.blocksize)))
		return FALSE;
	if (OpenPartition((UBYTE *)buffer) &&
		volume.getrawblocks((UBYTE *)buffer, per, first + ROOTBLOCK * per) == e_none)
	{
		CopyMem(buffer, rb, sizeof(*rb));
		*blocksize = per * volume.blocksize;
		ok = TRUE;
	}
	ClosePartition();
	FreeBufMem(buffer);
	return ok;
}

BOOL FullFormat(struct DosList *dl, BOOL verify)
{
	ULONG cylsectors, sectorsize, cyl, sector, end, n, marker = 0, sectors;
	ULONG *buffer = NULL, *vbuffer = NULL;
	BOOL ok = FALSE;
	LONG c;

	if (!SetupPartition(dl))
	{
		PutStr("Full format is not possible\n");
		return FALSE;
	}
	cylsectors = env.de_Surfaces * env.de_BlocksPerTrack;
	sectorsize = volume.blocksize;
	sectors = volume.lastblock - volume.firstblock + 1;

	do
	{
		if (!(buffer = AllocBufMem(CHUNK * sectorsize)) ||
			(verify && !(vbuffer = AllocBufMem(CHUNK * sectorsize))))
		{
			PutStr("Out of memory\n");
			break;
		}
		if (!OpenPartition((UBYTE *)buffer))
			break;

		Printf("LowCyl    %lu\nHighCyl   %lu\nDisksize  %lu sectors = %lu MiB\n\n", env.de_LowCyl,
			env.de_HighCyl, sectors, sectors / (1048576 / sectorsize));
		ok = TRUE;
		for (cyl = env.de_LowCyl; ok && cyl <= env.de_HighCyl; cyl++)
		{
			Printf(verify ? "\rFormatting and verifying cylinder %lu" : "\rFormatting cylinder %lu", cyl);
			Flush(Output());
			for (sector = cyl * cylsectors, end = sector + cylsectors; sector < end; sector += n)
			{
				n = end - sector < CHUNK ? end - sector : CHUNK;
				Fill(buffer, n, sectorsize, &marker);
				/* trackdisk writes a track out only on CMD_UPDATE */
				if (volume.writerawblocks((UBYTE *)buffer, n, sector) != e_none ||
					(trackdisk && !TDCommand(CMD_UPDATE, 0)))
				{
					Printf("\nWrite error on cylinder %lu\n", cyl);
					ok = FALSE;
					break;
				}
				if (!verify)
					continue;
				if ((trackdisk && !TDCommand(CMD_CLEAR, 0)) ||
					volume.getrawblocks((UBYTE *)vbuffer, n, sector) != e_none ||
					memcmp(buffer, vbuffer, n * sectorsize) != 0)
				{
					Printf("\nVerify error on cylinder %lu. Continue or break (C/B)? ", cyl);
					Flush(Output());
					while ((c = FGetC(Input())) != -1 && c != 'c' && c != 'C' && c != 'b' && c != 'B')
						;
					if (c != 'c' && c != 'C')
					{
						ok = FALSE;
						break;
					}
				}
			}
			if (ok && Break())
				ok = FALSE;
		}
		PutStr("\n");
		if (trackdisk)
			TDCommand(TD_MOTOR, 0);
	} while (0);

	ClosePartition();
	if (buffer)
		FreeBufMem(buffer);
	if (vbuffer)
		FreeBufMem(vbuffer);
	return ok;
}
