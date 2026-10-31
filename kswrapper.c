
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/alerts.h>
#include <exec/ports.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#include "blocks.h"
#include "struct.h"

#ifdef KSWRAPPER

#ifndef __SASC
#define NewList(list) \
({									\
  struct List *_NewList_list = (list);					\
  _NewList_list->lh_TailPred = (struct Node *)_NewList_list;		\
  _NewList_list->lh_Head = (struct Node *)&_NewList_list->lh_Tail;	\
  _NewList_list->lh_Tail = 0;						\
})
#endif

#define D(x)

/* Must check both ExecBase and DOSBase, CDTV KS 1.3 + extended ROM 2.x: v37 exec but v34 dos! */

#define ISDOS20 (g->v37DOS)
#define ISEXEC20 (g->v37EXEC)
#define ISEXEC30 (g->v39EXEC)

APTR W_AllocVec(ULONG size, ULONG flags, struct globaldata *g)
{
	if (ISEXEC20) {
		return AllocVec(size, flags);
	} else {
		ULONG *mem;
		
		if (!size)
				return NULL;
		size += sizeof(ULONG);
		mem = AllocMem(size, flags);
		if (!mem)
				return NULL;
		mem[0] = size;
		mem++;
		return mem;
	}
}
void W_FreeVec(APTR mem, struct globaldata *g)
{
	if (ISEXEC20) {
		FreeVec(mem);
	} else {
		ULONG *p = mem;
		if (!p)
			return;
		p--;
		FreeMem(p, p[0]);
	}
}

APTR W_CreateIORequest(struct MsgPort *ioReplyPort, ULONG size, struct globaldata *g)
{
	struct IORequest *ret=NULL;
	if(ioReplyPort==NULL)
		return NULL;
	ret=(struct IORequest *)AllocMem(size,MEMF_PUBLIC|MEMF_CLEAR);
	if(ret!=NULL)
	{
		ret->io_Message.mn_ReplyPort=ioReplyPort;
		ret->io_Message.mn_Length=size;
	}
	return ret;
}

void W_DeleteIORequest(APTR iorequest, struct globaldata *g)
{
	if(iorequest != NULL) {
		FreeMem(iorequest, ((struct Message *)iorequest)->mn_Length);
	}
}

struct MsgPort *W_CreateMsgPort(struct globaldata *g)
{
	struct MsgPort *ret;
	ret=(struct MsgPort *)AllocMem(sizeof(struct MsgPort),MEMF_PUBLIC|MEMF_CLEAR);
	if(ret!=NULL)
	{
		BYTE sb;
		sb=AllocSignal(-1);
		if (sb != -1)
		{
			ret->mp_Flags = PA_SIGNAL;
			ret->mp_Node.ln_Type = NT_MSGPORT;
  		NewList(&ret->mp_MsgList);
			ret->mp_SigBit=sb;
			ret->mp_SigTask=FindTask(NULL);
			return ret;
		}
		FreeMem(ret,sizeof(struct MsgPort));
	}
	return NULL;
}

void W_DeleteMsgPort(struct MsgPort *port, struct globaldata *g)
{
	if(port!=NULL)
	{
		FreeSignal(port->mp_SigBit);
		FreeMem(port,sizeof(struct MsgPort));
	}
}

BOOL W_MatchPatternNoCase(STRPTR pat, STRPTR str, struct globaldata *g)
{
	if (ISDOS20)
		return MatchPatternNoCase(pat, str);
	/* Used by ACTION_EXAMINE_ALL which is 2.0+ only packet */
	return FALSE;
}

STRPTR W_FilePart(STRPTR path, struct globaldata *g)
{
	if (ISDOS20) {
		return FilePart(path);
	} else {
		if(path)
		{
			STRPTR i;
			if (!*path)
			  return (STRPTR)path;
			i = path + strlen (path) -1;
			while ((*i != ':') && (*i != '/') && (i != path))
				i--;
			if ((*i == ':')) i++;
			if ((*i == '/')) i++;
			return (STRPTR)i;
		}
		return NULL;
	}
}

