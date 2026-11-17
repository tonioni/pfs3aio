/* $Id: blocks.h 1.21 1999/06/13 16:46:01 Michiel Exp Michiel $
 * $Log: blocks.h $
 * Revision 1.21  1999/06/13  16:46:01  Michiel
 * MAXNUMRESERVED raised
 *
 * Revision 1.20  1999/05/14  11:31:34  Michiel
 * Long filename support implemented; bugfixes
 *
 * Revision 1.19  1999/03/09  10:40:11  Michiel
 * Deldir extension and bitwise reserved roving
 *
 * Revision 1.18  1998/09/27  11:26:37  Michiel
 * MAXNUMRESERVED increased
 * BETA id changed
 *
 * Revision 1.17  1998/09/03  07:12:14  Michiel
 * version 17.4
 * bugfixes 118, 121, 123 and superindexblocks and td64 support
 *
 * Revision 1.16  1998/08/05  00:33:23  Michiel
 * MAXDISKSIZE, ID_MUPFS_DISK added
 *
 * Revision 1.15  1998/05/29  19:13:27  Michiel
 * Wrongly defined ID_PFS2_DISK
 *
 * Revision 1.14  1998/05/27  22:35:51  Michiel
 * Version 17.1 update
 *
 * Revision 1.12  1998/05/22  22:59:58  Michiel
 * Datestamps added
 *
 * Revision 1.11  1998/05/22  21:00:22  Michiel
 * Added anode roving pointer (overdue checkin)
 *
 * Revision 1.10  1995/12/29  11:01:53  Michiel
 * rollover extrafields added
 * reserved roving stored in rootblockextension
 *
 * Revision 1.9  1995/11/15  15:38:27  Michiel
 * rootblock extension defined
 * cachedblock 'done' field removed
 *
 * Revision 1.8  1995/11/07  17:31:22  Michiel
 * cachedblock changeflag and done now are bytes (instead of words)
 *
 * Revision 1.7  1995/08/04  04:15:30  Michiel
 * added DELENTRY_PROT_AND_MASK and DELENTRY_PROT_OR_MASK
 * added disksize (MODE_SIZEFIELD) to rootblock
 *
 * Revision 1.6  1995/07/28  08:26:55  Michiel
 * FNSIZE replaced by PATHSIZE and FILENAMESIZE
 *
 * Revision 1.5  1995/07/21  06:42:07  Michiel
 * More deldir stuff
 *
 * Revision 1.4  1995/07/11  09:24:38  Michiel
 * DELDIR stuff
 *
 * Revision 1.3  1995/07/10  15:17:13  Michiel
 * ID_MUAF_DISK and ID_AFS_DISK added
 *
 * Revision 1.2  1995/02/28  18:29:28  Michiel
 * extrafields added
 *
 * Revision 1.1  1995/02/15  16:46:45  Michiel
 * Initial revision
 *
 */

#ifndef _BLOCKS_H
#define _BLOCKS_H 1

#ifndef DOS_DOSEXTENS_H
#include <dos/dosextens.h>
#endif
#ifndef DEVICES_TRACKDISK_H
#include <devices/trackdisk.h>
#endif
#ifndef PROTO_EXEC_H
#include <proto/exec.h>
#endif
#ifndef PROTO_DOS_H
#include <proto/dos.h>
#endif
#ifndef _STDDEFH
#include <stddef.h>
#endif

#ifndef _EXTRATYPESH
#include "extratypes.h"
#endif
#ifndef _PFS3DISK_H
#include "pfs3disk.h"
#endif

#if LARGE_FILE_SIZE
// last two bytes used for extended file size
#define DELENTRYFNSIZE 16
#else
#define DELENTRYFNSIZE 18
#endif

typedef struct cbitmapblock
{
	struct cbitmapblock *next;
	struct cbitmapblock *prev;
	struct volumedata *volume;
	ULONG blocknr;
	ULONG oldblocknr;
	UWORD used;
	UBYTE changeflag;
	UBYTE dummy;
	struct bitmapblock blk;
} cbitmapblock_t;

typedef struct cindexblock
{
	struct cindexblock *next;
	struct cindexblock *prev;
	struct volumedata *volume;
	ULONG blocknr;
	ULONG oldblocknr;
	UWORD used;
	UBYTE changeflag;
	UBYTE dummy;
	struct indexblock blk;
} cindexblock_t;

