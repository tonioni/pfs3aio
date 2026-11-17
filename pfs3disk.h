/*
 * PFS3 on-disk format shared by the handler and pfsdoctor: block layouts,
 * block and disk ids, limits, disk options and predefined anodes.
 * No build options: everything here is the same in every build.
 */

#ifndef _PFS3DISK_H
#define _PFS3DISK_H 1

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

/* limits */
#define MAXSMALLBITMAPINDEX 4
#define MAXBITMAPINDEX 103
// was 28576. was 119837. Nu max reserved bitmap 256K.
#define MAXNUMRESERVED (4096 + 255*1024*8)
#define MAXSUPER 15
#define MAXSMALLINDEXNR 98
/* maximum disksize in sectors, limited by number of bitmapindexblocks
 * smalldisk = 10.241.440 blocks of 512 byte = 5G
 * normaldisk = 213.021.952 blocks of 512 byte = 104G
 * 2k reserved blocks = 104*509*509*32 blocks of 512 byte = 411G
 * 4k reserved blocks = 1,6T
 *  */
#define BITMAP_PAYLOAD_1K ((1024/4)-3) // 253
#define BITMAP_PAYLOAD_2K ((2048/4)-3) // 509
#define BITMAP_PAYLOAD_4K ((4096/4)-3) // 1021

#define MAXSMALLDISK ((MAXSMALLBITMAPINDEX+1)*BITMAP_PAYLOAD_1K*BITMAP_PAYLOAD_1K*32)
#define MAXDISKSIZE1K ((MAXBITMAPINDEX+1)*BITMAP_PAYLOAD_1K*BITMAP_PAYLOAD_1K*32)
#define MAXDISKSIZE2K ((MAXBITMAPINDEX+1)*BITMAP_PAYLOAD_2K*BITMAP_PAYLOAD_2K*32)
#define MAXDISKSIZE4K ((ULONG)(MAXBITMAPINDEX+1)*BITMAP_PAYLOAD_4K*BITMAP_PAYLOAD_4K*32)
#define MAXDISKSIZE MAXDISKSIZE4K

#define DNSIZE 32
#define DELENTRIES_PER_BLOCK 31

#if defined(__GNUC__) || defined(__VBCC__)
/* Force SAS/C compatible alignment rules for all these structures */
#pragma pack(2)
#endif

typedef struct bootblock
{
    LONG disktype;          /* PFS\1                            */
    UBYTE reserved[508];
} bootblock_t;

typedef struct rootblock
{
    LONG disktype;
    ULONG options;          /* bit 0 is harddisk mode           */
    ULONG datestamp;        /* current datestamp */
    UWORD creationday;      /* days since Jan. 1, 1978 (like ADOS; WORD instead of LONG) */
    UWORD creationminute;   /* minutes past modnight            */
    UWORD creationtick;     /* ticks past minute                */
    UWORD protection;       /* protection bits (ala ADOS)       */
    UBYTE diskname[DNSIZE]; /* disk label (pascal string)       */
    ULONG lastreserved;     /* reserved area. sector number of last reserved block */
    ULONG firstreserved;	/* sector number of first reserved block */
    ULONG reserved_free;    /* number of reserved blocks (blksize blocks) free  */
    UWORD reserved_blksize; /* size of reserved blocks in bytes */
    UWORD rblkcluster;      /* number of sectors in rootblock, including bitmap  */
    ULONG blocksfree;       /* blocks free                      */
    ULONG alwaysfree;       /* minimum number of blocks free    */
    ULONG roving_ptr;       /* current LONG bitmapfield nr for allocation       */
    ULONG deldir;           /* deldir location (<= 17.8)        */
    ULONG disksize;         /* disksize in sectors              */
    ULONG extension;        /* rootblock extension (16.4)       offset=88 $58 */
    ULONG not_used;
    union
    {
        // UWORD anodeblocks[208];         /* SMALL: 200*84 = 16800 anodes */
        struct
        {
            ULONG bitmapindex[MAXSMALLBITMAPINDEX + 1];       /* 5 bitmap indexblocks with 253 bitmap blocks each */
            ULONG indexblocks[MAXSMALLINDEXNR + 1];      /* 99 index blocks with 253 (more if reserved blocks > 1K) anode blocks each       */
        } small;
        struct
        {
            ULONG bitmapindex[MAXBITMAPINDEX + 1];		/* 104 bitmap indexblocks = max 104 G*/
        } large;
    } idx;
} rootblock_t;

/* structure for both normal as reserved bitmap
 * normal: normal clustersize
 * reserved: directly behind rootblock. As long as necessary
 */
typedef struct bitmapblock
{
    UWORD id;               /* BM (bitmap block)                */
    UWORD not_used;
    ULONG datestamp;
    ULONG seqnr;
    ULONG bitmap[0];        /* the bitmap.                      */
} bitmapblock_t;

typedef struct indexblock
{
    UWORD id;               /* AI or BI (anode- bitmap index)   */
    UWORD not_used;
    ULONG datestamp;
    ULONG seqnr;
    LONG index[0];          /* the indices                      */
} indexblock_t;

