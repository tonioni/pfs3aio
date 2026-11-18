/*
 * This file forms part of PFS3 (Professional File System 3)
 * Written by Michiel Pelt
 *
 * PFS3 header file
 */

#ifndef _PFS_H
#define _PFS_H 1

#ifndef EXEC_PORTS_H
#include "exec/ports.h"
#endif /* EXEC_PORTS_H */

#include "../pfs3disk.h"

#if defined(__GNUC__) || defined(__VBCC__)
/* Force SAS/C compatible alignment rules for all these structures */
#pragma pack(2)
#endif

/****************************************************************************/
/* PFS3 blocks                                                              */
/****************************************************************************/

union reservedblock
{
    UWORD            id;

    anodeblock_t     anodeblock;
    bitmapblock_t    btmapblock;
    bootblock_t      bootblock;
    dirblock_t       dirblock;
    deldirblock_t    deldirblock;
    indexblock_t     indexblock;
    rootblock_t      rootblock;
    extensionblock_t extensionblock;
};

typedef union reservedblock reservedblock_t;

/* limits */
#define MAXDELDIRSEQNR      31
#define MAXRESBLOCKSIZE 4096

#define SIZEOF_RESBLOCK (rbl->reserved_blksize)

/* disk options */
#define MODE_MASK        (8192-1)

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack()
#endif

#endif /* _PFS_H */
