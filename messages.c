/* $Id: messages.c 1.10 1999/05/14 11:31:34 Michiel Exp Michiel $ */
/* $Log: messages.c $
 * Revision 1.10  1999/05/14  11:31:34  Michiel
 * Long filename support implemented; bugfixes
 *
 * Revision 1.9  1999/03/09  10:31:35  Michiel
 * 00110: release number define
 *
 * Revision 1.8  1998/10/02  07:22:45  Michiel
 * final release 4.2 version
 *
 * Revision 1.7  1998/09/27  11:26:37  Michiel
 * ANODE_ERROR and ANODE_INIT added
 *
 * Revision 1.6  1998/09/03  07:12:14  Michiel
 * versie 17.4
 * bugfixes 118, 121, 123 and superindexblocks and td64 support
 *
 * Revision 1.5  1998/05/31  16:30:38  Michiel
 * version number
 *
 * Revision 1.4  1998/05/30  21:52:34  Michiel
 * Overdue addition
 *
 * Revision 1.3  1998/05/27  20:16:13  Michiel
 * AFS --> PFS2
 *
 * Revision 1.2  1997/11/19  22:14:15  mark
 * Copyright transferred from FLD to GREED
 * Changed name from AFS to PFS-II
 *
 * Revision 1.1  1997/03/03  22:04:04  Michiel
 * Initial revision
 * */

#include <exec/types.h>
#include "versionhistory.doc"

CONST UBYTE AFS_WARNING_MEMORY_MASK[]         = "WARNING:\nAllocated memory doesn't match memorymask";
CONST UBYTE AFS_WARNING_EXPERIMENTAL_DISK[]   = "WARNING:\nExperimental >104G partition / block size support enabled.\n(PBS=%lu,LBS=%lu,RBS=%lu)";
#if LARGE_FILE_SIZE
CONST UBYTE AFS_WARNING_EXPERIMENTAL_FILE[]   = "WARNING:\nExperimental >4G file size support enabled";
#endif

CONST UBYTE AFS_ERROR_DNV_ALLOC_INFO[]        = "ALERT:\nAllocation info not found";
CONST UBYTE AFS_ERROR_DNV_ALLOC_BLOCK[]       = "ALERT:\nAllocation block not found";
CONST UBYTE AFS_ERROR_DNV_WRONG_ANID[]        = "ALERT:\nWrong ablock id %08lX block %lu";
CONST UBYTE AFS_ERROR_DNV_WRONG_DIRID[]       = "ALERT:\nWrong dirblock id %08lX block %lu";
CONST UBYTE AFS_ERROR_DNV_LOAD_DIRBLOCK[]     = "ALERT:\nCould not read directoryblock";
CONST UBYTE AFS_ERROR_DNV_WRONG_BMID[]        = "ALERT:\nWrong bitmap block id %08lX block %lu";
CONST UBYTE AFS_ERROR_DNV_WRONG_INDID[]       = "ALERT:\nWrong index block id %08lX, expected %08lX, block %lu, %ld, %ld";
CONST UBYTE AFS_ERROR_CACHE_INCONSISTENCY[]   = "Cache inconsistency detected\nFinish all disk activity";
CONST UBYTE AFS_ERROR_OUT_OF_BUFFERS[]        = "Out of buffers";
CONST UBYTE AFS_ERROR_MEMORY_POOL[]           = "Couldn't allocate memorypool";
CONST UBYTE AFS_ERROR_PLEASE_FREE_MEM[]       = "Please free some memory";
CONST UBYTE AFS_ERROR_LIBRARY_PROBLEM[]       = "Couldn't open library!";
CONST UBYTE AFS_ERROR_INIT_FAILED[]           = "Initialization failure";
CONST UBYTE AFS_ERROR_READ_OUTSIDE[]          = "Read %ld attempt outside partition! %lu + %lu (%ld - %lu)";
CONST UBYTE AFS_ERROR_WRITE_OUTSIDE[]         = "Write %ld attempt outside partition! %lu + %lu (%ld - %lu)";
CONST UBYTE AFS_ERROR_READ_ERROR[]            = "Read %ld Error %lu on block %lu + %lu%s";
CONST UBYTE AFS_ERROR_WRITE_ERROR[]           = "Write %ld Error %lu on block %lu + %lu%s";
CONST UBYTE AFS_ERROR_READ_DELDIR[]           = "Could not read deldir";
CONST UBYTE AFS_ERROR_DELDIR_INVALID[]        = "Deldir invalid";
CONST UBYTE AFS_ERROR_EXNEXT_FAIL[]           = "ExamineNext failed";
CONST UBYTE AFS_ERROR_DOSLIST_ADD[]           = "DosList add error.\nPlease remove volume";
CONST UBYTE AFS_ERROR_EX_NEXT_FAIL[]          = "ExamineNext failed";
CONST UBYTE AFS_ERROR_NEWDIR_ADDLISTENTRY[]   = "Newdir addlistentry failure";
CONST UBYTE AFS_ERROR_LOAD_DIRBLOCK_FAIL[]    = "Couldn't load dirblock!!";
CONST UBYTE AFS_ERROR_LRU_UPDATE_FAIL[]       = "LRU update failed block %lu, err %ld";
CONST UBYTE AFS_ERROR_UPDATE_FAIL[]           = "Disk update failed";
CONST UBYTE AFS_ERROR_UNSLEEP[]               = "Unsleep error";
#if TD64 == 0 && NSD == 0 && SCSIDIRECT == 0
CONST UBYTE AFS_ERROR_DISK_TOO_LARGE[]		= "Disk too large for this version of PFS3.\nPlease install TD64 or direct-scsi version";
#endif
CONST UBYTE AFS_ERROR_ANODE_ERROR[]			= "Anode index invalid";
CONST UBYTE AFS_ERROR_ANODE_INIT[]            = "Anode initialisation failure";
CONST UBYTE AFS_ERROR_32BIT_ACCESS_ERROR[]    = "TD32 and Direct SCSI access modes failed!\nCan't read block %lu (<4G)\n%s:%ld";

#if VERSION23
CONST UBYTE AFS_ERROR_READ_EXTENSION[]        = "Could not read rootblock extension";
CONST UBYTE AFS_ERROR_EXTENSION_INVALID[]     = "Rootblock extension invalid";
#endif

#ifdef BETAVERSION
/* beta messages */
CONST UBYTE AFS_BETA_WARNING_1[]              = "BETA WARNING NR 1";
CONST UBYTE AFS_BETA_WARNING_2[]              = "BETA WARNING NR 2";
#endif

/*
 * Message shown when formatting a disk
 */

CONST UBYTE FORMAT_MESSAGE[] =
                "Professional File System 3 V" REVISION "\n"
                "      Open Source Version       \n"
                "          based on PFS3         \n"
                "            written by          \n"
                "           Michiel Pelt         \n"
                "      All-In-One version by     \n"
                "            Toni Wilen          \n"
                "     Press mouse to continue    ";

#if DELDIR
CONST UBYTE deldirname[] = "\007.DELDIR";
#endif

