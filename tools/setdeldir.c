/* $Id: setdeldir.c 1.2 1999/03/18 02:47:18 Michiel Exp Michiel $ */
/* $Log: setdeldir.c $
 * Revision 1.2  1999/03/18  02:47:18  Michiel
 * none
 *
 * Revision 1.1  1999/03/11  04:25:10  Michiel
 * Initial revision
 * */

#define __USE_SYSBASE
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>

#define REVISION "1.2"
#define DATE "11.3.1999"
#define TEMPLATE "DEVICE/A,DELDIRSIZE/N"
#define ACTION_SLEEP 2200
#define ACTION_UPDATE_ANODE 2201
#define ACTION_AFS_INFO 2202
#define ACTION_AFS_CONFIG 2203
#define ACTION_CREATE_ROLLOVER 2205
#define ACTION_SET_ROLLOVER 2206
#define ACTION_SET_DELDIR 2221
#define ID_PFS2_DISK (0x50465302L)	/*	'PFS\2'	*/

UBYTE *version = "$VER: SetDeldir " REVISION " (" DATE ") by Michiel Pelt";
LONG args[2];

void main(void)
{ 
  struct RDArgs *result;
  struct MsgPort *msgport;
  LONG error, nbr;

	SysBase = *((struct ExecBase **)4);
	if (SysBase->LibNode.lib_Version < 37)
		exit(RETURN_FAIL);

	/* read arguments */
	if (!(result = ReadArgs(TEMPLATE, args, NULL)))
	{
		PrintFault(ERROR_REQUIRED_ARG_MISSING, "SetDeldir");
		exit(RETURN_FAIL);
	}

	/* find device */
	if (!(msgport = DeviceProc((char *)args[0])))	
	{
		PutStr("Could not find device\n");
		FreeArgs(result);
		exit(RETURN_FAIL);
	}

	if (args[1])
	{

		if (!DoPkt(msgport, ACTION_SET_DELDIR, ID_PFS2_DISK, *(LONG *)args[1],
			   NULL, NULL, NULL))
		{
			error = IoErr();

			switch (error) {

				case ERROR_ACTION_NOT_KNOWN:

					puts("ERROR: This disk does not support deldir extension.");
					puts("Setdeldir is only supported by disks formatted with");
					puts("PFS2 4.3 or higher.");
					break;

				case ERROR_OBJECT_IN_USE:

					puts("ERROR: The deldir is in use.");
					puts("The size of the deldir can only be changed when it is");
					puts("not in use by some program. Make sure no file of the");
					puts("deldir is open and no shell has the deldir as current");
					puts("directory.");
					break;

				case ERROR_BAD_NUMBER:

					puts("ERROR: Number out of range");
					break;

				case ERROR_DISK_FULL:

					puts("ERROR: disk full");
					break;

				default:

					PrintFault(error, "SetDeldir");
			}

		}
		else
		{
			printf("deldir size changed\n\n");
		}
	}


	if (DoPkt(msgport, ACTION_SET_DELDIR, ID_PFS2_DISK, -1, NULL,
			NULL, NULL))
	{
		nbr = IoErr();
		printf("Current number of deldirblocks is %ld\n", nbr);
		printf("The delete directory has a maximum capacity of %ld deleted files\n", nbr*31);
	}

	FreeArgs(result);
}
