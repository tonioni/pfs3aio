
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

#define REVISION "1.0"
#define TEMPLATE "VOLUME/A,ENABLE/S"
#define ACTION_SET_LARGEFILE 2223
#define ID_PFS_DISK	(0x50465302L)	/*	'PFS\2' */

UBYTE *version = "$VER: Setlargefile " REVISION " (" REVDATE ") by Toni Wilen";
LONG args[2];

void main(void)
{ 
  struct RDArgs *result;
  struct MsgPort *msgport;
  ULONG enable = 0;
  ULONG error;

	SysBase = *((struct ExecBase **)4);
	if (SysBase->LibNode.lib_Version < 37)
		exit(RETURN_FAIL);

	/* read arguments */
	if (!(result = ReadArgs(TEMPLATE, args, NULL)))
	{
		PrintFault(ERROR_REQUIRED_ARG_MISSING, "Setlargefile");
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
		enable = 1;

	if (!DoPkt(msgport, ACTION_SET_LARGEFILE, ID_PFS_DISK, enable,
			   NULL, NULL, NULL))
	{
		error = IoErr();
		if (error == ERROR_BAD_NUMBER)
			printf("Volume is not large file capable.\n");
		else if (error == ERROR_ACTION_NOT_KNOWN)
			printf("Command not supported by this device\n");
		else
			PrintFault(IoErr(), "Setlargefile");
	}
	else
	{
		error = IoErr();
		if (error)
			printf("Large file support enabled.\n");
		else
			printf("Large file support not enabled.\n");
	}

	FreeArgs(result);
}