struct canodeblock
{
	struct canodeblock *next;	/* for cache list				*/
	struct canodeblock *prev;
	struct volumedata *volume;
	ULONG  blocknr;				/* for already cached check		*/
	ULONG  oldblocknr;
	UWORD  used;				/* locknr						*/
	UBYTE  changeflag;			/* dirty flag					*/
	UBYTE  dummy;
	struct anodeblock blk;
};

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack(2)
#endif

// Cached Allocation NODE
typedef struct canode
{
	ULONG clustersize;	// number of blocks in a cluster
	ULONG blocknr;		// the block number
	ULONG next;			// next anode (anodenummer), 0 = eof
	ULONG nr;			// the anodenr
} canode_t;

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack()
#endif

struct cdirblock
{
	struct cdirblock *next;		// volume cachelist
	struct cdirblock *prev;	
	struct volumedata *volume;
	ULONG  blocknr;		// voor already cached check
	ULONG  oldblocknr;
	UWORD  used;
	UBYTE  changeflag;	// size is word, could be byte
	UBYTE  dummy;
	struct dirblock blk;
};

struct lru_cdirblock
{
	struct lru_cdirblock *next;
	struct lru_cdirblock *prev;
	struct cdirblock cblk;
};

#if DELDIR
/*
 * del dir
 */
#define MAXDELDIR 31
#define MAXDELDIR_BLOCKS (MAXDELDIR + 1)

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack(2)
#endif

struct deldirentry
{
	ULONG anodenr;			/* anodenr							*/
	ULONG fsize;			/* size of file						*/
	UWORD creationday;		/* datestamp						*/
	UWORD creationminute;
	UWORD creationtick;
	UBYTE filename[18];		/* last two bytes hold fsizex on large-file volumes */
};

struct deldirblock
{
	UWORD id;				/* 'DD'								*/
	UWORD not_used;
	ULONG datestamp;
	ULONG seqnr;
	UWORD not_used_2[2];
	UWORD not_used_3;		/* roving in older versions	(<17.9)	*/	
	UWORD uid;				/* user id							*/
	UWORD gid;				/* group id							*/
	ULONG protection;
	UWORD creationday;
	UWORD creationminute;
	UWORD creationtick;
	struct deldirentry entries[0];	/* DELENTRIES_PER_BLOCK entries	*/
};

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack()
#endif

struct cdeldirblock
{
	struct cdeldirblock *next;	/* currently not used 			*/
	struct cdeldirblock *prev;	/* currently not used			*/
	struct volumedata *volume;
	ULONG blocknr;
	ULONG oldblocknr;
	UWORD used;
	UBYTE changeflag;
	UBYTE dummy;
	struct deldirblock blk;
};

#endif

#if VERSION23

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack(2)
#endif

struct postponed_op
{
	ULONG operation_id;		/* which operation is postponed */
	ULONG argument1;		/* operation arguments, e.g. number of blocks */
	ULONG argument2;
	ULONG argument3;
};

struct rootblockextension
{
	UWORD id;					/* id ('EX') */
	UWORD not_used_1;
	ULONG ext_options;
	ULONG datestamp;
	ULONG pfs2version;			/* pfs2 revision under which the disk was formatted */
	UWORD root_date[3];			/* root directory datestamp */
	UWORD volume_date[3];		/* volume datestamp */
	struct postponed_op tobedone;	/* postponed operation (curr. only delete) */
	ULONG reserved_roving;		/* reserved roving pointer */
	UWORD rovingbit;			/* bitnr in rootblock->roving_ptr bitmap field */
	UWORD curranseqnr;			/* anodeallocation roving pointer */
	UWORD deldirroving;			/* deldir roving pointer */
	UWORD deldirsize;			/* size of deldir */
	UWORD fnsize;				/* filename size (18.1) */
	UWORD not_used_2[3];
	ULONG superindex[MAXSUPER + 1];		/* MODE_SUPERINDEX only. offset=64 $40 */
	UWORD dd_uid;				/* deldir user id (17.9)			*/
	UWORD dd_gid;				/* deldir group id					*/
	ULONG dd_protection;		/* deldir protection				*/
	UWORD dd_creationday;		/* deldir datestamp					*/
	UWORD dd_creationminute;
	UWORD dd_creationtick;
	UWORD not_used_3;
	ULONG deldir[32];			/* 32 deldir blocks					*/
	ULONG dosenvec[17];			/* DosEnvec when formatted          */
};

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack()
#endif

