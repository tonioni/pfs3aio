
#ifdef KSWRAPPER

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

#define KSWRAPPER_DEBUG 0

#include <dos/dosextens.h>
#include <exec/ports.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>

void FixStartupPacket(struct DosPacket *pkt, struct globaldata *g);

/* dos */

#undef MakeDosEntry
#undef RemDosEntry
#undef FreeDosEntry
#undef AddDosEntry
#undef ErrorReport
#undef FilePart
#undef PathPart
#undef UnLockDosList
#undef AttemptLockDosList

#define MakeDosEntry(name, type) W_MakeDosEntry(name, type, g)
#define RemDosEntry(dlist) W_RemDosEntry(dlist, g)
#define FreeDosEntry(dlist) W_FreeDosEntry(dlist, g)
#define AddDosEntry(dlist) W_AddDosEntry(dlist, g)
#define ErrorReport(code, type, arg1, device) W_ErrorReport(code, type, arg1, device, g)
#define FilePart(path) W_FilePart(path, g)
#define PathPart(path) W_PathPart(path, g)
#define UnLockDosList(flags) W_UnLockDosList(flags, g)
#define AttemptLockDosList(flags) W_AttemptLockDosList(flags, g)

STRPTR W_FilePart(STRPTR path, struct globaldata *g);
STRPTR W_PathPart(STRPTR path, struct globaldata *g);
struct DosList *W_MakeDosEntry(STRPTR name, LONG type, struct globaldata *g);
LONG W_RemDosEntry(struct DosList *dlist, struct globaldata *g);
void W_FreeDosEntry(struct DosList *dlist, struct globaldata *g);
LONG W_AddDosEntry(struct DosList *list, struct globaldata *g);
LONG W_ErrorReport(LONG code, LONG type, ULONG arg1, struct MsgPort *device, struct globaldata *g);
void W_UnLockDosList(ULONG flags, struct globaldata *g);
struct DosList *W_AttemptLockDosList(ULONG flags, struct globaldata *g);

/* exec */

#undef AllocVec
#undef FreeVec
#undef CreateIORequest
#undef DeleteIORequest
#undef CreateMsgPort
#undef DeleteMsgPort

#define AllocVec(size, flags) W_AllocVec(size, flags, g)
#define FreeVec(mem) W_FreeVec(mem, g)
#define CreateIORequest(ioReplyPort, size) W_CreateIORequest(ioReplyPort, size, g)
#define DeleteIORequest(iorequest) W_DeleteIORequest(iorequest, g)
#define CreateMsgPort() W_CreateMsgPort(g)
#define DeleteMsgPort(port) W_DeleteMsgPort(port, g)

APTR W_AllocVec(ULONG size, ULONG flags, struct globaldata *g);
void W_FreeVec(APTR mem, struct globaldata *g);
APTR W_CreateIORequest(struct MsgPort *ioReplyPort, ULONG size, struct globaldata *g);
void W_DeleteIORequest(APTR iorequest, struct globaldata *g);
struct MsgPort *W_CreateMsgPort(struct globaldata *g);
void W_DeleteMsgPort(struct MsgPort *port, struct globaldata *g);

/* intuition */

#undef EasyRequestArgs

#define EasyRequestArgs(window, easyStruct, IDCMP_ptr, argList) W_EasyRequestArgs(window, easyStruct, IDCMP_ptr, argList, g)

LONG W_EasyRequestArgs(struct Window *window, struct EasyStruct *easyStruct, ULONG *IDCMP_ptr, APTR argList, struct globaldata *g);

/* pools */

#undef LibCreatePool
#undef LibDeletePool
#undef LibAllocPooled
#undef LibFreePooled

#define LibCreatePool(a,b,c) W_LibCreatePool(a,b,c,g)
#define LibDeletePool(p) W_LibDeletePool(p,g)
#define LibAllocPooled(p,s) W_LibAllocPooled(p,s,g)
#define LibFreePooled(p,m,s) W_LibFreePooled(p,m,s,g)

APTR W_LibCreatePool(ULONG requirements, ULONG puddleSize, ULONG threshSize, struct globaldata *g);
void W_LibDeletePool(APTR pool, struct globaldata *g);
void W_LibFreePooled(APTR pool, APTR memory, ULONG memSize, struct globaldata *g);
APTR W_LibAllocPooled(APTR pool, ULONG memSize, struct globaldata *g);

/* debug */
void DebugPutStr(const char *buff);
void DebugPutDec(const char *what, ULONG val);
void DebugPutHex(const char *what, ULONG val);
void DebugPutHexVal(ULONG val);

#endif
