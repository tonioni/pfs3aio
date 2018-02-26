
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <resources/filesysres.h>
#include <proto/exec.h>
#include "versionhistory.doc"

extern CONST UBYTE shortname[];

#ifndef __SASC
#undef NewList
#define NewList(list) \
({									\
  struct List *_NewList_list = (list);					\
  _NewList_list->lh_TailPred = (struct Node *)_NewList_list;		\
  _NewList_list->lh_Head = (struct Node *)&_NewList_list->lh_Tail;	\
  _NewList_list->lh_Tail = 0;						\
})
#endif

/* AddToFSResource
**
** function supplied by Nicola Salmoria
*/
void AddToFSResource(ULONG dostype, BPTR seglist, struct ExecBase *SysBase)
{
  struct FileSysResource *FileSysResBase;

	FileSysResBase = (struct FileSysResource *)OpenResource(FSRNAME);
	if (!FileSysResBase) {
		FileSysResBase = AllocMem(sizeof(struct FileSysResource), MEMF_CLEAR | MEMF_PUBLIC);
		FileSysResBase->fsr_Creator = (UBYTE*)shortname;
		NewList(&FileSysResBase->fsr_FileSysEntries);
		FileSysResBase->fsr_Node.ln_Type = NT_RESOURCE;
		FileSysResBase->fsr_Node.ln_Name = FSRNAME;
		AddTail(&SysBase->ResourceList, &FileSysResBase->fsr_Node);
	}
	if (FileSysResBase)
	{
		struct FileSysEntry *fse,*nfse;

		Forbid();

		fse = (struct FileSysEntry *)FileSysResBase->fsr_FileSysEntries.lh_Head;
		while ((nfse = (struct FileSysEntry *)fse->fse_Node.ln_Succ))
		{
			/* if filesystem already in resource, return */
			if (fse->fse_DosType == dostype)
			{
				break;
			}

			fse = nfse;
		}

		if (!nfse && (fse = AllocMem(sizeof(struct FileSysEntry), MEMF_PUBLIC | MEMF_CLEAR)))
		{
			fse->fse_Node.ln_Name = (UBYTE*)shortname;
			fse->fse_DosType = dostype;
			fse->fse_Version = ((LONG)VERNUM) << 16 | REVNUM;
			fse->fse_PatchFlags = 0x180;
			fse->fse_SegList = seglist;
			fse->fse_GlobalVec = -1;

			AddHead(&FileSysResBase->fsr_FileSysEntries,&fse->fse_Node);
		}

		Permit();
	}
}

extern void entrypoint(void);
void ResidentAddToFSResource(void)
{
	struct ExecBase *eb =  *((struct ExecBase **)4);

	AddToFSResource(0x50465303, MKBADDR(entrypoint) - 2, eb); // PFS\3
	AddToFSResource(0x50445303, MKBADDR(entrypoint) - 2, eb); // PDS\3
}