struct crootblockextension
{
	struct crootblockextension *next;	/* currently not used */
	struct crootblockextension *prev;	/* currently not used */
	struct volumedata *volume;
	ULONG blocknr;
	ULONG oldblocknr;
	UWORD used;
	UBYTE changeflag;
	UBYTE dummy;
	struct rootblockextension blk;
};

typedef struct crootblockextension crootblockextension_t;

/* postponed operations operation_id's */
#define PP_FREEBLOCKS_FREE 1
#define PP_FREEBLOCKS_KEEP 2
#define PP_FREEANODECHAIN 3

/* */
#endif

/* Cached blocks in general
*/
struct cachedblock
{
	struct cachedblock	*next;
	struct cachedblock	*prev;
	struct volumedata	*volume;
	ULONG	blocknr;				// the current (new) blocknumber of the block
	ULONG	oldblocknr;				// the blocknr before reallocation. NULL if not reallocated.
	UWORD	used;					// block locked if used == g->locknr
	UBYTE	changeflag;				// dirtyflag
	UBYTE	dummy;					// pad to make offset even
	UBYTE	data[0];				// the datablock;
};

/* Dirty contents whose copy-on-write relocation still needs to be linked. */
#define CBLK_DIRTY_PENDING 2

struct lru_cachedblock
{
	struct lru_cachedblock *next;
	struct lru_cachedblock *prev;
	struct cachedblock cblk;
};


/* max length of filename, diskname and comment
 * FNSIZE is 108 for compatibilty. Used for searching
 * files.
 */
#define FNSIZE 108
#define PATHSIZE 256
#define FILENAMESIZE (g->fnsize)
#define CMSIZE 80
#define MAX_ENTRYSIZE (sizeof(struct direntry) + FNSIZE + CMSIZE + 34)

/* macros on cachedblocks */
#define IsDirBlock(blk) (((UWORD *)(blk->data))[0] == DBLKID)
#define IsAnodeBlock(blk) (((UWORD *)(blk->data))[0] == ABLKID)
#define IsIndexBlock(blk) (((UWORD *)(blk->data))[0] == IBLKID)
#define IsBitmapBlock(blk) (((UWORD *)(blk->data))[0] == BMBLKID)
#define IsBitmapIndexBlock(blk) (((UWORD *)(blk->data))[0] == BMIBLKID)
#define IsDeldir(blk) (((UWORD *)(blk->data))[0] == DELDIRID)
#define IsSuperBlock(blk) (((UWORD *)(blk->data))[0] == SBLKID)

/* size of reserved blocks in bytes and blocks
 * place you can find rootblock
 */
#define SIZEOF_RESBLOCK (g->rootblock->reserved_blksize)
#define SIZEOF_CACHEDBLOCK (sizeof(struct cachedblock) + SIZEOF_RESBLOCK)
#define SIZEOF_LRUBLOCK (sizeof(struct lru_cachedblock) + SIZEOF_RESBLOCK)
#define RESCLUSTER (g->currentvolume->rescluster)
#define BOOTBLOCK1 0
#define BOOTBLOCK2 1
#define ROOTBLOCK 2

/* Longs per bitmapblock */
#define LONGS_PER_BMB ((g->rootblock->reserved_blksize/4)-3) // 253/509/1021

#define DB_HEADSPACE (sizeof(struct dirblock))
#define DB_ENTRYSPACE (SIZEOF_RESBLOCK - sizeof(struct dirblock))

/* direntry macros */
// comment: de is struct direntry *
#define COMMENT(de) ((UBYTE*)(&((de)->startofname) + (de)->nlength))
/* DIRENTRYNAME: DSTR of name. */
#define DIRENTRYNAME(de) ((UBYTE*)(&(de->nlength)))
// get first direntry; blok is dirblock
#define FIRSTENTRY(blok) ((struct direntry*)((blok)->blk.entries))
#define ENTRYLEN(de, comment) ((sizeof(struct direntry) + (de)->nlength + strlen(comment))&0xfffe)

#if DELDIR
#define DELENTRY_PROT 0x0005
#define DELENTRY_PROT_AND_MASK 0xaa0f
#define DELENTRY_PROT_OR_MASK 0x0005
/* maximum number of entries per block, max deldirblock seqnr */
#endif /* DELDIR */


/* Max size reported in DOS ULONG size fields */
#define MAXFILESIZE32 0xffffffff

#endif /* _BLOCKS_H */
