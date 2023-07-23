/* $Id: setfnsize.c 1.1 1999/06/08 09:57:00 Michiel Exp Michiel $
 * $Log: setfnsize.c $
 * Revision 1.1  1999/06/08  09:57:00  Michiel
 * Initial revision
 *
 */

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

#define REVISION "1.1"
#define DATE "13.5.99"
#define TEMPLATE "VOLUME/A,FILENAMESIZE/N"
#define ACTION_SLEEP 2200
#define ACTION_UPDATE_ANODE 2201
#define ACTION_AFS_INFO 2202
#define ACTION_AFS_CONFIG 2203
#define ACTION_CREATE_ROLLOVER 2205
#define ACTION_SET_ROLLOVER 2206
#define ACTION_SET_FNSIZE 2222
#define ID_PFS_DISK	(0x50465302L)	/*	'PFS\2' */

UBYTE *version = "$VER: Setfnsize " REVISION " (" DATE ") by Michiel Pelt";
LONG args[2];

void main(void)
{ 
  struct RDArgs *result;
  struct MsgPort *msgport;
  ULONG fnsize;
  ULONG error;

	SysBase = *((struct ExecBase **)4);
	if (SysBase->LibNode.lib_Version < 37)
		exit(RETURN_FAIL);

	/* read arguments */
	if (!(result = ReadArgs(TEMPLATE, args, NULL)))
	{
		PrintFault(ERROR_REQUIRED_ARG_MISSING, "Setfnsize");
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
		fnsize = *(ULONG *)args[1];
	else
		fnsize = 0;

	if (!DoPkt(msgport, ACTION_SET_FNSIZE, ID_PFS_DISK, fnsize,
			   NULL, NULL, NULL))
	{
		error = IoErr();
		if (error == ERROR_BAD_NUMBER)
			printf("Filenamesize must be larger than current size\n");
		else if (error == ERROR_OBJECT_TOO_LARGE)
			printf("The maximum filenamesize is 107\n");
		else if (error == ERROR_ACTION_NOT_KNOWN)
			printf("Command not supported by this device\n");
		else
			PrintFault(IoErr(), "Setfnsize");
	}
	else
	{
		printf("maximum filename size is now %d\n", IoErr());
	}

	FreeArgs(result);
}
