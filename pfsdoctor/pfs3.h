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

typedef struct rootblockextension
{
    UWORD id;                   /* id ('EX') */
    UWORD not_used_1;
    ULONG ext_options;
    ULONG datestamp;
    ULONG pfs2version;          /* pfs2 format revision */
    UWORD root_date[3];         /* root directory datestamp */
    UWORD volume_date[3];       /* volume datestamp */
    ULONG postponed_op[4];      /* postponed operation (private) */
    ULONG reserved_roving;      /* reserved roving pointer */
    UWORD rovingbit;            /* bitnr in rootblock->roving_ptr field */
    UWORD curranseqnr;          /* anode allocation roving pointer  */
    UWORD deldirroving;         /* (5.1) deldir roving pointer */
    UWORD deldirsize;           /* (5.1) size of deldir in blocks, 0 is disabled */
    UWORD fnsize;				/* (5.1) filename size */
    UWORD not_used_2[3];
    ULONG superindex[16];       /* (4.2) MODE_SUPERINDEX only */
    UWORD dd_uid;               /* (5.1) deldir user id (17.9) */
    UWORD dd_gid;               /* (5.1) deldir group id */
    ULONG dd_protection;        /* (5.1) deldir protection */
    UWORD dd_creationday;       /* (5.1) deldir datestamp */
    UWORD dd_creationminute;
    UWORD dd_creationtick;
    UWORD not_used_3;
    ULONG deldir[32];           /* (5.1) 32 deldir blocks */
    ULONG not_used_4[188];
} extensionblock_t;

struct deldirentry
{
    ULONG anodenr;          /* anodenr */
    ULONG fsize;            /* size of file */
    UWORD creationday;      /* datestamp */
    UWORD creationminute;
    UWORD creationtick;
    UBYTE filename[16];     /* filename; filling up to 30 chars */
    // was previously filename[18]
    // now last two bytes used for extended file size
    UWORD fsizex;           /* extended bits 32-47 of fsize		*/
};

typedef struct deldirblock
{
    UWORD id;               /* 'DD' */
    UWORD not_used;
    ULONG datestamp;
    ULONG seqnr;
    UWORD not_used_2[3];
    UWORD uid;              /* user id */
    UWORD gid;              /* group id */
    ULONG protection;
    UWORD creationday;
    UWORD creationminute;
    UWORD creationtick;
    struct deldirentry entries[0];  /* 31 entries */
} deldirblock_t;

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