typedef struct anode
{
    ULONG clustersize;
    ULONG blocknr;
    ULONG next;
} anode_t;

typedef struct
{
	UWORD seqnr;
	UWORD offset;
} anodenr_t;

typedef struct anodeblock
{
    UWORD id;               /* AB                               */
    UWORD not_used;
    ULONG datestamp;
    ULONG seqnr;
    ULONG not_used_2;
    struct anode nodes[0];
} anodeblock_t;

typedef struct dirblock
{
    UWORD id;               /* 'DB'                             */
    UWORD not_used;
    ULONG datestamp;
    UWORD not_used_2[2];
    ULONG anodenr;          /* anodenr belonging to this directory (points to FIRST block of dir) */
    ULONG parent;           /* parent                           */
    UBYTE entries[0];       /* entries                          */
} dirblock_t;

struct direntry
{
    UBYTE next;             /* sizeof direntry                  */
    BYTE  type;             /* dir, file, link etc              */
    ULONG anode;            /* anode number                     */
    ULONG fsize;            /* sizeof file                      */
    UWORD creationday;      /* days since Jan. 1, 1978 (like ADOS; WORD instead of LONG) */
    UWORD creationminute;   /* minutes past modnight            */
    UWORD creationtick;     /* ticks past minute                */
    UBYTE protection;       /* protection bits (like DOS)       */
    UBYTE nlength;          /* lenght of filename               */
    UBYTE startofname;      /* filename, followed by filenote length & filenote */
    UBYTE pad;              /* make size even                   */
};

struct extrafields
{
	ULONG link;				/* link anodenr						*/
	UWORD uid;				/* user id							*/
	UWORD gid;				/* group id							*/
	ULONG prot;				/* byte 1-3 of protection			*/
	// rollover fields
	ULONG virtualsize;		/* virtual rollover filesize in bytes (as shown by Examine()) */
	ULONG rollpointer;		/* current start of file AND end of file pointer */
	// extended file size
	UWORD fsizex;           /* extended bits 32-47 of direntry.fsize */
};

#if defined(__GNUC__) || defined(__VBCC__)
#pragma pack()
#endif

/* disk id 'PFS\1'  */
//#ifdef BETAVERSION
//#define ID_PFS_DISK		(0x42455441L)	/*	'BETA'	*/
//#else
#define ID_PFS_DISK		(0x50465301L)	/*  'PFS\1' */
//#endif
#define ID_BUSY			(0x42555359L)	/*	'BUSY'  */
#define ID_MUAF_DISK	(0x6d754146L)	/*	'muAF'	*/
#define ID_MUPFS_DISK	(0x6d755046L)	/*	'muPF'	*/
#define ID_AFS_DISK		(0x41465301L)	/*	'AFS\1' */
#define ID_PFS2_DISK	(0x50465302L)	/*	'PFS\2'	*/
#define ID_AFS_USER_TEST (0x41465355L)	/*	'AFSU'	*/

/* block id's		*/
#define DBLKID 0x4442
#define ABLKID 0x4142
#define IBLKID 0x4942
#define BMBLKID 0x424D
#define BMIBLKID 0x4D49
#define DELDIRID 0x4444
#define EXTENSIONID 0x4558	/* 'EX' */
#define SBLKID 0x5342	/* 'SB' */

/* get filenote from directory entry */
#define FILENOTE(de) ((UBYTE*)(&((de)->startofname) + (de)->nlength))
// get a pointer to the next direntry; de = struct direntry *
#define NEXTENTRY(de) ((struct direntry*)((UBYTE*)(de) + (de)->next))

/* disk options */
#define MODE_HARDDISK	1
#define MODE_SPLITTED_ANODES 2
#define MODE_DIR_EXTENSION 4
#define MODE_DELDIR 8
#define MODE_SIZEFIELD 16
// rootblock extension
#define MODE_EXTENSION 32
// if enabled the datestamp was on at format time (!)
#define MODE_DATESTAMP 64
#define MODE_SUPERINDEX 128
#define MODE_SUPERDELDIR 256
#define MODE_EXTROVING 512
#define MODE_LONGFN 1024
#define MODE_LARGEFILE 2048
// Geometry when formatted. "Superfloppy" removable support.
#define MODE_STORED_GEOM 4096

#define DELENTRY_SEP '@'
#define ST_ROLLOVERFILE -16

/* predefined anodes */
#define ANODE_EOF			0
#define ANODE_RESERVED_1	1	// not used by MODE_BIG
#define ANODE_RESERVED_2	2	// not used by MODE_BIG
#define ANODE_RESERVED_3	3	// not used by MODE_BIG
#define ANODE_BADBLOCKS		4	// not used yet
#define ANODE_ROOTDIR		5
#define ANODE_USERFIRST		6
#define MAX_ANODE_SEQNR		0xffffUL

#endif /* _PFS3DISK_H */
