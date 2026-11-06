/* Prototypes for functions defined in
lru.c
 */

#ifndef MIN_BUFFERS
#define MIN_BUFFERS 10
#endif
#ifndef MAX_BUFFERS
#define MAX_BUFFERS 600
#endif

BOOL InitLRU(globaldata *, UWORD);
LONG ResizeLRU(globaldata *, LONG);
void DeallocLRU(globaldata *);

struct cachedblock * AllocLRU(globaldata * );

void FlushBlock(struct cachedblock * , globaldata * );

void UpdateReference(ULONG , struct cdirblock * , globaldata * );

void UpdateLE(listentry_t * , globaldata * );

void UpdateLE_exa(lockentry_t * , globaldata * );

struct cachedblock * CheckCache(struct MinList * , UWORD , ULONG , globaldata * );

void ResToBeFreed(ULONG blocknr, globaldata *g);