STRPTR W_PathPart(STRPTR path, struct globaldata *g)
{
	if (ISDOS20) {
		return PathPart(path);
	} else {
		STRPTR ptr;

		while (*path == '/')
		{
			++path;
		}
		ptr = path;
		while (*ptr)
		{
			if (*ptr == '/')
			{
				path = ptr;
			}
			else if (*ptr == ':')
			{
				path = ptr + 1;
			}
			ptr++;
		}
		return (STRPTR)path;
	}
}

static LONG CMPBSTR(BSTR s1, BSTR s2)
{
	UBYTE *ss1 = BADDR(s1);
	UBYTE *ss2 = BADDR(s2);
	return memcmp(ss1, ss2, ss1[0] + 1);
}
static struct DosList *getdoslist(struct globaldata *g)
{
	struct DosInfo *di;
	
	Forbid();
	di = BADDR(DOSBase->dl_Root->rn_Info);
	return (struct DosList *)&di->di_DevInfo;
}
static void freedoslist(struct globaldata *g)
{
	Permit();
}

struct DosList *W_MakeDosEntry(STRPTR name, LONG type, struct globaldata *g)
{
	ULONG len;
	STRPTR s2;
	struct DosList *dl;

	if (ISDOS20)
		return MakeDosEntry(name, type);

	len = strlen(name);
	dl = (struct DosList *)W_AllocVec(sizeof(struct DosList), MEMF_PUBLIC | MEMF_CLEAR, g);

	if (dl != NULL)
	{
		/* Binary compatibility for BCPL string.
		 * First byte is the length then comes the string.
		 * For ease of use a zero is put at the end so it can be used as a
		 * C string
		 */
		s2 = (STRPTR)W_AllocVec(len+2, MEMF_PUBLIC | MEMF_CLEAR, g);
		dl->dol_Name = MKBADDR(s2);
		if (s2 != NULL)
			*s2++ = (UBYTE)(len > 255 ? 255 : len);
		if (s2 != NULL)
		{
			strcpy(s2, name);
			dl->dol_Type = type;
			return dl;
		}
		W_FreeVec(dl, g);
	}
	return NULL;
}

LONG W_RemDosEntry(struct DosList *dlist, struct globaldata *g)
{
	struct DosList *dl;

	if (ISDOS20)
		return RemDosEntry(dlist);

	if(dlist == NULL)
		return 0;

	dl = getdoslist(g);

	while(dl->dol_Next)
	{
		struct DosList *dl2 = BADDR(dl->dol_Next);

		if(dl2 == dlist)
		{
			dl->dol_Next = dlist->dol_Next;
			break;
		}

		dl = dl2;
	}

	freedoslist(g);

	return 1;
}

void W_FreeDosEntry(struct DosList *dlist, struct globaldata *g)
{
	if (ISDOS20) {
		FreeDosEntry(dlist);
		return;
	}
	if (dlist == NULL)
			return;
	W_FreeVec(BADDR(dlist->dol_Name), g);
	W_FreeVec(dlist, g);
}

LONG W_AddDosEntry(struct DosList *dlist, struct globaldata *g)
{
	LONG success = DOSTRUE;
	struct DosList *dl;

	if (ISDOS20)
		return AddDosEntry(dlist);

	if (dlist == NULL)
			return success;

	D(bug("[AddDosEntry] Adding '%b' type %d from addr %x Task '%s'\n",
		dlist->dol_Name, dlist->dol_Type, dlist,
		FindTask(NULL)->tc_Node.ln_Name));

	dl = getdoslist(g);

	if(dlist->dol_Type != DLT_VOLUME)
	{
		while(TRUE)
		{
			dl = BADDR(dl->dol_Next);

			if(dl == NULL)
				break;

			if(dl->dol_Type != DLT_VOLUME && !CMPBSTR(dl->dol_Name, dlist->dol_Name))
			{
				D(bug("[AddDosEntry] Name clash for %08lx->dol_Name: %b and %08lx->dol_Name %b\n", dl, dl->dol_Name, dlist, dlist->dol_Name));
				success = DOSFALSE;
				break;
			}
		}
	}

	if(success)
	{
		struct DosInfo *dinf = BADDR(DOSBase->dl_Root->rn_Info);

		dlist->dol_Next = dinf->di_DevInfo;
		dinf->di_DevInfo = MKBADDR(dlist);
	}

	freedoslist(g);

	return success;	
}

