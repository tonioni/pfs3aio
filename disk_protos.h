/* Prototypes for functions defined in
disk.c
 */

//ULONG ReadFromFile(fileentry_t * , UBYTE * , ULONG , ULONG * , globaldata * );
//ULONG WriteToFile(fileentry_t * , UBYTE * , ULONG , ULONG * , globaldata * );
SFSIZE ChangeFileSize(fileentry_t * , SFSIZE , LONG , ULONG * , globaldata * );
ULONG ReadFromObject(fileentry_t * , UBYTE * , ULONG , ULONG * , globaldata * );
ULONG WriteToObject(fileentry_t * , UBYTE * , ULONG , ULONG * , globaldata * );
SFSIZE SeekInObject(fileentry_t * , SFSIZE , LONG , ULONG * , globaldata * );
SFSIZE ChangeObjectSize(fileentry_t * , SFSIZE , LONG , ULONG * , globaldata * );
SFSIZE SeekInFile(fileentry_t *file, SFSIZE offset, LONG mode, ULONG *error, globaldata *g);

ULONG DiskRead(UBYTE * , ULONG , ULONG , globaldata * );

ULONG DiskWrite(UBYTE * , ULONG , ULONG , globaldata * );

ULONG RawRead(UBYTE * , ULONG , ULONG , globaldata * );

ULONG RawWrite(UBYTE * , ULONG , ULONG , globaldata * );

void UpdateAndMotorOff(globaldata * );

void UpdateCache (globaldata *g);
void FlushDataCache (globaldata *g);
void UpdateDataCache (globaldata *g);

BOOL detectaccessmode(UBYTE *buffer, globaldata *g);
BOOL get_scsi_geometry(globaldata *g);
