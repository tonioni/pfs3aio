/* Prototypes for functions defined in
allocation.c
 */

void InitAllocation (struct volumedata * , globaldata * );

BOOL AllocateBlocks (ULONG , ULONG , globaldata * );
BOOL AllocateBlocksAC (struct anodechain *achain, ULONG size, struct fileinfo *ref, globaldata *g);
BOOL FreeBlocksAC (struct anodechain *achain, ULONG size, enum freeblocktype freetype, globaldata *g);
BOOL UpdateFreeList(globaldata * );
BOOL ValidateFreeList(globaldata * );
void CommitFreeList(globaldata * );
void UndoFreeList(globaldata * );

ULONG AllocReservedBlock(globaldata * );
ULONG AllocReservedBlockSave(globaldata * );
void FreeReservedBlock(ULONG , globaldata * );

cindexblock_t * GetBitmapIndex(UWORD , globaldata * );
struct cbitmapblock * NewBitmapBlock(ULONG seqnr , globaldata * );
struct cbitmapblock *GetBitmapBlock (ULONG seqnr, globaldata *g);