struct DosList *W_AttemptLockDosList(ULONG flags, struct globaldata *g)
{
	if (ISDOS20) {
		struct DosList *dl = AttemptLockDosList(flags);
		if ((ULONG)dl <= 1) // v36-v39.23 bug workaround
			return NULL;
		return dl;
	} else {
		return getdoslist(g);
	}
}

void W_UnLockDosList(ULONG flags, struct globaldata *g)
{
	if (ISDOS20) {
		UnLockDosList(flags);
	} else {
		freedoslist(g);
	}
}


LONG W_ErrorReport(LONG code, LONG type, ULONG arg1, struct MsgPort *device, struct globaldata *g)
{
	if (ISDOS20 && ISEXEC20)
		return ErrorReport(code, type, arg1, device);
	return FALSE;
}

LONG W_EasyRequestArgs(struct Window *window, struct EasyStruct *easyStruct, ULONG *IDCMP_ptr, APTR argList, struct globaldata *g)
{
	if (ISDOS20 && ISEXEC20)
		return EasyRequestArgs(window, easyStruct, IDCMP_ptr, argList);
	return 1;
}

void FixStartupPacket(struct DosPacket *pkt, struct globaldata *g)
{
	struct DosList *dl;
	
	if (pkt->dp_Arg3)
		return;
	/* Fix 1.3 bad boot time ACTION_STARTUP: dp_Arg3 = NULL, dp_Arg1 = uninitialized data */
	dl = getdoslist(g);
	while(dl->dol_Next)
	{
		dl = BADDR(dl->dol_Next);
		if (dl->dol_Type == DLT_DEVICE && dl->dol_misc.dol_handler.dol_Startup == pkt->dp_Arg2) {
			/* This is safe, Startup Packet is allocated from BCPL stack
			 * that goes away after DOS gets message reply.
			 */
			pkt->dp_Arg1 = dl->dol_Name;
			pkt->dp_Arg3 = MKBADDR(dl);
			break;
		}
	}
	freedoslist(g);
}


/* AROS Pool implementation */

typedef struct Pool
{
  struct MinList PuddleList;
  ULONG MemoryFlags;
  ULONG PuddleSize;
  ULONG ThreshSize;
} POOL;

#define poolHeader ((POOL*)pool)

APTR W_LibCreatePool(ULONG requirements, ULONG puddleSize, ULONG threshSize, struct globaldata *g)
{
	if (ISEXEC30) {
		return CreatePool(requirements, puddleSize, threshSize);
	} else {
		POOL *pool = NULL;
		if (threshSize <= puddleSize)
		{
			if ((pool = (POOL*)AllocMem(sizeof(POOL), MEMF_ANY)) != NULL)
			{
				NewList((struct List*)&pool->PuddleList);
				puddleSize = ((puddleSize + 7) & ~7);
				pool->MemoryFlags = requirements;
				pool->PuddleSize  = puddleSize;
				pool->ThreshSize  = threshSize;
			}
		}
		return (APTR)pool;
	}
}

void W_LibDeletePool(APTR pool, struct globaldata *g)
{
	if (ISEXEC30) {
		DeletePool(pool);
	} else {
		if (poolHeader != NULL)
		{
			ULONG *poolMem, size;
			while ((poolMem = (ULONG*)RemHead((struct List *)&poolHeader->PuddleList))!=NULL)
			{
				size = *--poolMem;
				FreeMem(poolMem, size);
			}
			FreeMem(poolHeader, sizeof(POOL));
		}
	}
}

void W_LibFreePooled(APTR pool, APTR memory, ULONG memSize, struct globaldata *g)
{
	if (ISEXEC30) {
		FreePooled(pool, memory, memSize);
	} else {
    if (poolHeader != NULL && memory != NULL)
    {
			ULONG size, *puddle = (ULONG*)((struct MinNode*)memory - 1) - 1;
			if (poolHeader->ThreshSize>memSize)
			{
				struct MemHeader *a = (struct MemHeader*)&poolHeader->PuddleList.mlh_Head;
				for(;;)
				{
					a = (struct MemHeader*)a->mh_Node.ln_Succ;
					if (a->mh_Node.ln_Succ == NULL)
						return;
					if (a->mh_Node.ln_Type && memory >= a->mh_Lower && memory < a->mh_Upper)
						break;
				}
				Deallocate(a, memory, memSize);
				if (a->mh_Free != poolHeader->PuddleSize)
					return;
				puddle = (ULONG*)&a->mh_Node;
			}
			Remove((struct Node*)puddle);
			size = *--puddle;
			FreeMem(puddle, size);
		}
	}
}

APTR W_LibAllocPooled(APTR pool, ULONG memSize, struct globaldata *g)
{
	if (ISEXEC30) {
		return AllocPooled(pool, memSize);
	} else {
		ULONG *puddle = NULL;
		if (poolHeader != NULL && memSize != 0)
		{
			ULONG size;
			if (poolHeader->ThreshSize > memSize)
			{
				struct MemHeader *a = (struct MemHeader*)poolHeader->PuddleList.mlh_Head;
				for(;;)
				{
					if (a->mh_Node.ln_Succ!=NULL)
					{
						if (a->mh_Node.ln_Type && (puddle = (ULONG*)Allocate(a, memSize)) != NULL)
							break;
						a = (struct MemHeader *)a->mh_Node.ln_Succ;
					}
					else
					{
						size = poolHeader->PuddleSize + sizeof(struct MemHeader) + 2 * sizeof (ULONG);
						if (!(puddle = (ULONG*)AllocMem(size, poolHeader->MemoryFlags)) )
							return NULL;
						*puddle ++ = size;
						a = (struct MemHeader*)puddle;
						a->mh_Node.ln_Type = NT_MEMORY;
						a->mh_Lower = a->mh_First = (struct MemChunk*)((UBYTE*)a + sizeof (struct MemHeader) + sizeof (UBYTE *));
						a->mh_First->mc_Next = NULL;
						a->mh_Free = a->mh_First->mc_Bytes = poolHeader->PuddleSize;
						a->mh_Upper = (char*)a->mh_First + a->mh_Free;
						AddHead((struct List*)&poolHeader->PuddleList, &a->mh_Node);
						puddle = (ULONG*)Allocate(a, memSize);
						break;
					}
				}
				/* We do have to clear memory here. It may have been dirtied by somebody using it beforehand. */
				if (poolHeader->MemoryFlags & MEMF_CLEAR)
				{
					ULONG *p = puddle;
					memSize  += 7;
					memSize >>= 3;
					do
					{
						*p++=0;
						*p++=0;
					} while (--memSize);
				}
			}
			else
			{
				size = memSize + sizeof(struct MinNode) + 2 * sizeof(ULONG);
				if ((puddle = (ULONG *)AllocMem (size, poolHeader->MemoryFlags)))
				{
					*puddle ++ = size;
					AddTail ((struct List*)&poolHeader->PuddleList, (struct Node*)puddle);
					puddle = (ULONG*)((struct MinNode*)puddle + 1);
					*puddle ++ = 0;
				}
			}
		}
		return puddle;
	}
}


#endif
