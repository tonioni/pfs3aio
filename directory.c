/* $Id: directory.c 15.21 1999/09/11 17:05:14 Michiel Exp Michiel $ */
/* $Log: directory.c $
 * Revision 15.21  1999/09/11  17:05:14  Michiel
 * bugfix version 18.4
 *
 * Revision 15.20  1999/05/14  11:31:34  Michiel
 * Long filename support implemented; bugfixes
 *
 * Revision 15.19  1999/05/10  23:53:29  Michiel
 * hardlink bug (ST_LINKFILE/ST_LINKDIR) fixed
 *
 * Revision 15.18  1999/04/25  21:48:19  Michiel
 * bug 00141 fixed: hardlink overwrite problem in NewFile()
 *
 * Revision 15.17  1999/04/23  11:12:39  Michiel
 * optimisation: delfile is deleted when detected to be empty (during dirscan)
 *
 * Revision 15.16  1999/03/23  06:00:19  Michiel
 * More informative error messages in setdeldir
 * Maxdeldir bug in setdeldir fixed
 *
 * Revision 15.15  1999/03/09  10:34:02  Michiel
 * typo in mufs code
 *
 * Revision 15.14  1999/02/22  16:31:55  Michiel
 * Changes for increasing deldir capacity
 * Fixed link problems; new function MoveLink
 *
 * Revision 15.13  1998/09/27  11:26:37  Michiel
 * ErrorMsg param
 *
 * Revision 15.12  1998/09/03  07:12:14  Michiel
 * versie 17.4
 * bugfixes 118, 121, 123 and superindexblocks and td64 support
 *
 * Revision 15.11  1997/03/03  22:04:04  Michiel
 * Release 16.21
 *
 * Revision 15.10  1996/04/22  22:12:41  Michiel
 * NewFile overwrite bug fix
 * SetDate deldirfile bug fix
 *
 * Revision 15.9  1996/03/14  19:30:52  Michiel
 * The connect anode for directory anode allocation now is the second anode,
 * not the head anode --> better large directory performance
 * NewFile does not call SearchInDir anymore --> better (large) dir perf.
 * (see log 15.3.96)
 *
 * Revision 15.8  1996/03/07  10:09:09  Michiel
 * rename bug fixed (wt)
 *
 * Revision 15.7  1996/01/30  12:47:46  Michiel
 * --- working tree overlap ---
 * Softlink bug fixes
 * Notify-update changes
 *
 * Revision 15.6  1995/12/29  11:02:43  Michiel
 * SetRollover extended
 *
 * Revision 15.4  1995/12/21  12:00:51  Michiel
 * bugfix: newfile didn't clear extrafields/filetype
 * Now using FreeBlockAC directly instead of buggy FreeAnodeBlocksAC
 *
 * Revision 15.3  1995/12/20  11:54:19  Michiel
 * indented
 *
 * Revision 15.2  1995/12/07  15:25:38  Michiel
 * rollover support and bugfixes
 *
 * Revision 15.1  1995/11/15  15:41:17  Michiel
 * Postponed operation support:
 * AllocDeldirSlot, AddToDeldir, FreeAnodeBlocks, NewFile, DeleteObject, FreeAnodesInChain
 * Rootblock extension: dates in Touch, Examine
 *
 * Revision 14.12  1995/11/07  14:54:13  Michiel
 * Checks for ReservedAreaLock where needed
 *
 * Revision 14.11  1995/10/20  10:08:37  Michiel
 * Anode reserved area adaptions (16.3)
 *
 * Revision 14.10  1995/10/04  08:58:17  Michiel
 * FreeAnodeBlocks nu met anodechains geimplementeerd (FAB kan nu falen!).
 *
 * Revision 14.9  1995/10/03  10:59:37  Michiel
 * merged
 *
 * Revision 14.8  1995/09/28  12:18:25  Michiel
 * soft/hardlink namelength check added
 * Deldir now frees oldblocknr of dirty member blocks
 *
 * Revision 14.7  1995/08/24  13:01:27  Michiel
 * Touch now doesn't touch rootblock because that gives
 * disk recognition problems when changing disks
 *
 * Revision 14.6  1995/08/21  04:18:49  Michiel
 * RenameAndMove didn't check if source was deldir
 *
 * Revision 14.5  1995/08/16  14:31:34  Michiel
 * added KillEmpty
 * added forced_RemoveDirEntry
 *
 * Revision 14.4  1995/08/04  04:17:09  Michiel
 * SetProtection on deldir now allowed (using masks).
 * SetOwner on deldir now allowed
 * Touching deldir when file added to deldir
 * Now using protection field of deldir for all deldirentries
 *
 * Revision 14.3  1995/08/02  16:09:39  Michiel
 * Filename size limitation (31 characters)
 * Empty filenames not accepted
 *
 * Revision 14.2  1995/07/28  07:58:01  Michiel
 * Deldirentry valid check added
 *
 * Revision 14.1  1995/07/21  06:43:33  Michiel
 * DELDIR implemented
 * fixed ExNext problem (?)
 * NewDir now does a diskwp. check
 * Touch() now updates rootdir too
 *
 * Revision 13.11  1995/07/11  17:29:31  Michiel
 * ErrorMsg () calls use messages.c variables now.
 *
 * Revision 13.10  1995/07/11  09:23:36  Michiel
 * DELDIR stuff
 *
 * Revision 13.9  1995/07/07  14:41:21  Michiel
 * no fib->fib_Reserved stuff
 * ProtectFile fix
 *
 * Revision 13.8  1995/06/23  17:26:57  Michiel
 * MakeDirEntry now also sets protection to default and owner to current (multiuser)
 *
 * Revision 13.7  1995/06/21  08:13:18  Michiel
 * CheckVolume() calls removed (moved to dostohandlerinterface.c)
 *
 * Revision 13.6  1995/06/20  18:55:00  Michiel
 * fixed a bug in FreeAnodeBlocks
 * Examine checks softprotect
 *
 * Revision 13.5  1995/06/15  18:55:28  Michiel
 * pooled mem
 *
 * Revision 13.4  1995/06/08  11:12:32  Michiel
 * Now GetFullPath checks if path is directory.
 *
 * Revision 13.3  1995/06/04  07:43:54  Michiel
 * Longword protection implemented
 *
 * Revision 13.2  1995/05/03  19:14:13  Michiel
 * LOCK added to create soft & hardlink
 *
 * Revision 13.1  1995/04/23  09:06:53  Michiel
 * Rewritten direntry change routines,
 * solving directory-order problem
 *
 * Revision 12.9  1995/04/18  17:08:16  Michiel
 * CopyMem() to memcpy() and some cosmetic changes
 *
 * Revision 12.8  1995/04/14  10:03:53  Michiel
 * 'Out of buffers' problem fixed.
 *
 * Revision 12.7  1995/04/05  12:24:43  Michiel
 * Bugfix in ExamineAll
 * Don't allow hardlinks when no dirextension
 *
 * Revision 12.6  1995/03/30  11:53:46  Michiel
 * Rename now does a notify to sourcedir if rename across dirs
 *
 * Revision 12.5  1995/03/30  10:50:37  Michiel
 * some notify stuff added
 *
 * Revision 12.4  1995/03/24  16:29:57  Michiel
 * Softlink support added
 * Hardlink bugs fixed
 *
 * Revision 12.3  1995/02/28  18:20:49  Michiel
 * Link handling functions added: DeleteLink(), RemapLinks(), UpdateLinks(),
 * FetchObject(), UpdateLinkDir().
 * NewFile() takes over old direntry now, instead of delete/create.
 * GetDir() resolves links.
 *
 * Revision 12.2  1995/02/24  07:12:41  Michiel
 * CreateLink added, MakeDirEntry changed
 *
 * Revision 12.1  1995/02/23  06:57:01  Michiel
 * Directory extension implemented
 *
 * Revision 11.5  1995/02/15  16:43:39  Michiel
 * Release version
 * Using new headers (struct.h & blocks.h)
 *
 * Revision 11.4  1995/01/29  07:34:57  Michiel
 * Bug in FreeAnodeBlocks fixed
 *
 * Revision 11.3  1995/01/24  09:49:51  Michiel
 * Cache hashing added
 *
 * Revision 11.2  1995/01/18  04:29:34  Michiel
 * Bugfixes. Now ready for beta release.
 *
 * Revision 11.1  1995/01/08  16:16:29  Michiel
 * Compiled
 *
 * Revision 10.4  1994/11/15  17:52:30  Michiel
 * GuruBook update
 *
 * Revision 10.3  1994/10/28  06:06:40  Michiel
 * uses new listentry field anodenr
 *
 * Revision 10.2  1994/10/27  11:29:33  Michiel
 * *** empty log message ***
 *
 * Revision 10.1  1994/10/24  11:16:28  Michiel
 * first RCS revision
 * */

// anodenr in cdirblock !!! @->bugnr @@->currently wrong
//#define DEBUG 1
#define __USE_SYSBASE

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <dos/exall.h>
#include <dos/filehandler.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#if MULTIUSER
#include <proto/multiuser.h>
#endif
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

// own includes
#include "debug.h"
#include "blocks.h"
#include "struct.h"
#include "directory_protos.h"
#include "allocation_protos.h"
#include "volume_protos.h"
#include "lock_protos.h"
#include "disk_protos.h"
#include "anodes_protos.h"
#include "update_protos.h"
#include "lru_protos.h"
#include "ass_protos.h"
#include "support.c"

#include "kswrapper.h"

void PFSDoNotify(struct fileinfo *object, BOOL checkparent, globaldata * g);
void PFSUpdateNotify(ULONG dirnode, DSTR filename, ULONG anodenr, globaldata * g);

/**********************************************************************/
/*                               DEBUG                                */
/**********************************************************************/

#ifdef DEBUG
extern BOOL debug;
static UBYTE debugbuf[120];
#define DebugOn debug++
#define DebugOff debug=0
#define DebugMsg(msg) if(debug) {NormalErrorMsg(msg, NULL);debug=0;}
#define DebugMsgNum(msg, num) sprintf(debugbuf, "%s 0x%08lx.", msg, num); \
			if(debug) {NormalErrorMsg(debugbuf, NULL);debug=0;}
#define DebugMsgName(msg, name) sprintf(debugbuf, "%s >%s<.", msg, name); \
			if(debug) {NormalErrorMsg(debugbuf, NULL);debug=0;}
#else
#define DebugOn
#define DebugOff
#define DebugMsg(m)
#define DebugMsgNum(msg,num)
#define DebugMsgName(msg, name)
#endif

/*
 * Contents
 */
static BOOL GetParentOf(union objectinfo *path, SIPTR *error, globaldata *);
static BOOL GetDir(STRPTR dirname, union objectinfo *path, SIPTR *error, globaldata *);
static BOOL GetObject(STRPTR objectname, union objectinfo *path, SIPTR *error, globaldata *);
static LONG SearchInDir(ULONG dirnodenr, STRPTR objectname, union objectinfo *info, globaldata * g);
static BOOL FillFib(struct direntry *direntry, struct cdirblock *dirblock, struct FileInfoBlock *fib, FSIZE *size64, globaldata * g);
#if DELDIR
static struct deldirentry *SearchInDeldir(STRPTR delname, union objectinfo *result, SIPTR *error, globaldata *g);
static BOOL IsDelfileValid(struct deldirentry *dde, struct cdeldirblock *ddblk, SIPTR *error, globaldata *g);
static LONG BlockTaken(struct canode *anode, globaldata *g);
static BOOL FillDelfileFib(struct deldirentry *dde, ULONG slotnr, struct FileInfoBlock *fib, globaldata *g);
static struct deldirentry *GetDeldirEntry(IPTR *nr, SIPTR *error, globaldata *g);
static ULONG FillInDDEData(struct ExAllData *buffer, LONG type, struct deldirentry *dde, ULONG ddenr, ULONG spaceleft, globaldata *g);
static struct cdeldirblock *GetDeldirBlock(ULONG seqnr, globaldata *g);
#endif
static BOOL GetFirstEntry(lockentry_t *, globaldata *);
static BOOL GetFirstNonEmptyDE(ULONG anodenr, struct fileinfo *, ULONG *, globaldata *);
static void RemakeNextEntry(lockentry_t *, struct FileInfoBlock *, globaldata *);
static ULONG FillInData(struct ExAllData *, LONG, struct direntry *, ULONG, SIPTR *, globaldata *);
static BOOL DeleteDir(union objectinfo *, SIPTR *, globaldata *);
static BOOL DirIsEmpty(ULONG, SIPTR *, globaldata *);
static BOOL MakeDirEntry(BYTE type, UBYTE *name, UBYTE *entrybuffer, globaldata * g);
static BOOL IsChildOf(union objectinfo child, union objectinfo parent, SIPTR *error, globaldata * g);
static BOOL SetDirParent(ULONG dirnodenr, ULONG parentnr, globaldata * g);
static BOOL DeleteLink(struct fileinfo *link, SIPTR *, globaldata * g);
static LONG RemapLinks(struct fileinfo *object, globaldata * g);
static BOOL UpdateLinkDir(struct direntry *object, ULONG newdiran, globaldata * g);
static BOOL MoveLink(struct direntry *object, ULONG newdiran, globaldata *g);
static void RenameInPlace(struct fileinfo from, struct direntry *to, struct fileinfo *result, globaldata * g);
static struct direntry *CheckFit(struct cdirblock *blok, int needed, globaldata * g);
static BOOL MoveToPrevious(struct fileinfo de, struct direntry *to, struct fileinfo *result, globaldata * g);
static BOOL RenameWithinDir(struct fileinfo from, struct direntry *to, struct fileinfo *result, globaldata * g);
static BOOL RenameAcrossDirs(struct fileinfo from, struct direntry *to, union objectinfo *destdir, struct fileinfo *result, globaldata * g);
static void RemoveDirEntry(struct fileinfo info, globaldata * g);
static BOOL CutDirEntry(struct fileinfo info, globaldata * g);
static BOOL AddDirectoryEntry(union objectinfo *dir, struct direntry *newentry, struct fileinfo *newinfo, globaldata * g);
static BOOL UpdateChangedRef(struct fileinfo from, struct fileinfo *to, int diff, globaldata * g);

#if VERSION23
static int AllocDeldirSlot(globaldata * g);
static BOOL AddToDeldir(union objectinfo *info, int ddnr, globaldata * g);
#else
static void AddToDeldir(union objectinfo *info, globaldata * g);
static BOOL FreeAnodeBlocks(ULONG anodenr, enum freeblocktype freenodes, globaldata * g);
#endif

/**********************************************************************/
/*                        SEARCH IN DIRECTORY                         */
/*                        SEARCH IN DIRECTORY                         */
/*                        SEARCH IN DIRECTORY                         */
/**********************************************************************/

/* GetFullPath converts a relative path to an absolute path.
 * The fileinfo of the new path is returned in [result].
 * The return value is the filename without path.
 * Error: return 0
 *
 * Parsing Syntax:
 * : after '/' or at the beginning ==> root
 * : after [name] ==> volume [name]
 * / after / or ':' or at the beginning ==> parent
 * / after dir ==> get dir
 * / after file ==> error (ALWAYS) (AMIGADOS ok if LAST file)
 *
 * IN basispath, filename, g
 * OUT fullpath, error
 *
 * If only a partial path is found, a pointer to the unparsed part
 * will be stored in g->unparsed.
 */
UBYTE *GetFullPath(union objectinfo *basispath, STRPTR filename,
				   union objectinfo *fullpath, SIPTR *error, globaldata * g)
{
	UBYTE *pathpart, parttype;
	COUNT index;
	BOOL eop = FALSE, success = TRUE;
	struct volumedata *volume;

	// VVV Init:getrootvolume
	ENTER("GetFullPath");
	g->unparsed = NULL;

	/* Set base path */
	if (basispath)
		*fullpath = *basispath;
	else
		GetRoot(fullpath, g);

	/* The basispath should not be a file
	 * BTW: softlink is illegal too, but not possible
	 */
	if (IsFile(*fullpath) || IsDelFile(*fullpath))
	{
		*error = ERROR_OBJECT_WRONG_TYPE;
		return NULL;
	}

	/* check if device present */
	if (IsVolume(*fullpath) || IsDelDir(*fullpath))
		volume = fullpath->volume.volume;
	else
		volume = fullpath->file.dirblock->volume;

	if (!CheckVolume(volume, 0, error, g))
		return (0);

	/* extend base-path using filename and
	 * continue until path complete (eop = end of path)
	 */
	while (!eop)
	{
		pathpart = filename;
		index = strcspn(filename, "/:");
		parttype = filename[index];
		filename[index] = 0x0;

		switch (parttype)
		{
			case ':':
				success = FALSE;
				break;

			case '/':
				if (*pathpart == 0x0)
				{
					// if already at root, fail with an error
					if (IsVolume(*fullpath))
					{
						*error = ERROR_OBJECT_NOT_FOUND;
						success = FALSE;
						break;
					}
					success = GetParentOf(fullpath, error, g);
				}
				else
					success = GetDir(pathpart, fullpath, error, g);
				break;

			default:
				eop = TRUE;
		}

		filename[index] = parttype;

		if (!success)
		{
			/* return pathrest for readlink() */
			if (*error == ERROR_IS_SOFT_LINK)
				g->unparsed = filename + index;
			else if (*error == ERROR_OBJECT_NOT_FOUND)
				g->unparsed = filename;
			return NULL;
		}

		if (!eop)
			filename += index + 1;
	}

	return filename;
}

BOOL GetRoot(union objectinfo * path, globaldata * g)
{
	UpdateCurrentDisk(g);
	path->volume.root = 0;
	path->volume.volume = g->currentvolume;
	return DOSTRUE;
}

/* pre: - path <> 0 and volume or directory
 * result back in path
 */
static BOOL GetParentOf(union objectinfo *path, SIPTR *error, globaldata * g)
{
	BOOL ok;
	union objectinfo info;

	info = *path;
	ok = GetParent(&info, path, error, g);
	return ok;
}

/* pre: - path <> 0 and volume of directory
 *      - dirname without path; strlen(dirname) > 0
 * result back in path
 */
static BOOL GetDir(STRPTR dirname, union objectinfo *path, SIPTR *error, globaldata * g)
{
	BOOL found;

	found = GetObject(dirname, path, error, g);

#if DELDIR
	if (found && g->deldirenabled && IsDelDir(*path))
		return DOSTRUE;
#endif

	/* check if found directory */
#if DELDIR
	if (!found)
		return DOSFALSE;
	if (IsFile(*path) || IsDelFile(*path))
#else
	if (!found)
		return DOSFALSE;
	if (IsFile(*path))
#endif
	{
		*error = ERROR_OBJECT_NOT_FOUND;    // DOPUS doesn't like DIR_NOT_FOUND
		return DOSFALSE;
	}

	/* check if softlink */
	if (IsSoftLink(*path))
	{
		*error = ERROR_IS_SOFT_LINK;
		return DOSFALSE;
	}

	/* resolve links */
	if (path->file.direntry->type == ST_LINKDIR)
	{
		struct extrafields extrafields;
		struct canode linknode;

		GetExtraFields(path->file.direntry, &extrafields);
		if (!GetAnode(&linknode, path->file.direntry->anode, g))
		{
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;
		}
		if (!FetchObject(linknode.clustersize, extrafields.link, path, error, g))
			return DOSFALSE;
	}

	return DOSTRUE;
}

/* pre: - path<>0 and volume of directory
 *      - objectname without path; strlen(objectname) > 0
 * result back in path
 */
static BOOL GetObject(STRPTR objectname, union objectinfo *path, SIPTR *error, globaldata *g)
{
	ULONG anodenr;
	BOOL found;
	LONG lookup;
#if DELDIR
	if (IsDelDir(*path))
	{
		*error = 0;
		found = (SearchInDeldir(objectname, path, error, g) != NULL);
		if (!found && *error)
			return DOSFALSE;
		goto go_error;
	}
#endif

	if (IsVolume(*path))
		anodenr = ANODE_ROOTDIR;
	else
		anodenr = path->file.direntry->anode;

	DB(Trace(1, "GetObject", "parent anodenr %lx\n", anodenr));
	lookup = SearchInDir(anodenr, objectname, path, g);
	if (lookup < 0)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	found = lookup != 0;

  go_error:
	if (!found)
	{
#if DELDIR
		if (g->deldirenabled && IsVolume(*path))
		{
			if (stricmp(deldirname+1, objectname) == 0)
			{
				path->deldir.special = SPECIAL_DELDIR;
				path->deldir.volume = g->currentvolume;
				return DOSTRUE;
			}
		}
#endif
		*error = ERROR_OBJECT_NOT_FOUND;
		return DOSFALSE;
	}

	return DOSTRUE;
}

/* <FindObject> 
 *
 * FindObject searches the object 'fname' in directory 'directory'.
 * FindObject zoekt het object 'fname' in directory 'directory'. 
 * Interpret empty filename as parent and ":" as root.
 * Does not use multiple-assign-list
 *
 * input : - [directory]: the 'root' directory of the search
 *         - [objectname]: file to be found, including path
 * 
 * output: - [object]: If file found : fileinfo of object
 *                     If path found : fileinfo of directory
 *         - [error]: Errornumber as result = DOSFALSE; otherwise 0
 *
 * result: DOSTRUE  (-1) = file found (->in fileinfo)
 *          DOSFALSE (0)  = error
 *
 * If only a partial path is found, a pointer to the unparsed part
 * will be stored in g->unparsed.
 */
BOOL FindObject(union objectinfo *directory, STRPTR objectname,
				union objectinfo *object, SIPTR *error, globaldata *g)
{
	UBYTE *filename;
	BOOL ok;

	*error = 0;
	filename = GetFullPath(directory, objectname, object, error, g);

	if (!filename)
	{
		DB(Trace(2, "FindObject !filename %s\n", objectname));
		return DOSFALSE;
	}

	/* path only (dir or volume) */
	if (!*filename)
		return DOSTRUE;

	/* there is a filepart (file or dir)  */
	ok = GetObject(filename, object, error, g);
	if (!ok && (*error == ERROR_OBJECT_NOT_FOUND))
		g->unparsed = filename;

	return ok;
}


/* GetParent
 *
 * childanodenr = anodenr of start directory (the child)
 * parentanodenr = anodenr of directory containing childanodenr (the parent)
 * childfi == parentfi can be dangerous
 * in:childfi; out:parentfi, error
 */
BOOL GetParent(union objectinfo *childfi, union objectinfo *parentfi, SIPTR *error, globaldata *g)
{
	struct canode anode;
	struct cdirblock *dirblock;
	struct direntry *de;
	ULONG anodeoffset = 0;
	ULONG childanodenr, parentanodenr;
	BOOL eod = FALSE, eob = FALSE, found = FALSE;

	// -I- Find anode of parent
	if (!childfi || IsVolume(*childfi))     // child is rootdir
	{
		*error = 0x0;           /* No error; just return NULL */
		return 0;
	}

#if DELDIR
	if (g->deldirenabled)
	{
		if (IsDelDir(*childfi))
			return GetRoot(parentfi, g);

		if (IsDelFile(*childfi))
		{
			parentfi->deldir.special = SPECIAL_DELDIR;
			parentfi->deldir.volume = g->currentvolume;
			return TRUE;
		}
	}
#endif

	childanodenr = childfi->file.dirblock->blk.anodenr;     /* the directory 'child' is in */
	parentanodenr = childfi->file.dirblock->blk.parent;     /* the directory 'childanodenr' is in */

	// -II- check if in root
	if (parentanodenr == 0)     /* child is in rootdir */
	{
		parentfi->volume.root = 0;
		parentfi->volume.volume = childfi->file.dirblock->volume;
		return TRUE;
	}

	// -III- get parentdirectory and find direntry
	if (!GetAnode(&anode, parentanodenr, g))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return FALSE;
	}
	while (!found && !eod)
	{
		dirblock = LoadDirBlock(anode.blocknr + anodeoffset, g);
		if (dirblock)
		{
			de = FIRSTENTRY(dirblock);
			eob = 0;

			while (!found && !eob)
			{
				found = (de->anode == childanodenr);
				eob = (de->next == 0);
				if (!found && !eob)
					de = NEXTENTRY(de);
			}

			if (!found)
				eod = !NextBlock(&anode, &anodeoffset, g);
		}
		else
		{
			break;
		}
	}

	if (!found)
	{
		DB(Trace(1, "GetParent", "DiskNotValidated %ld\n", childanodenr));
		*error = PFS_ERROR_INCONSISTENT;
		return FALSE;
	}

	parentfi->file.direntry = de;
	parentfi->file.dirblock = dirblock;
	LOCK(dirblock);
	return TRUE;
}


/* SearchInDir
 *
 * Search an object in a directory and return the fileinfo
 *
 * input : - dirnodenr: anodenr of directory to search in
 *         - objectname: found object (without path)
 * 
 * output: - info: objectinfo of found object
 *
 * result: 1 found, 0 absent, -1 unreadable
 */
static LONG SearchInDir(ULONG dirnodenr, STRPTR objectname, union objectinfo *info, globaldata * g)
{
	struct canode anode;
	struct cdirblock *dirblock;
	struct direntry *entry = NULL;
	BOOL found = FALSE, eod = FALSE;
	ULONG anodeoffset;
	UBYTE intl_name[PATHSIZE + 2];	/* length byte + 255 chars + NUL */

	ENTER("SearchInDir");
	ctodstr(objectname, intl_name);

	/* truncate */
	if (intl_name[0] > FILENAMESIZE - 1)
		intl_name[0] = FILENAMESIZE - 1;

	intltoupper(intl_name);     /* international uppercase objectname */
	if (!GetAnode(&anode, dirnodenr, g))
		return -1;
	anodeoffset = 0;
	dirblock = LoadDirBlock(anode.blocknr, g);

	while (dirblock && !found && !eod)  /* eod stands for end-of-dir */
	{
		entry = (struct direntry *)(&dirblock->blk.entries);

		/* scan block */
		while (entry->next)
		{
			found = intlcmp(intl_name, DIRENTRYNAME(entry));
			if (found)
				break;
			entry = NEXTENTRY(entry);
		}

		/* load next block */
		if (!found)
		{
			if (NextBlock(&anode, &anodeoffset, g))
				dirblock = LoadDirBlock(anode.blocknr + anodeoffset, g);
			else if (anode.blocknr == (ULONG)~0UL)
				return -1;
			else
				eod = TRUE;
		}
	}

	/* make fileinfo */
	if (!dirblock)
	{
		return -1;
	}
	else if (found)
	{
		info->file.direntry = entry;
		info->file.dirblock = dirblock;
		LOCK(dirblock);
		return TRUE;
	}
	else
		return FALSE;
}


/*
 * Search object by anodenr in directory 
 * in: diranodenr, target: anodenr of target and the anodenr of the directory to search in
 * out: result: an objectinfo to the object, if found
 * returns: success, with error distinguishing absence from I/O failure
 */
BOOL FetchObject(ULONG diranodenr, ULONG target, union objectinfo *result, SIPTR *error, globaldata *g)
{
	struct canode anode;
	struct cdirblock *dirblock;
	struct direntry *de;
	ULONG anodeoffset = 0;
	BOOL eod = FALSE, found = FALSE;

	/* Get directory and find object */
	if (!GetAnode(&anode, diranodenr, g))
		goto read_error;
	while (!found && !eod)
	{
		dirblock = LoadDirBlock(anode.blocknr + anodeoffset, g);
		if (dirblock)
		{
			de = FIRSTENTRY(dirblock);
			while (de->next)
			{
				if (!(found = (de->anode == target)))
					de = NEXTENTRY(de);
				else
					break;
			}

			if (!found && !(eod = !NextBlock(&anode, &anodeoffset, g)))
				continue;
			if (!found && anode.blocknr == (ULONG)~0UL)
				goto read_error;
		}
		else
			goto read_error;
	}

	if (!found)
	{
		*error = ERROR_OBJECT_NOT_FOUND;
		return FALSE;
	}

	result->file.dirblock = dirblock;
	result->file.direntry = de;
	LOCK(dirblock);
	return TRUE;

read_error:
	*error = PFS_ERROR_INCONSISTENT;
	return FALSE;
}

/**********************************************************************/
/*                       GET DIRECTORY CONTENTS                       */
/*                       GET DIRECTORY CONTENTS                       */
/*                       GET DIRECTORY CONTENTS                       */
/**********************************************************************/


/* ExamineFile
 *
 * Specification:
 *
 * - fill in fileinfoblock
 * - prepares for ExamineNextFile
 * - result: success
 *   *error: error if failure; else undefined
 *
 * - fib_DirEntryType must be equal to fib_EntryType
 * - size64, when not NULL, receives the full file size of a file entry
 */
BOOL ExamineFile(listentry_t *file, struct FileInfoBlock * fib, FSIZE *size64, SIPTR *error, globaldata * g)
{
	struct volumedata *volume;
#if DELDIR
	struct crootblockextension *rext;
	union objectinfo *info;
#endif

	/* volume must be or become currentvolume */
	if (!file)
		volume = g->currentvolume;
	else
		volume = file->volume;

	/* fill in fib */
	fib->fib_DiskKey = (IPTR)file;  // I use it as dir-block-number for ExNext
	if (!file || IsVolumeEntry(file))
	{
		size_t namelen;

		fib->fib_DirEntryType = \
			fib->fib_EntryType = ST_USERDIR;
		fib->fib_Protection = (g->diskstate == ID_WRITE_PROTECTED) || g->softprotect;
		fib->fib_Size = 0;
		fib->fib_NumBlocks = 0;

		if (volume->rblkextension)
		{
			fib->fib_Date.ds_Days = (ULONG)volume->rblkextension->blk.root_date[0];
			fib->fib_Date.ds_Minute = (ULONG)volume->rblkextension->blk.root_date[1];
			fib->fib_Date.ds_Tick = (ULONG)volume->rblkextension->blk.root_date[2];
		}
		else
		{
			fib->fib_Date.ds_Days = (ULONG)volume->rootblk->creationday;
			fib->fib_Date.ds_Minute = (ULONG)volume->rootblk->creationminute;
			fib->fib_Date.ds_Tick = (ULONG)volume->rootblk->creationtick;
		}

		namelen = min(volume->rootblk->diskname[0], sizeof(fib->fib_FileName) - 2);
		memcpy(fib->fib_FileName, volume->rootblk->diskname, namelen + 1);
		fib->fib_FileName[0] = namelen;
		fib->fib_FileName[namelen + 1] = 0;
		fib->fib_Comment[0] = 0x0;
		//fib->fib_Reserved[0]  = 0x0;
	}
	else
#if DELDIR
	{
		info = &file->info;
		if (IsDelDir(*info))
		{
			rext = volume->rblkextension;
			fib->fib_DiskKey = 0;
			fib->fib_DirEntryType = \
				fib->fib_EntryType = ST_USERDIR;
			fib->fib_Protection = (ULONG)rext->blk.dd_protection;
			fib->fib_Size = 0;
			fib->fib_NumBlocks = 0;
			fib->fib_Date.ds_Days = (ULONG)rext->blk.dd_creationday;
			fib->fib_Date.ds_Minute = (ULONG)rext->blk.dd_creationminute;
			fib->fib_Date.ds_Tick = (ULONG)rext->blk.dd_creationtick;
			strcpy(fib->fib_FileName, deldirname);
			fib->fib_Comment[0] = 0x0;
			fib->fib_OwnerUID = (ULONG)rext->blk.dd_uid;
			fib->fib_OwnerGID = (ULONG)rext->blk.dd_gid;

			/* prepare ExNext() */
			((lockentry_t *)file)->nextanode = 0;
			return DOSTRUE;
		}
		else if (IsDelFile(*info))
		{
			if (!FillDelfileFib(NULL, info->delfile.slotnr, fib, g))
			{
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
			return DOSTRUE;
		}
		else
#endif
		{
			if (!FillFib(file->info.file.direntry, file->info.file.dirblock, fib, size64, g))
			{
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
		}
#if DELDIR
	}
#endif

	/* prepare examinenext */
	if (file && file->type.flags.dir && !GetFirstEntry((lockentry_t *)file, g))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}

	return DOSTRUE;
}

/* ExamineNextFile
 *
 * Specification:
 *
 * - used to obtain info on all directory entries
 * - fill in fileinfoblock
 * 
 * Quirks:
 *
 * - fib_DirEntryType must be equal to fib_EntryType
 * - it is allowed to stop invoking before end of dir
 * - for old compatibility: only DiskKey and FileName guaranteed to be the same
 *                           on subsequent calls
 * - it is possible to receive other packets between ExNext's -> special cases
 * - the lock is passed is not always the same, but it is a lock to the same object
 *
 * Implementation:
 * 
 * - fib_DiskKey contains address of lock; different->problems
 *
 * lock->nextentry contains pointer to be fetched entry. End of dir <=>
 * lock->nextentry == {NULL, NULL}
 *
 */
BOOL ExamineNextFile(lockentry_t *file, struct FileInfoBlock * fib, FSIZE *size64,
					 SIPTR *error, globaldata * g)

{
	struct direntry *direntry;

	*error = 0;

	/* file must be lock on dir; NULL lock not allowed */
	if (!file || !file->le.type.flags.dir)
	{
		*error = ERROR_OBJECT_WRONG_TYPE;
		return DOSFALSE;
	}

#if DELDIR
	if (IsDelDir(file->le.info))
	{
		struct deldirentry *dde;

		dde = GetDeldirEntry(&fib->fib_DiskKey, error, g);
		if (dde)
		{
			if (!FillDelfileFib(dde, fib->fib_DiskKey, fib, g))
			{
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
			fib->fib_DiskKey++;
			return DOSTRUE;
		}
		else
		{
			if (!*error)
				*error = ERROR_NO_MORE_ENTRIES;
			return DOSFALSE;
		}
	}
#endif

	/* check if same lock used */
	if ((IPTR)file != (IPTR)fib->fib_DiskKey)
	{
		DB(Trace(1, "ExamineNextFile", "Other lock!!\n"));
		RemakeNextEntry(file, fib, g);
	}

	if (file->nexterror || file->nextdirblocknr)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	/* Check if end of dir */
	if (file->nextentry.dirblock == NULL)
	{
		*error = ERROR_NO_MORE_ENTRIES;
		return DOSFALSE;
	}

	/* Fill infoblock */
	direntry = file->nextentry.direntry;
	if (direntry->next)
	{
		fib->fib_DiskKey = (IPTR)file;  // I use it as dir-block-number
		if (!FillFib(direntry, file->nextentry.dirblock, fib, size64, g))
		{
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;
		}
	}
	else
	{
		*error = ERROR_NO_MORE_ENTRIES;
		ErrorMsg(AFS_ERROR_EX_NEXT_FAIL, NULL, g);
		return DOSFALSE;
	}

	/* get next entry */
	GetNextEntry(file, g);
	return DOSTRUE;
}


/* Fill fileinfoblock of a file
 */
static BOOL FillFib(struct direntry *direntry, struct cdirblock *dirblock, struct FileInfoBlock *fib, FSIZE *size64, globaldata * g)
{
	struct extrafields extrafields;
	UBYTE *comment;
	FSIZE size;
	size_t nameoffset = offsetof(struct direntry, startofname);
	size_t commentoffset, namelen, commentlen;
	UBYTE *blockstart = (UBYTE *)&dirblock->blk;
	UBYTE *entry = (UBYTE *)direntry;

	if ((IPTR)entry < (IPTR)blockstart ||
		(IPTR)entry - (IPTR)blockstart > g->rootblock->reserved_blksize ||
		g->rootblock->reserved_blksize - ((IPTR)entry - (IPTR)blockstart) < nameoffset + 1)
		return FALSE;
	if (direntry->next > g->rootblock->reserved_blksize - ((IPTR)entry - (IPTR)blockstart))
		return FALSE;
	namelen = direntry->nlength;
	commentoffset = nameoffset + namelen;
	if (commentoffset >= direntry->next)
		return FALSE;
	comment = (UBYTE *)direntry + commentoffset;
	commentlen = *comment;
	if (commentlen > direntry->next - commentoffset - 1)
		return FALSE;
	namelen = min(namelen, sizeof(fib->fib_FileName) - 2);
	commentlen = min(commentlen, sizeof(fib->fib_Comment) - 2);
	
	size = GetDEFileSize(direntry, g);
	if (size64)
		*size64 = size;
	/* fib->fib_DiskKey done by Examine(Next) */
	fib->fib_DirEntryType = direntry->type;
	fib->fib_EntryType = direntry->type;
	fib->fib_Protection = (ULONG)direntry->protection;
	fib->fib_Size = GetDEFileSize32(direntry, g);
	fib->fib_NumBlocks = (size >> BLOCKSHIFT) + ((size & BLOCKSIZEMASK) != 0);
	fib->fib_Date.ds_Days = direntry->creationday;
	fib->fib_Date.ds_Minute = direntry->creationminute;
	fib->fib_Date.ds_Tick = direntry->creationtick;
	fib->fib_FileName[0] = namelen;
	memcpy(&fib->fib_FileName[1], &direntry->startofname, namelen);
	fib->fib_FileName[namelen + 1] = 0;
	fib->fib_Comment[0] = commentlen;
	memcpy(&fib->fib_Comment[1], comment + 1, commentlen);
	fib->fib_Comment[commentlen + 1] = 0;

	if (g->dirextension)
	{
		GetExtraFields(direntry, &extrafields);
		fib->fib_Protection |= extrafields.prot;
		fib->fib_OwnerUID = extrafields.uid;
		fib->fib_OwnerGID = extrafields.gid;

#if ROLLOVER
		if (direntry->type == ST_ROLLOVERFILE)
		{
			fib->fib_Size = extrafields.virtualsize;
			if (size64)
				*size64 = extrafields.virtualsize;
		}
#endif
	}
	return TRUE;
}

/* Fill 'nextentry' of listentry [le]. If directory contains no entries,
 * nextentry will be set to NULL
 * > not for deldirs <
 */
static BOOL GetFirstEntry(lockentry_t *le, globaldata * g)
{
	le->nexterror = 0;
	le->nextdirblocknr = le->nextdirblockoffset = 0;
	if (!GetFirstNonEmptyDE(le->le.anodenr, &le->nextentry, &le->nextanode, g))
	{
		le->nexterror = PFS_ERROR_INCONSISTENT;
		return FALSE;
	}
	return TRUE;
}

/* Get first non empty direntry starting from anode [anodenr] 
 * Returns {NULL, NULL} if end of dir, FALSE on read failure.
 */
static BOOL GetFirstNonEmptyDE(ULONG anodenr, struct fileinfo *info, ULONG *foundanode, globaldata *g)
{
	struct canode anode;
	info->direntry = NULL;
	info->dirblock = NULL;
	*foundanode = 0;
	while (anodenr)
	{
		if (!GetAnode(&anode, anodenr, g) ||
			!(info->dirblock = LoadDirBlock(anode.blocknr, g)))
		{
			info->direntry = NULL;
			info->dirblock = NULL;
			return FALSE;
		}
		info->direntry = FIRSTENTRY(info->dirblock);
		if (info->direntry->next)
		{
			LOCK(info->dirblock);
			*foundanode = anode.nr;
			return TRUE;
		}
		anodenr = anode.next;
	}
	info->direntry = NULL;
	info->dirblock = NULL;
	return TRUE;
}


static void RemakeNextEntry(lockentry_t *file, struct FileInfoBlock *fib, globaldata *g)
{
	ULONG anodenr;
	UBYTE filename[FNSIZE];

	anodenr = file->le.anodenr;
	if (ddstricmp(fib->fib_FileName, file->le.volume->rootblk->diskname))
		GetFirstEntry(file, g);
	else
	{
		LONG lookup;
		file->nexterror = 0;
		file->nextdirblocknr = file->nextdirblockoffset = 0;
		if (!BCPLtoCString(filename, sizeof(filename), fib->fib_FileName))
			lookup = -1;
		else
			lookup = SearchInDir(anodenr, filename, (union objectinfo *)&file->nextentry, g);
		if (lookup <= 0)
		{
			file->nextentry.dirblock = NULL;
			file->nextentry.direntry = NULL;
			file->nextanode = anodenr;
			if (lookup < 0)
				file->nexterror = PFS_ERROR_INCONSISTENT;
		}
		else
		{
			struct canode anode;
			ULONG cur = anodenr;

			file->nextanode = anodenr;
			while (cur)
			{
				if (!GetAnode(&anode, cur, g))
				{
					file->nextentry.dirblock = NULL;
					file->nextentry.direntry = NULL;
					file->nexterror = PFS_ERROR_INCONSISTENT;
					return;
				}
				file->nextanode = anode.nr;
				if (anode.blocknr == file->nextentry.dirblock->blocknr)
					break;
				cur = anode.next;
			}
			GetNextEntry(file, g);
		}
	}
}

void GetNextEntry(lockentry_t *file, globaldata * g)
{
	struct canode anode;

	/* get nextentry */
	file->nextentry.direntry = NEXTENTRY(file->nextentry.direntry);

	/* no next entry? -> next block */
	if (!file->nextentry.direntry->next)
	{
		/* NB: 'nextanode' is een verwarrende naam */
		if (!GetAnode(&anode, file->nextanode, g) ||
			!GetFirstNonEmptyDE(anode.next, &file->nextentry, &file->nextanode, g))
		{
			file->nextentry.direntry = NULL;
			file->nextentry.dirblock = NULL;
			file->nexterror = PFS_ERROR_INCONSISTENT;
		}
	}
}

/* ExamineAll
 *
 * fill buffer with directory information
 *
 * Note: +- 2000 stack needed
 */
static ULONG FillInData(struct ExAllData *buffer, LONG type, struct direntry *direntry, ULONG spaceleft, SIPTR *error, globaldata *g)
{
	UWORD nameoffset, commentoffset = 0;
	UBYTE *name, *comment = NULL;
	UCOUNT size;
	struct extrafields extrafields;
	size_t entrycommentoffset, namelen, commentlen;

	/* An entry that does not hold its own name and comment is an error,
	 * not "does not fit": the caller must not retry it forever.
	 */
	namelen = direntry->nlength;
	entrycommentoffset = offsetof(struct direntry, startofname) + namelen;
	comment = (UBYTE *)direntry + entrycommentoffset;
	if (entrycommentoffset + 1 > direntry->next ||
		(commentlen = *comment) > direntry->next - entrycommentoffset - 1)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return 0;
	}

	/* get location to put name */
	switch (type)
	{
		case ED_NAME:
			size = offsetof(struct ExAllData, ed_Type);
			break;

		case ED_TYPE:
			size = offsetof(struct ExAllData, ed_Size);
			break;

		case ED_SIZE:
			size = offsetof(struct ExAllData, ed_Prot);
			break;

		case ED_PROTECTION:
			size = offsetof(struct ExAllData, ed_Days);
			break;

		case ED_DATE:
			size = offsetof(struct ExAllData, ed_Comment);
			break;

		case ED_COMMENT:
			size = offsetof(struct ExAllData, ed_OwnerUID);
			break;

		case ED_OWNER:
#if defined(__MORPHOS__)
			size = offsetof(struct ExAllData, ed_Size64);
			break;

		case ED_SIZE64:
#endif
			size = sizeof(struct ExAllData);
			break;

		default:
			size = offsetof(struct ExAllData, ed_Type);
			break;
	}

	/* size of name */
	nameoffset = size;
	name = &direntry->nlength;
	size += namelen + 1;

	/* size of comment */
	if (type >= ED_COMMENT)
	{
		commentoffset = size;
		size += commentlen + 1;
	}

	/* check fit */
	size = (size + 1) & 0xfffe;
	if (size > spaceleft)
		return (0);

	/* get extra fields */
	memset(&extrafields, 0, sizeof(extrafields));
	if (g->dirextension)
		GetExtraFields(direntry, &extrafields);

	/* copy */
	buffer->ed_Next = NULL;
	switch (type)
	{
#if defined(__MORPHOS__)
		case ED_SIZE64:
#if ROLLOVER
			if (direntry->type == ST_ROLLOVERFILE)
				buffer->ed_Size64 = extrafields.virtualsize;
			else
#endif
				buffer->ed_Size64 = GetDEFileSize(direntry, g);
#endif
		case ED_OWNER:
			buffer->ed_OwnerUID = extrafields.uid;
			buffer->ed_OwnerGID = extrafields.gid;

		case ED_COMMENT:
			buffer->ed_Comment = (UBYTE *)buffer + commentoffset;
			strncpy((UBYTE *)buffer + commentoffset, comment + 1, commentlen);
			*((UBYTE *)buffer + commentoffset + commentlen) = 0x0;

		case ED_DATE:
			buffer->ed_Days = direntry->creationday;
			buffer->ed_Mins = direntry->creationminute;
			buffer->ed_Ticks = direntry->creationtick;

		case ED_PROTECTION:
			buffer->ed_Prot = (ULONG)direntry->protection | extrafields.prot;

		case ED_SIZE:
#if ROLLOVER
			if (direntry->type == ST_ROLLOVERFILE)
				buffer->ed_Size = extrafields.virtualsize;
			else
#endif
				buffer->ed_Size = GetDEFileSize32(direntry, g);

		case ED_TYPE:
			buffer->ed_Type = direntry->type;

		case ED_NAME:

			strncpy((UBYTE *)buffer + nameoffset,
					(UBYTE *)&direntry->startofname, namelen);
			*((UBYTE *)buffer + nameoffset + namelen) = 0x0;

		default:
			buffer->ed_Name = (UBYTE *)buffer + nameoffset;
	}

	return size;
}

BOOL ExamineAll(lockentry_t *object, UBYTE *buffer, ULONG buflen, LONG type, struct ExAllControl * ctrl, SIPTR *error, globaldata * g)
{
	struct direntry *direntry;
	struct ExAllData *lasteaentry = NULL;
	ULONG spaceleft, size;
	UWORD j;
	BOOL eod;
#if DELDIR
	struct deldirentry *dde;
#endif

	ENTER("ExamineAll");
	*error = 0;

#ifdef KSWRAPPER
	if (!g->v37DOS)
		return DOSFALSE;
#endif

	/* init and check */
	if (!object || !object->le.type.flags.dir)
	{
		*error = ERROR_OBJECT_WRONG_TYPE;
		return DOSFALSE;
	}

	/* check type field */
#if defined(__MORPHOS__)
	if (type > ED_SIZE64 || type < ED_NAME)
#else
	if (type > ED_OWNER || type < ED_NAME)
#endif
	{
		*error = ERROR_BAD_NUMBER;
		return DOSFALSE;
	}

	/* Prepare loop */
	spaceleft = buflen;
	j = 0;                      // # direntries in buffer

#if DELDIR
	if (IsDelDir(object->le.info))
	{
		/* Main loop */
		*error = 0;
		while ((dde = GetDeldirEntry(&ctrl->eac_LastKey, error, g)))
		{
			BOOL wanted;

			size = FillInDDEData((struct ExAllData *)buffer, type, dde, ctrl->eac_LastKey,
									spaceleft, g);
			if (size)
			{
				/* check if entry is wanted (needs 1500 stack) */
				if (ctrl->eac_MatchFunc)
					wanted = CallHookPkt(ctrl->eac_MatchFunc, &type, buffer);
				else if (ctrl->eac_MatchString)
					wanted = MatchPatternNoCase(ctrl->eac_MatchString,
									   ((struct ExAllData *)buffer)->ed_Name);
				else
					wanted = TRUE;

				/* if so update statistics */
				if (wanted)
				{
					if (lasteaentry)
						lasteaentry->ed_Next = (struct ExAllData *)buffer;

					lasteaentry = (struct ExAllData *)buffer;
					buffer += size;
					spaceleft -= size;
					j++;
				}

				ctrl->eac_LastKey++;
			}
			else
				break;          // doesn't fit

		}

		/* Finish up */
		ctrl->eac_Entries = j;
		if (!dde)
		{
			if (!*error)
				*error = ERROR_NO_MORE_ENTRIES;
			return DOSFALSE;
		}
		else
			return DOSTRUE;
	}
#endif

	/* check if it's the first call and if same lock is used */
	if (ctrl->eac_LastKey == 0)
	{
		if (!GetFirstEntry(object, g))
		{
			ctrl->eac_Entries = 0;
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;
		}
	}
	else if ((IPTR)object != (IPTR)ctrl->eac_LastKey)
	{
		ctrl->eac_Entries = 0;
		*error = ERROR_NO_MORE_ENTRIES;
		return DOSFALSE;
	}

	DB(Trace(1, "ExamineAll", "matchfunc = %lx matchstring = %s\n", ctrl->eac_MatchFunc, ctrl->eac_MatchString));

	/* Main loop */
	while (1)
	{
		BOOL wanted;

		if (object->nexterror || object->nextdirblocknr)
		{
			eod = FALSE;
			*error = PFS_ERROR_INCONSISTENT;
			break;
		}
		if (!(eod = (object->nextentry.dirblock == NULL)))
			direntry = object->nextentry.direntry;
		else
			break;

		size = FillInData((struct ExAllData *)buffer, type, direntry,
							spaceleft, error, g);
		if (size)
		{

			/* check if entry is wanted (needs 1500 stack) */
			if (ctrl->eac_MatchFunc)
				wanted = CallHookPkt(ctrl->eac_MatchFunc, &type, buffer);
			else if (ctrl->eac_MatchString)
				wanted = MatchPatternNoCase(ctrl->eac_MatchString,
									   ((struct ExAllData *)buffer)->ed_Name);
			else
				wanted = TRUE;

			/* if so update statistics */
			if (wanted)
			{
				if (lasteaentry)
					lasteaentry->ed_Next = (struct ExAllData *)buffer;

				lasteaentry = (struct ExAllData *)buffer;
				buffer += size;
				spaceleft -= size;
				j++;
			}

			GetNextEntry(object, g);
		}
		else
			break;              // doesn't fit

	}

	/* Finish up */
	ctrl->eac_LastKey = (IPTR)object;
	ctrl->eac_Entries = j;

	if (*error == PFS_ERROR_INCONSISTENT)
		return DOSFALSE;
	if (eod)
	{
		*error = ERROR_NO_MORE_ENTRIES;
		return DOSFALSE;
	}
	else
		return DOSTRUE;
}

/**********************************************************************/
/*                        ADD AND REMOVE FILES                        */
/*                        ADD AND REMOVE FILES                        */
/*                        ADD AND REMOVE FILES                        */
/**********************************************************************/

/* <NewFile>
 *
 * NewFile creates a new file in a [directory] on currentvolume
 *
 * input : - [directory]: directory of file;
 *         - [filename]: name (without path) of file
 *         - found: flag, file already present?. If so newfile == old
 *
 * output: - [newfile]: fileinfo of new file (struct is managed by caller)
 *		   - [directory]: fileinfo of parent (can have changed if hardlink) 
 *
 * result: errornr; 0 = success
 *
 * maxneeds: 1 nd + 2 na = 3 res
 *
 * Note: 'directory' and 'newfile' may point to the same.
 */
ULONG NewFile (BOOL found, union objectinfo *directory, STRPTR filename, union objectinfo *newfile, globaldata *g)
{
	union objectinfo info;
	ULONG anodenr;
	SIPTR error;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct extrafields extrafields;
	struct direntry *destentry;
	struct canode anode;
	size_t l;
#if VERSION23
	struct anodechain *achain;
#endif

	DB(Trace(10, "NewFile", "%s\n", filename));
	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, &error, g))
		return error;

#if DELDIR
	if (IsDelDir(*directory))
		return ERROR_WRITE_PROTECTED;
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
		return ERROR_DISK_FULL;

	/* truncate filename to 31 characters */
	if (!(l = strlen(filename)))
		return ERROR_INVALID_COMPONENT_NAME;
	if (l > FILENAMESIZE - 1)
		filename[FILENAMESIZE - 1] = 0x0;

	if (found)
	{
		/*
		 * new version: take over direntry
		 * (used to simply delete old and make new)
		 */
		info.file = newfile->file;
		anodenr = FIANODENR(&info.file);

		/* Check deleteprotection */
		if (IsVolume(info) || info.file.direntry->protection & FIBF_DELETE)
			return ERROR_DELETE_PROTECTED;

		/* If link, get real object. After this it has become a
		 * ST_FILE
		 */
		if ((info.file.direntry->type == ST_LINKFILE) ||
			(info.file.direntry->type == ST_LINKDIR))
		{
			struct canode linknode;

			GetExtraFields(info.file.direntry, &extrafields);
			anodenr = extrafields.link;
			if (!GetAnode(&linknode, info.file.direntry->anode, g))
				return PFS_ERROR_INCONSISTENT;
			if (!FetchObject(linknode.clustersize, anodenr, &info, &error, g))
				return error;
			
			/* have to check protection again */
			if (info.file.direntry->protection & FIBF_DELETE)
				return ERROR_DELETE_PROTECTED;
			
			/* get parent */
			if (!GetParent(&info, directory, &error, g))
				return ERROR_OBJECT_NOT_FOUND;
		}

		/* Check if there are outstanding locks on object */
		if (ScanLockList(HeadOf(&g->currentvolume->fileentries), anodenr))
		{
			DB(Trace(1, "NewFile", "object in use"));
			return ERROR_OBJECT_IN_USE;
		}

		if (!(achain = GetAnodeChain(anodenr, g)))
			return ERROR_NO_FREE_STORE;

		/* Free used space */
		{
			BOOL do_deldir = (g->deldirenabled && info.file.direntry->type == ST_FILE);

			if (do_deldir)
			{
				int ddslot;

				/* free a slot to put old version in, inter. update possible */
				ddslot = AllocDeldirSlot(g);
				if (ddslot < 0)
				{
					DetachAnodeChain(achain, g);
					return PFS_ERROR_INCONSISTENT;
				}

				/* make replacement anode, because we want to reuse the old one */
				info.file.direntry->anode = achain->head.an.nr = AllocAnode(0, g);
				if (!achain->head.an.nr)
				{
					info.file.direntry->anode = anodenr;
					DetachAnodeChain(achain, g);
					return ERROR_DISK_FULL;
				}
				if (!SaveAnode(&achain->head.an, achain->head.an.nr, g))
				{
					FreeAnode(achain->head.an.nr, g);
					info.file.direntry->anode = achain->head.an.nr = anodenr;
					DetachAnodeChain(achain, g);
					return PFS_ERROR_INCONSISTENT;
				}
				anode.clustersize = 0;
				anode.blocknr = 0xffffffff;
				anode.next = 0;
				if (!SaveAnode(&anode, anodenr, g))
				{
					FreeAnode(achain->head.an.nr, g);
					info.file.direntry->anode = achain->head.an.nr = anodenr;
					DetachAnodeChain(achain, g);
					return PFS_ERROR_INCONSISTENT;
				}
				if (!AddToDeldir(&info, ddslot, g))
				{
					/* the old anode was reset: put its head back, or the
					 * old data chain would be orphaned
					 */
					FreeAnode(achain->head.an.nr, g);
					SaveAnode(&achain->head.an, anodenr, g);
					info.file.direntry->anode = anodenr;
					DetachAnodeChain(achain, g);
					return PFS_ERROR_INCONSISTENT;
				}
				info.file.direntry->anode = anodenr;
			}

			/* Rollover files are essentially just 'reset' by overwriting:
			 * only the virtualsize and offset are set to zero (extrafields)
			 * Other files are deleted and recreated as a new file.
			 */
			if (info.file.direntry->type != ST_ROLLOVERFILE)
			{
				/* Reclaim anode */
				if (!do_deldir)
				{
					anode.clustersize = 0;
					anode.blocknr = 0xffffffff;
					anode.next = 0;
					if (!SaveAnode(&anode, anodenr, g))
					{
						DetachAnodeChain(achain, g);
						return PFS_ERROR_INCONSISTENT;
					}
				}

				/* Change directory entry only after the anode save succeeds. */
				SetDEFileSize(info.file.direntry, 0, g);
				info.file.direntry->type = ST_FILE;
				MakeBlockDirty((struct cachedblock *)info.file.dirblock, g);

				/* Delete old file (update possible) */
				if (!FreeBlocksAC(achain, ULONG_MAX,
					do_deldir ? keepanodes : freeanodes, g))
				{
					DetachAnodeChain(achain, g);
					return PFS_ERROR_INCONSISTENT;
				}
				DetachAnodeChain(achain, g);
			}
			else
				DetachAnodeChain(achain, g);
		}

		/* Clear direntry extrafields */
		destentry = (struct direntry *)entrybuffer;
		memcpy(destentry, info.file.direntry, info.file.direntry->next);
		if (g->dirextension)
		{
			GetExtraFields(info.file.direntry, &extrafields);
			extrafields.virtualsize = extrafields.rollpointer = 0;
			AddExtraFields(destentry, &extrafields);
		}
		if (!ChangeDirEntry(info.file, destentry, directory, &info.file, g))
			return ERROR_DISK_FULL;
		newfile->file = info.file;
		return 0;
	}

	/* direntry alloceren en invullen */
	if (MakeDirEntry(ST_FILE, filename, entrybuffer, g))
	{
		if (AddDirectoryEntry(directory, (struct direntry *)entrybuffer, &newfile->file, g))
			return 0;
		else
			FreeAnode(((struct direntry *)entrybuffer)->anode, g);
	}

	return ERROR_DISK_FULL;
}


/* NewDir
 *
 * Specification:
 * 
 * - make new dir 
 * - returns fileentry (!) with exclusive lock
 *
 * Implementation:
 *
 * - check if file/dir exists
 * - make direntry
 * - make first dirblock
 *
 * Similar to NewFile()
 *
 * maxneeds: 2 nd, 3 na = 2 nablk : 4 res
 */
lockentry_t *NewDir(union objectinfo *parent, STRPTR dirname, SIPTR *error, globaldata * g)
{
	union objectinfo info;
	listentry_t *fileentry;
	listtype type;
	struct cdirblock *blk;
	ULONG parentnr, blocknr;
	BOOL added = FALSE;
	LONG lookup;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	size_t l;

	DB(Trace(1, "NewDir", "%s\n", dirname));

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return NULL;

#if DELDIR
	if (IsDelDir(*parent))
	{
		*error = ERROR_WRITE_PROTECTED;
		return NULL;
	}
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return NULL;
	}

	/* checkvolume */
	if (IsVolume(*parent))
		parentnr = ANODE_ROOTDIR;
	else
		parentnr = parent->file.direntry->anode;

	/* truncate dirname to 31 characters */
	if (!(l = strlen(dirname)))
	{
		*error = ERROR_INVALID_COMPONENT_NAME;
		return DOSFALSE;
	}
	if (l > FILENAMESIZE - 1)
		dirname[FILENAMESIZE - 1] = 0x0;

	/* check if object exists */
	lookup = SearchInDir(parentnr, dirname, &info, g);
	if (lookup < 0)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return NULL;
	}
	if (lookup)
	{
		*error = ERROR_OBJECT_EXISTS;
		return DOSFALSE;
	}

	/* allocate directory entry, fill it. Make fileentry */
	if (!MakeDirEntry(ST_USERDIR, dirname, entrybuffer, g))
		goto error1;

	if (!AddDirectoryEntry(parent, (struct direntry *)entrybuffer, &info.file, g))
	{
		FreeAnode(((struct direntry *)entrybuffer)->anode, g);
  error1: 
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	type.value = ET_LOCK | ET_EXCLREAD;
	if (!(fileentry = MakeListEntry(&info, type, error, g)))
		goto error2;
	if (!AddListEntry(fileentry))   /* Should never fail, accessconflict impossible */
	{
		ErrorMsg(AFS_ERROR_NEWDIR_ADDLISTENTRY, NULL, g);
		*error = ERROR_OBJECT_IN_USE;
		goto error2;
	}
	added = TRUE;

	/* Make first directoryblock (needed for parentfinding) */
	if (!(blocknr = AllocReservedBlock(g)))
	{
		*error = ERROR_DISK_FULL;
  error2:
		FreeAnode(info.file.direntry->anode, g);
		RemoveDirEntry(info.file, g);
		if (fileentry)
		{
			if (added)
				RemoveListEntry(fileentry, g);
			else
				FreeListEntry(fileentry, g);
		}
		DB(Trace(1, "Newdir", "disk full"));
		return DOSFALSE;
	}

	blk = MakeDirBlock(blocknr, info.file.direntry->anode,
					   info.file.direntry->anode, parentnr, g);
	if (!blk)
	{
		FreeReservedBlock(blocknr, g);
		*error = ERROR_NO_FREE_STORE;
		goto error2;
	}

	return (lockentry_t *)fileentry;
}

struct cdirblock *MakeDirBlock(ULONG blocknr, ULONG anodenr, ULONG rootanodenr,
							   ULONG parentnr, globaldata * g)
{
	struct canode anode;
	struct cdirblock *blk;
	struct volumedata *volume = g->currentvolume;

	DB(Trace(10,"MakeDirBlock","blocknr = %lx\n", blocknr));

	blk = (struct cdirblock *)AllocLRU(g);
	if (!blk)
		return NULL;
	LOCK(blk);

	/* fill in anode (allocated by MakeDirEntry) */
	anode.clustersize = 1;
	anode.blocknr = blocknr;
	anode.next = 0;
	if (!SaveAnode(&anode, anodenr, g))
	{
		FreeLRU((struct cachedblock *)blk);
		return NULL;
	}

	blk->blk.id = DBLKID;
	blk->blk.anodenr = rootanodenr;
	blk->blk.parent = parentnr;
	blk->volume = volume;
	blk->blocknr = blocknr;
	blk->oldblocknr = 0;
	blk->changeflag = TRUE;

	Hash(blk, volume->dirblks, HASHM_DIR);
	LOCK(blk);
	return blk;
}


/* DeleteObject
 *
 * Specification:
 *
 * - The object referenced by the info structure is removed
 * - The object must be on currentvolume
 *
 * Implementation:
 *
 * - check deleteprotection
 * - if dir, check if directory is empty
 * - check if there are outstanding locks on object 
 * - remove object from directory and free anode
 * - rearrange & store directory
 *
 * Don't check dirtycount!
 * info becomes INVALID!
 */
BOOL DeleteObject(union objectinfo * info, SIPTR *error, globaldata * g)
{
	ULONG anodenr;
	LONG remapped;

	ENTER("DeleteObject");
	/* Check deleteprotection */
#if DELDIR
	if (!info || info->deldir.special <= SPECIAL_DELFILE ||
		info->file.direntry->protection & FIBF_DELETE)
#else
	if (!info || IsVolume(*info) || info->file.direntry->protection & FIBF_DELETE)
#endif
	{
		*error = ERROR_DELETE_PROTECTED;
		return DOSFALSE;
	}

	/* Check if link, links can always be removed */
	if ((info->file.direntry->type == ST_LINKFILE) ||
		(info->file.direntry->type == ST_LINKDIR))
	{
		return DeleteLink(&info->file, error, g);
	}

	anodenr = FIANODENR(&info->file);

	/* Check if there are outstanding locks on object */
	if (ScanLockList(HeadOf(&g->currentvolume->fileentries), anodenr))
	{
		DB(Trace(1, "Delete", "object in use"));
		*error = ERROR_OBJECT_IN_USE;
		return DOSFALSE;
	}

	/* Check if object has links,
	 * if it does the object should not be deleted,
	 * just the direntry
	 */
	remapped = RemapLinks(&info->file, g);
	if (remapped < 0)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if (!remapped)
	{
		/* Remove object from directory and free anode */
		if (info->file.direntry->type == ST_USERDIR)
			return DeleteDir(info, error, g);
		else
		{
			/* ST_FILE or ST_SOFTLINK */
			struct anodechain *achain;
			int do_deldir = g->deldirenabled && info->file.direntry->type == ST_FILE;
			int ddslot;

			if (!(achain = GetAnodeChain(anodenr, g)))
			{
				*error = ERROR_NO_FREE_STORE;
				return DOSFALSE;
			}
			if (do_deldir)
			{
				ddslot = AllocDeldirSlot(g);
				if (ddslot < 0 || !AddToDeldir(info, ddslot, g))
				{
					DetachAnodeChain(achain, g);
					*error = PFS_ERROR_INCONSISTENT;
					return DOSFALSE;
				}
			}

			ChangeDirEntry(info->file, NULL, NULL, NULL, g);    /* remove direntry */
			if (!FreeBlocksAC(achain, ULONG_MAX, do_deldir ? keepanodes : freeanodes, g))
			{
				DetachAnodeChain(achain, g);
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
			if (!do_deldir && !FreeAnode(achain->head.an.nr, g))
			{
				DetachAnodeChain(achain, g);
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
			DetachAnodeChain(achain, g);
		}
	}

	return DOSTRUE;
}

/* 
 * Delete directory
 */
static BOOL DeleteDir(union objectinfo *info, SIPTR *error, globaldata * g)
{
	struct canode anode, chnode;
	struct volumedata *volume = g->currentvolume;
	struct cdirblock *dirblk;
	struct anodewalk walk;

	anode.nr = FIANODENR(&info->file);
	if (!DirIsEmpty(anode.nr, error, g))
	{
		return DOSFALSE;
	}
	else
	{
		/* check if tobefreedcache is sufficiently large,
		 * otherwise update disk
		 */
		chnode.next = anode.nr;
		walk.mark = 0;		/* no anode read yet */
		walk.steps = 0;
		while (chnode.next)
			if (!GetNextAnode(&chnode, chnode.next, &walk, g))
			{
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}

		if (2*(walk.steps + 1) + alloc_data.rtbf_index > RTBF_THRESHOLD)
			UpdateDisk (g);

		/* do it (btw: fails if dirblock contains more than 128 empty
		 * blocks)
		 */
		anode.next = anode.nr;
		while (anode.next)
		{
			if (!GetAnode(&anode, anode.next, g))
			{
				volume->metadata_error = TRUE;
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}

			/* remove dirblock from list if there */
			if (!FreeAnode(anode.nr, g))
			{
				volume->metadata_error = TRUE;
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
			dirblk = (struct cdirblock *)CheckCache(volume->dirblks, HASHM_DIR, anode.blocknr, g);
			if (dirblk)
			{
				MinRemove(dirblk);
				if (dirblk->changeflag)
					ResToBeFreed(dirblk->oldblocknr, g);

				FreeLRU((struct cachedblock *)dirblk);
			}
			ResToBeFreed(anode.blocknr , g);
		}
		ChangeDirEntry(info->file, NULL, NULL, NULL, g);    // delete entry from parentdir
		return DOSTRUE;
	}
}

/*
 * AFS Temporary extensions: kill empty, remove dir entry
 */
BOOL KillEmpty(union objectinfo * parent, globaldata * g)
{
	ULONG dirnodenr;
	SIPTR error;
	union objectinfo filefi;

	if (IsVolume(*parent))
		dirnodenr = ANODE_ROOTDIR;
	else
		dirnodenr = parent->file.direntry->anode;

	if (SearchInDir(dirnodenr, "", &filefi, g) > 0 && IsRealDir(filefi))
		return DeleteDir(&filefi, &error, g);
	else
		return DOSFALSE;
}

/*
 * Remove a directory entry without freeing anything and without
 * checking validity
 */
LONG forced_RemoveDirEntry(union objectinfo *info, SIPTR *error, globaldata * g)
{
	if (!info || IsVolume(*info))
	{
		*error = ERROR_DELETE_PROTECTED;
		return DOSFALSE;
	}

	ChangeDirEntry(info->file, NULL, NULL, NULL, g);    /* rearrange & store done by this function */
	return DOSTRUE;
}

/* Check if directory with anodenr [anodenr] is empty 
 * There can be multiple empty directory blocks
 */
static BOOL DirIsEmpty(ULONG anodenr, SIPTR *error, globaldata * g)
{
	struct canode anode;
	struct cdirblock *dirblok;
	struct anodewalk walk;

	if (!GetAnode(&anode, anodenr, g))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	dirblok = LoadDirBlock(anode.blocknr, g);

	walk.mark = anodenr;
	walk.steps = 0;
	while (dirblok && (FIRSTENTRY(dirblok)->next == 0) && anode.next)
	{
		if (!GetNextAnode(&anode, anode.next, &walk, g))
		{
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;
		}
		dirblok = LoadDirBlock(anode.blocknr, g);
	}

	if (!dirblok)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if ((FIRSTENTRY(dirblok)->next) == 0)    /* not empty->entries present */
		return DOSTRUE;
	else
	{
		*error = ERROR_DIRECTORY_NOT_EMPTY;
		return DOSFALSE;
	}
}

#if DELDIR
/*
 * Frees anodes without freeing blocks
 */
BOOL FreeAnodesInChain(ULONG anodenr, globaldata * g)
{
	struct canode anode;
	struct crootblockextension *rext = g->currentvolume->rblkextension;

	DB(Trace(1, "FreeAnodeInChain", "anodenr: %ld \n", anodenr));
	while (anodenr)
	{
		if (!GetAnode(&anode, anodenr, g))
			goto failure;
		if (IsUpdateNeeded(RTBF_THRESHOLD))
		{
			if (rext)
			{
				rext->blk.tobedone.operation_id = PP_FREEANODECHAIN;
				rext->blk.tobedone.argument1 = anode.nr;
				rext->blk.tobedone.argument2 = 0;
				rext->blk.tobedone.argument3 = 0;
				MakeBlockDirty((struct cachedblock *)rext, g);
			}

			if (!UpdateDisk(g))
				goto failure;
		}

		if (!FreeAnode(anode.nr, g))
			goto failure;
		anodenr = anode.next;
	}

	if (rext && rext->blk.tobedone.operation_id == PP_FREEANODECHAIN)
	{
		rext->blk.tobedone.operation_id = 0;
		rext->blk.tobedone.argument1 = 0;
		rext->blk.tobedone.argument2 = 0;
		rext->blk.tobedone.argument3 = 0;
		MakeBlockDirty((struct cachedblock *)rext, g);
	}
	return TRUE;

failure:
	g->currentvolume->metadata_error = TRUE;
	g->dirty = TRUE;
	return FALSE;
}

#endif /* DELDIR */

/**********************************************************************/
/*                        DIRECTORYOPERATIONS                         */
/*                        DIRECTORYOPERATIONS                         */
/*                        DIRECTORYOPERATIONS                         */
/**********************************************************************/

/* RenameAndMove
 *
 * Specification:
 *
 * - rename object
 * - renaming directories into a child not allowed!
 *
 * Rename across devices tested in dd_Rename (DosToHandlerInterface)
 *
 * Implementation:
 * 
 * - source ophalen
 * - check if new name allowed
 * - destination maken
 * - remove source direntry
 * - add destination direntry
 *
 * maxneeds: 2 dblk changed, 1 new an : 3 res
 *
 * sourcedi = objectinfo of source directory
 * destdi = objectinfo of destination directory
 * srcinfo = objectinfo of source
 * destinfo = objectinfo of destination
 * src- destanodenr = anodenr of source- destination directory
 */
BOOL RenameAndMove (union objectinfo *sourcedi, union objectinfo *srcinfo,
					union objectinfo *destdir, STRPTR destpath, SIPTR *error,
					globaldata * g)
{
	struct direntry *srcdirentry, *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	ULONG srcanodenr, destanodenr;
	WORD srcfieldoffset, destfieldoffset, fieldsize;
	union objectinfo destinfo, destdi;
	UBYTE *srccomment, *destcomment;
	STRPTR destname;

	/* fetch source info & path and check if exists */
	if (!(destname = GetFullPath (destdir, destpath, &destdi, error, g)))
	{
		return DOSFALSE;
	}

	/* source nor destination may be a volume */
	if (IsVolume(*srcinfo) || !*destname)
	{
		*error = ERROR_OBJECT_WRONG_TYPE;
		return DOSFALSE;
	}

#if DELDIR
	if (IsDelDir(*sourcedi) || IsDelDir(destdi) || IsDelDir(*srcinfo))
	{
		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	srcdirentry = srcinfo->file.direntry;
	if ((IPTR)srcdirentry < (IPTR)&srcinfo->file.dirblock->blk ||
		(IPTR)srcdirentry - (IPTR)&srcinfo->file.dirblock->blk > g->rootblock->reserved_blksize ||
		g->rootblock->reserved_blksize - ((IPTR)srcdirentry - (IPTR)&srcinfo->file.dirblock->blk) < offsetof(struct direntry, startofname) + 1 ||
		srcdirentry->next > g->rootblock->reserved_blksize - ((IPTR)srcdirentry - (IPTR)&srcinfo->file.dirblock->blk))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if (srcdirentry->next <= offsetof(struct direntry, startofname) + srcdirentry->nlength)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	srccomment = COMMENT(srcdirentry);
	if (*srccomment > srcdirentry->next - offsetof(struct direntry, startofname) - srcdirentry->nlength - 1)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if (strlen(destname) > FILENAMESIZE - 1)
		destname[FILENAMESIZE - 1] = 0;

	/* check if new name allowed
	 * destpath should exist and file should not
	 * %9.1 the same name IS allowed (rename 'hello' to 'Hello')
	 */
	*error = 0;
	if (FindObject(&destdi, destname, &destinfo, error, g))
	{
		if (destinfo.file.direntry != srcinfo->file.direntry)
		{
			*error = ERROR_OBJECT_EXISTS;
			return DOSFALSE;
		}
	}
	else if (*error != ERROR_OBJECT_NOT_FOUND)
		return DOSFALSE;
	
	/* Test if a directory is being renamed to a child of itself. This is so
	 * if:
	 * 1) source (srcinfo) is a directory and
	 * 2) sourcepath (sourcedi) <> destinationpath (destdi) and
	 * 3) source (srcinfo) is part of destpath (destdi)
	 * Example: rename a/b to a/b/c/d:
	 * 1) a/b is dir [ok]; 2) a <> a/b/c [ok]; 3) a/b is part of a/b/c [ok]
	 * Links need special attention! 
	 */
	srcanodenr = IsRootA(*sourcedi) ? ANODE_ROOTDIR : FIANODENR(&sourcedi->file);
	destanodenr = IsRootA(destdi) ? ANODE_ROOTDIR : FIANODENR(&destdi.file);
	if (IsRealDir(*srcinfo) && (srcanodenr != destanodenr) &&
		IsChildOf(destdi, *srcinfo, error, g))
		return DOSFALSE;

	/* Make destination  */
	destentry = (struct direntry *)&entrybuffer;

	/* copy header */
	memcpy(destentry, srcdirentry, offsetof(struct direntry, nlength));

	/* copy name */
	destentry->nlength = min(strlen(destname), FILENAMESIZE - 1);

	/* copy fields */
	srcfieldoffset = (sizeof(struct direntry) + srcdirentry->nlength + *srccomment) & 0xfffe;
	destfieldoffset = (sizeof(struct direntry) + destentry->nlength + *srccomment) & 0xfffe;
	fieldsize = srcdirentry->next - srcfieldoffset;
	if (fieldsize < 0 ||
		offsetof(struct direntry, startofname) + destentry->nlength + *srccomment + 1 > sizeof(entrybuffer) ||
		destfieldoffset + (g->dirextension ? fieldsize : 0) > 255 ||
		destfieldoffset + (g->dirextension ? fieldsize : 0) > sizeof(entrybuffer))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	memcpy((UBYTE *)&destentry->startofname, destname, destentry->nlength);
	destcomment = (UBYTE *)&destentry->startofname + destentry->nlength;
	memcpy(destcomment, srccomment, *srccomment + 1);
	if (g->dirextension)
		memcpy((UBYTE *)destentry + destfieldoffset, (UBYTE *)srcdirentry + srcfieldoffset, fieldsize);

	/* set size */
	if (g->dirextension)
		destentry->next = (UBYTE)(destfieldoffset + fieldsize);
	else
		destentry->next = (UBYTE)destfieldoffset;

	/* remove source and add new direntry 
	 * Makes srcinfo INVALID
	 */
	PFSDoNotify(&srcinfo->file, TRUE, g);
	if (!ChangeDirEntry(srcinfo->file, destentry, &destdi, &destinfo.file, g))   // output:destinfo
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* Update linklist and notify source if object moved across dirs
	 */
	if (destanodenr != srcanodenr)
	{
		if (!MoveLink(destentry, destanodenr, g))
		{
			g->currentvolume->metadata_error = TRUE;
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;
		}
		PFSDoNotify (&destinfo.file, TRUE, g);
	}
	else
	{
		PFSDoNotify (&destinfo.file, FALSE, g);
	}
		
	/* If object is a directory and parent changed, update dirblocks */
	if (IsRealDir(destinfo) && (srcanodenr != destanodenr) &&
		!SetDirParent(destinfo.file.direntry->anode, destanodenr, g))
	{
		g->currentvolume->metadata_error = TRUE;
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}

	return DOSTRUE;
}


/* AddComment
 *
 * - get old direntry
 * - make new direntry
 * - remove old direntry
 * - add new direntry
 *
 * maxdirty: 1d, 1a = 2 res
 */
BOOL AddComment(union objectinfo * info, STRPTR comment, SIPTR *error, globaldata * g)
{
	struct direntry *sourceentry, *destentry;
	union objectinfo directory;
	UBYTE *destcomment, *srccomment, entrybuffer[MAX_ENTRYSIZE];
	WORD srcfieldoffset, destfieldoffset, fieldsize;

	DB(Trace(1, "AddComment", "%s\n", comment));
#if DELDIR
	if (info->deldir.special <= SPECIAL_DELFILE)
	{
		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	if (strlen(comment) > sizeof(((struct FileInfoBlock *)0)->fib_Comment) - 2)
	{
		*error = ERROR_COMMENT_TOO_BIG;
		return DOSFALSE;
	}

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* make new direntry */
	destentry = (struct direntry *)entrybuffer;
	sourceentry = info->file.direntry;
	if (sourceentry->next < sizeof(struct direntry) ||
		offsetof(struct direntry, startofname) + sourceentry->nlength >= sourceentry->next)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	srccomment = COMMENT(sourceentry);
	srcfieldoffset = (sizeof(struct direntry) + sourceentry->nlength + *srccomment) & 0xfffe;
	destfieldoffset = (sizeof(struct direntry) + sourceentry->nlength + strlen(comment)) & 0xfffe;
	fieldsize = sourceentry->next - srcfieldoffset;
	if (srcfieldoffset > sourceentry->next || fieldsize < 0 ||
		destfieldoffset + (g->dirextension ? fieldsize : 0) > 255 ||
		(size_t)(destfieldoffset + (g->dirextension ? fieldsize : 0)) > sizeof(entrybuffer))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}

	/* copy header & name */
	memcpy(destentry, sourceentry, sizeof(struct direntry) + sourceentry->nlength - 1);

	/* copy comment */
	destcomment = COMMENT(destentry);
	*destcomment = strlen(comment);
	memcpy(destcomment + 1, comment, *destcomment);

	/* copy fields */
	if (g->dirextension)
		memcpy((UBYTE *)destentry + destfieldoffset, (UBYTE *)sourceentry + srcfieldoffset, fieldsize);

	/* set size */
	if (g->dirextension)
		destentry->next = (UBYTE)(destfieldoffset + fieldsize);
	else
		destentry->next = (UBYTE)destfieldoffset;

	/* remove old directoryentry and add new */
	if (!GetParent(info, &directory, error, g))
		return DOSFALSE;
	else
	{
		if (!ChangeDirEntry(info->file, destentry, &directory, &info->file, g))
		{
			*error = ERROR_DISK_FULL;
			return DOSFALSE;
		}
		return DOSTRUE;
	}
}

/* ProtectFile, SetDate
 *
 * - simple direntry in cache change, no change in size 
 * - CACHEDDIRENTRY must have changeflag
 *
 * maxneeds: changes 1 block. NEVER allocates new block
 */
BOOL ProtectFile(struct fileinfo * file, ULONG protection, SIPTR *error, globaldata * g)
{
	ENTER("ProtectFile");

	// isvolume check already done in dostohandler..
	//
	//  if (!file || !file->direntry)   /* @XLV */
	//  {
	//      *error = ERROR_OBJECT_WRONG_TYPE;
	//      return DOSFALSE;
	//  }

#if DELDIR
	if ((*(union objectinfo *)file).delfile.special <= SPECIAL_DELFILE)
	{
		if ((*(union objectinfo *)file).delfile.special == SPECIAL_DELDIR)
		{
			protection &= DELENTRY_PROT_AND_MASK;
			protection |= DELENTRY_PROT_OR_MASK;
			g->currentvolume->rblkextension->blk.dd_protection = protection;
			MakeBlockDirty((struct cachedblock *)g->currentvolume->rblkextension, g);
			return DOSTRUE;
		}

		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	file->direntry->protection = protection;

	/* add second part of protection */
	if (g->dirextension)
	{
		union objectinfo directory;
		struct direntry *sourceentry, *destentry;
		struct extrafields extrafields;
		UBYTE entrybuffer[MAX_ENTRYSIZE];

		/* make new direntry */
		destentry = (struct direntry *)entrybuffer;
		sourceentry = file->direntry;

		/* copy source */
		memcpy(destentry, sourceentry, sourceentry->next);

		/* set new extrafields */
		GetExtraFields(sourceentry, &extrafields);
		extrafields.prot = protection;
		AddExtraFields(destentry, &extrafields);

		/* commit changes */
		if (!GetParent((union objectinfo *)file, &directory, error, g))
			return DOSFALSE;
		else if (!ChangeDirEntry(*file, destentry, &directory, file, g))
		{
			*error = ERROR_DISK_FULL;
			return DOSFALSE;
		}
	}

	/* mark block for update and return success */
	MakeBlockDirty((struct cachedblock *)file->dirblock, g);
	return DOSTRUE;
}

BOOL SetOwnerID(struct fileinfo * file, ULONG owner, SIPTR *error, globaldata * g)
{
	struct extrafields extrafields;
	union objectinfo directory;
	struct direntry *sourceentry, *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];

	ENTER("SetOwnerID");
#if DELDIR
	if ((*(union objectinfo *)file).delfile.special <= SPECIAL_DELFILE)
	{
		if ((*(union objectinfo *)file).delfile.special == SPECIAL_DELDIR)
		{
			struct crootblockextension *rext = g->currentvolume->rblkextension;

			rext->blk.dd_uid = owner >> 16;
			rext->blk.dd_gid = owner & 0xffff;
			MakeBlockDirty ((struct cachedblock *)rext, g);
			return DOSTRUE;
		}

		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	if (!g->dirextension)
	{
		*error = ERROR_ACTION_NOT_KNOWN;
		return DOSFALSE;
	}

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* make new direntry */
	destentry = (struct direntry *)entrybuffer;
	sourceentry = file->direntry;

	/* copy source */
	memcpy(destentry, sourceentry, sourceentry->next);

	/* set new extrafields */
	GetExtraFields(sourceentry, &extrafields);
	extrafields.uid = owner >> 16;
	extrafields.gid = owner & 0xffff;
	AddExtraFields(destentry, &extrafields);

	/* commit changes */
	if (!GetParent((union objectinfo *)file, &directory, error, g))
		return DOSFALSE;
	else if (!ChangeDirEntry(*file, destentry, &directory, file, g))
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	MakeBlockDirty((struct cachedblock *)file->dirblock, g);
	return DOSTRUE;
}

LONG ReadSoftLink(union objectinfo *linkfi, const char *prefix, char *buffer, ULONG size, SIPTR *error, globaldata * g)
{
	struct canode anode;
	UBYTE *softblock;
	ULONG rc;
	ULONG prefixlen;
	ULONG postfixlen;
	ULONG linklen;
	const char *postfix;
	char *ptr = buffer;

	ENTER("ReadSoftLink");
	if (!linkfi || IsVolume(*linkfi))
	{
		*error = ERROR_OBJECT_WRONG_TYPE;
		return -1;
	}

	linklen = linkfi->file.direntry->fsize;
	if (linklen >= BLOCKSIZE)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return -1;
	}
	softblock = AllocVec(BLOCKSIZE, 0);
	if (!softblock) {
		*error = ERROR_NO_FREE_STORE;
		return -1;
	}

	/*
	* TODO: It might be a good idea to try to compress the resulting
	* path. For example "foo/bar/bleh///meh" should become "foo/meh".
	* FFS2 does this sort of thing for instance.
	*
	* Not doing this could lead to problems easily as many callers use
	* maximum buffer of around 256 bytes. - Piru
	*/

	if (!GetAnode(&anode, linkfi->file.direntry->anode, g))
	{
		FreeVec(softblock);
		*error = PFS_ERROR_INCONSISTENT;
		return -1;
	}
	if ((rc = DiskRead(softblock, 1, anode.blocknr, g)))
	{
		FreeVec(softblock);
		*error = rc;
		return -1;
	}
	if (softblock[linklen] != 0 || memchr(softblock, 0, linklen))
	{
		FreeVec(softblock);
		*error = PFS_ERROR_INCONSISTENT;
		return -1;
	}

	/* If link destination is absolute, ignore prefix */
	if (strchr(softblock, ':'))
	{
		prefixlen = 0;

		/* ...Unless if the link target begins with ':' and the original
		 * name has a volume/device/assign specifier. If so, the specifier
		 * needs to be used as a prefix. If this is not done links pointing
		 * to root of a volume/device/assign would point to different
		 * targets depending on the user's current directory. That'd be
		 * horribly wrong.
		 */
		if (softblock[0] == ':')
		{
			char *colonpos = strchr(prefix, ':');
			/* Note: if colon is at the beginning then prefixlen == 0
			 * which is correct.
			 */
			if (colonpos)
				prefixlen = colonpos - prefix;
		}
	}
	else
	{
		prefixlen = strlen(prefix);
	}

	postfix = g->unparsed;
	postfixlen = postfix ? strlen(postfix) : 0;

	/* if link destination ends in a /, skip the postfix / if any */
	if (linklen > 0 && softblock[linklen - 1] == '/')
	{
		if (postfixlen && *postfix == '/')
			{
				postfix++;
				postfixlen--;
			}
	}
	else
	{
		/* Else remove a lone trailing '/' in the postfix, if any */
		/* "/" becomes "" */
		/* "foo/" becomes "foo" */
		/* "foo//" becomes "foo//" */
		if (postfixlen > 0 && g->unparsed[postfixlen - 1] == '/' &&
		    (postfixlen == 1 || g->unparsed[postfixlen - 2] != '/'))
			postfixlen--;
	}

	if (prefixlen >= size || linklen > size - prefixlen - 1 ||
		postfixlen > size - prefixlen - linklen - 1) {
		FreeVec(softblock);
		return -2;
	}

	memcpy(ptr, (void*)prefix, prefixlen);
	ptr += prefixlen;
	memcpy(ptr, softblock, linklen);
	ptr += linklen;
	if (postfixlen)
		memcpy(ptr, postfix, postfixlen);
	ptr += postfixlen;
	*ptr = '\0';

	FreeVec(softblock);

	return (LONG) (ptr - buffer);
}

BOOL CreateSoftLink(union objectinfo *linkdir, STRPTR linkname, STRPTR softlink, union objectinfo *newlink, SIPTR *error, globaldata * g)
{
	ULONG anodenr;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct direntry *de;
	UBYTE *softblock = NULL;
	struct canode anode;
	struct anodechain *achain = NULL;
	size_t l;
	size_t targetlen;
	ULONG rc;
	LONG lookup;

	ENTER("CreateSoftLink");
#if DELDIR
	if (IsDelDir(*linkdir))
	{
		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return DOSFALSE;
	targetlen = strlen(softlink);
	if (targetlen >= BLOCKSIZE)
	{
		*error = ERROR_LINE_TOO_LONG;
		return DOSFALSE;
	}

	/* get anodenr of directory */
	if (!linkdir || IsVolume(*linkdir))
		anodenr = ANODE_ROOTDIR;
	else
	{
		anodenr = FIANODENR(&linkdir->file);
		LOCK(linkdir->file.dirblock);
	}

	/* truncate filename to 31 characters */
	if (!(l = strlen(linkname)))
	{
		*error = ERROR_INVALID_COMPONENT_NAME;
		return DOSFALSE;
	}
	if (l > FILENAMESIZE - 1)
		linkname[FILENAMESIZE - 1] = 0x0;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* check if a file by that name already exists */
	lookup = SearchInDir(anodenr, linkname, newlink, g);
	if (lookup < 0)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if (lookup)
	{
		*error = ERROR_OBJECT_EXISTS;
		return DOSFALSE;
	}

	softblock = AllocVec(BLOCKSIZE, MEMF_CLEAR);
	if (!softblock) {
		*error = ERROR_NO_FREE_STORE;
		return DOSFALSE;
	}

	/* make directory entry 
	 * the anode allocated is the link list element
	 */
	de = (struct direntry *)entrybuffer;
	if (!MakeDirEntry(ST_SOFTLINK, linkname, entrybuffer, g))
	{
		*error = ERROR_DISK_FULL;
		goto error0;
	}

	/* store directoryentry */
	if (!AddDirectoryEntry(linkdir, (struct direntry *)entrybuffer, &newlink->file, g))
	{
		*error = ERROR_DISK_FULL;
		goto error1;
	}

	/* fill directory entry */
	if (!(achain = GetAnodeChain(de->anode, g)))
	{
		*error = ERROR_NO_FREE_STORE;
		goto error2;
	}
	if (!AllocateBlocksAC(achain, 1, &newlink->file, g))
	{
		LOCK(newlink->file.dirblock);
		*error = ERROR_DISK_FULL;
		goto error_alloc;
	}
	LOCK(newlink->file.dirblock);

	if (!GetAnode(&anode, de->anode, g))
	{
		*error = PFS_ERROR_INCONSISTENT;
		goto error3;
	}
	memcpy(softblock, softlink, targetlen);
	if ((rc = DiskWrite(softblock, 1, anode.blocknr, g)))
	{
		*error = rc;
		goto error3;
	}
	newlink->file.direntry->fsize = targetlen;
	MakeBlockDirty((struct cachedblock *)newlink->file.dirblock, g);
	DetachAnodeChain(achain, g);
	FreeVec(softblock);
	return DOSTRUE;

	/* errors */
  error3:
	RemoveDirEntry(newlink->file, g);
	FreeBlocksAC(achain, ULONG_MAX, freeanodes, g);
	DetachAnodeChain(achain, g);
	goto error1;
  error_alloc:
	DetachAnodeChain(achain, g);
  error2:
	RemoveDirEntry(newlink->file, g);
  error1:
	FreeAnode(de->anode, g);
  error0:
  	FreeVec(softblock);

	return DOSFALSE;
}

/*
 * linkdir: directory (in) 
 * linkname: name (in)
 * object: object to link to (in) (dir must be locked)
 * newlink: result (out)
 */
BOOL CreateLink(union objectinfo * linkdir, STRPTR linkname, union objectinfo * object,
				union objectinfo * newlink, SIPTR *error, globaldata * g)
{
	union objectinfo info, odi;
	ULONG anodenr, linklist;
	struct direntry *objectentry, *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct extrafields extrafields;
	struct canode linknode, head;
	size_t l;
	LONG lookup;

	ENTER("CreateLink");
#if DELDIR
	if (IsDelDir(*linkdir) || IsDelDir(*object) || IsDelFile(*object))
	{
		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	/* only files and directories can be linked to (not the volume) */
	if (!IsFile(*object) && !IsRealDir(*object))
	{
		*error = ERROR_OBJECT_WRONG_TYPE;
		return DOSFALSE;
	}

	/* check if operation possible */
	if (!g->dirextension)
	{
		*error = ERROR_ACTION_NOT_KNOWN;
		return DOSFALSE;
	}

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return DOSFALSE;

	/* get anodenr */
	if (!linkdir || IsVolume(*linkdir))
	{
		anodenr = ANODE_ROOTDIR;
	}
	else
	{
		anodenr = FIANODENR(&linkdir->file);
		LOCK(linkdir->file.dirblock);
	}

	/* truncate filename to 31 characters */
	if (!(l = strlen(linkname)))
	{
		*error = ERROR_INVALID_COMPONENT_NAME;
		return DOSFALSE;
	}
	if (l > FILENAMESIZE - 1)
		linkname[FILENAMESIZE - 1] = 0x0;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* check if a file by that name already exists */
	lookup = SearchInDir(anodenr, linkname, &info, g);
	if (lookup < 0)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if (lookup)
	{
		*error = ERROR_OBJECT_EXISTS;
		return DOSFALSE;
	}

	/* make directory entry 
	 * the anode allocated is the link list element
	 */
	if (!MakeDirEntry(ST_LINKDIR, linkname, entrybuffer, g))
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* add link info */
	destentry = (struct direntry *)entrybuffer;
	objectentry = object->file.direntry;
#if MULTIUSER
	GetExtraFields(destentry, &extrafields);
#else
	memset(&extrafields, 0, sizeof(struct extrafields));
#endif
	extrafields.link = objectentry->anode;
	AddExtraFields(destentry, &extrafields);

	/* copy object info */
	destentry->fsize = GetDEFileSize32(objectentry, g);
	switch (objectentry->type)
	{
		case ST_FILE:
		case ST_SOFTLINK:
		case ST_ROLLOVERFILE:
		case ST_LINKFILE:
			destentry->type = ST_LINKFILE;
			break;
		
		default:
			destentry->type = ST_LINKDIR;
			break;
	}

	/* store directoryentry */
	if (!AddDirectoryEntry(linkdir, destentry, &newlink->file, g))
	{
		FreeAnode(destentry->anode, g);
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* make linknode; a later link goes right after the first one */
	GetExtraFields(objectentry, &extrafields);
	linklist = extrafields.link;
	linknode.clustersize = object->file.dirblock->blk.anodenr;
	linknode.blocknr = newlink->file.dirblock->blk.anodenr;
	linknode.next = 0;
	if (linklist)
	{
		if (!GetAnode(&head, linklist, g))
			goto link_read_error;
		linknode.next = head.next;
	}
	if (!SaveAnode(&linknode, newlink->file.direntry->anode, g))
	{
		ULONG linkanode = newlink->file.direntry->anode;
		RemoveDirEntry(newlink->file, g);
		FreeAnode(linkanode, g);
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}

	/* change objectentry */
	if (!linklist)
	{
		/* there were no links yet ->
		 * add link field. Use entrybuffer and destentry pointer
		 */
		extrafields.link = linklist = newlink->file.direntry->anode;
		memcpy(entrybuffer, objectentry, objectentry->next);
		AddExtraFields(destentry, &extrafields);

		if (!GetParent(object, &odi, error, g))
		{
			ULONG linkanode = newlink->file.direntry->anode;
			RemoveDirEntry(newlink->file, g);
			FreeAnode(linkanode, g);
			return DOSFALSE;
		}

		if (!ChangeDirEntry(object->file, destentry, &odi, &object->file, g))
		{
			/* like the GetParent failure above: the new link entry
			 * stays, but the operation is reported as failed */
			*error = ERROR_DISK_FULL;
			g->currentvolume->metadata_error = TRUE;
			return DOSFALSE;
		}
	}
	else
	{
		/* add new link after the first one */
		head.next = newlink->file.direntry->anode;
		if (!SaveAnode(&head, head.nr, g))
		{
			ULONG linkanode = newlink->file.direntry->anode;
			RemoveDirEntry(newlink->file, g);
			FreeAnode(linkanode, g);
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;
		}
	}

	return DOSTRUE;

link_read_error:
	{
		ULONG linkanode = newlink->file.direntry->anode;
		RemoveDirEntry(newlink->file, g);
		FreeAnode(linkanode, g);
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
}

BOOL SetDate(union objectinfo *file, struct DateStamp *date, SIPTR *error, globaldata *g)
{
	ENTER("SetDate");

#if DELDIR
	if (file->deldir.special <= SPECIAL_DELFILE)
	{
		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	if (!CheckVolume(file->file.dirblock->volume, 1, error, g))
		return DOSFALSE;

	file->file.direntry->creationday = (UWORD)date->ds_Days;
	file->file.direntry->creationminute = (UWORD)date->ds_Minute;
	file->file.direntry->creationtick = (UWORD)date->ds_Tick;
	MakeBlockDirty((struct cachedblock *)file->file.dirblock, g);
	return DOSTRUE;
}

void Touch(struct fileinfo *info, globaldata * g)   // ook archiveflag..
 {
	struct DateStamp time;

	DateStamp(&time);

#if VERSION23
	if (IsVolume(*((union objectinfo *)info)) &&
		g->currentvolume->rblkextension)
	{
		g->currentvolume->rblkextension->blk.root_date[0] = (UWORD)time.ds_Days;
		g->currentvolume->rblkextension->blk.root_date[1] = (UWORD)time.ds_Minute;
		g->currentvolume->rblkextension->blk.root_date[2] = (UWORD)time.ds_Tick;
		MakeBlockDirty((struct cachedblock *)g->currentvolume->rblkextension, g);
	}
	else
#endif

	if (!IsVolume(*((union objectinfo *)info)))
	{
		info->direntry->creationday = (UWORD)time.ds_Days;
		info->direntry->creationminute = (UWORD)time.ds_Minute;
		info->direntry->creationtick = (UWORD)time.ds_Tick;
		info->direntry->protection &= ~FIBF_ARCHIVE;    // clear archivebit (eor)

		MakeBlockDirty((struct cachedblock *)info->dirblock, g);
	}
}


#if ROLLOVER
/*
 * dir: directory (in) 
 * rollname: name of rollover file (in)
 * result: created rolloverfile (out)
 */
BOOL CreateRollover(union objectinfo *dir, STRPTR rollname, ULONG size,
					union objectinfo *result, SIPTR *error, globaldata * g)
{
	ULONG anodenr;
	struct direntry *de;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct anodechain *ac;
	size_t l;
	LONG lookup;

	DB(Trace(1, "CreateRollover", "name %s size %d \n", rollname, size));
#if DELDIR
	/* no write access to deldir */
	if (IsDelDir(*dir))
	{
		*error = ERROR_WRITE_PROTECTED;
		return DOSFALSE;
	}
#endif

	/* check if operation possible */
	if (!g->dirextension)
	{
		*error = ERROR_ACTION_NOT_KNOWN;
		return DOSFALSE;
	}

	/* check size */
	if (!size || size > (0xffffffffUL >> BLOCKSHIFT))
	{
		*error = ERROR_BAD_NUMBER;
		return DOSFALSE;
	}

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return DOSFALSE;

	/* get anodenr of directory */
	if (!dir || IsVolume(*dir))
		anodenr = ANODE_ROOTDIR;
	else
	{
		anodenr = FIANODENR(&dir->file);
		LOCK(dir->file.dirblock);
	}

	/* truncate filename to 31 characters */
	if (!(l = strlen(rollname)))
	{
		*error = ERROR_INVALID_COMPONENT_NAME;
		return DOSFALSE;
	}
	if (l > FILENAMESIZE - 1)
		rollname[FILENAMESIZE - 1] = 0x0;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* check if a file by that name already exists */
	lookup = SearchInDir(anodenr, rollname, result, g);
	if (lookup < 0)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	if (lookup)
	{
		*error = ERROR_OBJECT_EXISTS;
		return DOSFALSE;
	}

	/* make directory entry 
	 * the anode allocated is the link list element
	 */
	de = (struct direntry *)entrybuffer;
	if (!MakeDirEntry(ST_ROLLOVERFILE, rollname, entrybuffer, g))
	{
		*error = ERROR_DISK_FULL;
		return DOSFALSE;
	}

	/* add rollover info is not necessary, since
	 * both virtualsize and rollpointer are initially
	 * zero
	 */
	if (!(ac = GetAnodeChain(de->anode, g)))
		goto error1;

	/* store directoryentry */
	if (!AddDirectoryEntry(dir, de, &result->file, g))
		goto error2;

	/* allocate blocks
	 * in case of an intermediate update, the rollover file will
	 * be temporarily committed smaller
	 */
	if (!(AllocateBlocksAC(ac, size, &result->file, g)))
		goto error3;

	/* set real size 
	 */
	result->file.direntry->fsize = size << BLOCKSHIFT;
	return DOSTRUE;

	/* errors */
  error3:
	RemoveDirEntry(result->file, g);
  error2:
	DetachAnodeChain(ac, g);
  error1:
	FreeAnode(de->anode, g);
	*error = ERROR_DISK_FULL;
	return DOSFALSE;
}

/* changes/reads rollover information of rollfile using roinfo. Objectinfo of
 * 'rollfile' is updated, if so needed. Roinfo is always filled with the current
 * rollfile settings (AFTER update). The real filesize is only changed when the
 * realsize fiels is unequal zero.
 */
ULONG SetRollover(fileentry_t *rollfile, struct rolloverinfo *roinfo, globaldata *g)
{
	union objectinfo directory;
	struct extrafields extrafields;
	struct direntry *sourcede, *destde;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	SIPTR error;
	ULONG realsize;

	/* check if file is rollover file */
	if (!IsRollover(rollfile->le.info))
		return ERROR_OBJECT_WRONG_TYPE;

	destde = (struct direntry *)entrybuffer;
	sourcede = rollfile->le.info.file.direntry;
	GetExtraFields(sourcede, &extrafields);
	if (roinfo->set)
	{
		if (roinfo->realsize)
		{
			realsize = roinfo->realsize & ~BLOCKSIZEMASK;
			if (!realsize) realsize = BLOCKSIZE;
		}
		else
			realsize = sourcede->fsize;

		/* virtualsize and rollpointer can not extend past end
		 * of file. virtualsize has to be <= realsize - 1
		 */
		if (roinfo->rollpointer > realsize ||
			roinfo->virtualsize >= realsize)
			return ERROR_SEEK_ERROR;

		if (roinfo->realsize && realsize != sourcede->fsize)
		{
#if !LARGE_FILE_SIZE
			/* ChangeFileSize takes a signed 32-bit absolute position in
			 * this build, so larger sizes cannot be passed to it safely.
			 */
			if (realsize > 0x7fffffffUL)
				return ERROR_OBJECT_TOO_LARGE;
#endif
			error = 0;
			if (ChangeFileSize(rollfile, realsize, OFFSET_BEGINNING, &error, g) == -1)
				return error ? error : ERROR_DISK_FULL;
		}
		roinfo->realsize = sourcede->fsize;
		memcpy(destde, sourcede, sourcede->next);
		extrafields.virtualsize = roinfo->virtualsize;  
		extrafields.rollpointer = roinfo->rollpointer;
		AddExtraFields(destde, &extrafields);

		if (!GetParent(&rollfile->le.info, &directory, &error, g))
			return error;
		else if (!ChangeDirEntry(rollfile->le.info.file, destde, &directory,
						   &rollfile->le.info.file, g))
			return ERROR_DISK_FULL;
	}
	else
	{
		roinfo->realsize = sourcede->fsize;
		roinfo->virtualsize = extrafields.virtualsize;
		roinfo->rollpointer = extrafields.rollpointer;
	}

	return 0;
}

#endif /* ROLLOVER */

/**********************************************************************/
/*                         LL CHANGE DIRENTRY                         */
/*                         LL CHANGE DIRENTRY                         */
/*                         LL CHANGE DIRENTRY                         */
/**********************************************************************/


/* Change a directoryentry. Covers all reference changing too

 * If direntry==NULL no new direntry is to be added, only removed.
 * result may be NULL then as well
 *
 * in: from, to, destdir
 * out: result
 *
 * from can become INVALID.., from.dirblock can be freed
 */

BOOL ChangeDirEntry(struct fileinfo from, struct direntry *to,
		   union objectinfo *destdir, struct fileinfo *result, globaldata * g)
{
	ULONG destanodenr = IsRoot(destdir) ? ANODE_ROOTDIR : FIANODENR(&destdir->file);

	/* check whether a 'within dir' rename */
	if (to && destanodenr == from.dirblock->blk.anodenr)
		return RenameWithinDir(from, to, result, g);
	else
		return RenameAcrossDirs(from, to, destdir, result, g);
}

/*
 * Move a file from one dir to another
 * NULL = delete allowed
 * Returns FALSE (with source intact and nothing added) if the new
 * entry could not be added.
 */
static BOOL RenameAcrossDirs(struct fileinfo from, struct direntry *to,
		   union objectinfo *destdir, struct fileinfo *result, globaldata * g)
{
	UWORD removedlen;
	BOOL empty;

	/* may not be evicted while AddDirectoryEntry loads other blocks */
	LOCK(from.dirblock);

	if (to)
	{
		/* Add the new entry BEFORE removing the old one: if adding
		 * fails (e.g. no reserved block for a fresh dirblock), the
		 * source entry is still present and the operation fails
		 * cleanly instead of orphaning the object and leaving
		 * 'result' uninitialized. Source and destination directory are
		 * different, so 'destdir' cannot be invalidated by the removal
		 * below.
		 */
		if (!AddDirectoryEntry(destdir, to, result, g))
			return FALSE;
	}

	/* remove old entry; free its block if that leaves it empty and
	 * nothing points into it any more
	 */
	removedlen = from.direntry->next;
	empty = CutDirEntry(from, g);
	if (UpdateChangedRef(from, result, -removedlen, g) || !empty ||
		FreeEmptyDBlk((struct cachedblock *)from.dirblock, g) <= 0)
		MakeBlockDirty((struct cachedblock *)from.dirblock, g);
	if (result)
		LOCK(result->dirblock);
	return TRUE;
}

/*
 * Rename file within dir
 * NULL destination not allowed
 * Returns FALSE (with the source entry still present, though possibly
 * moved within the directory) if space could not be made.
 */
static BOOL RenameWithinDir(struct fileinfo from, struct direntry *to,
							struct fileinfo *result, globaldata * g)
{
	int spaceneeded;
	struct fileinfo mover;

	LOCK(from.dirblock);
	mover.direntry = FIRSTENTRY(from.dirblock);
	mover.dirblock = from.dirblock;
	spaceneeded = to->next - from.direntry->next;
	if (spaceneeded <= 0)
		RenameInPlace(from, to, result, g);
	else
	{
		/* make space in block
		 */
		while (!CheckFit(from.dirblock, spaceneeded, g) && from.direntry != mover.direntry)
		{
			from.direntry = (struct direntry *)((UBYTE *)from.direntry - mover.direntry->next);
			if (!MoveToPrevious(mover, mover.direntry, result, g))
				return FALSE;
		}

		if (CheckFit(from.dirblock, spaceneeded, g))
			RenameInPlace(from, to, result, g);
		else
		{
			if (!MoveToPrevious(from, to, result, g))
				return FALSE;
		}
	}

	LOCK(result->dirblock);
	return TRUE;
}



/*
 * Check if direntry will fit in,
 * returns position to place it if ok
 */
static struct direntry *CheckFit(struct cdirblock *blok, int needed, globaldata * g)
{
	struct direntry *entry;
	int i;

	/* goto end of dirblock */
	entry = FIRSTENTRY(blok);
	for (i = 0; entry->next; entry = NEXTENTRY(entry))
		i += entry->next;

	if (needed + i + 1 <= DB_ENTRYSPACE)
		return entry;
	else
		return NULL;
}

/*
 * Moves firstentry to previous block, changing it to to. To can point to de.direntry
 * if wanted.
 * Return new fileinfo in 'result'
 * NB: no need to touch parent: MTP is always followed by another function
 * on the block.
 */
static BOOL MoveToPrevious(struct fileinfo de, struct direntry *to, struct fileinfo *result, globaldata * g)
{
	struct direntry *dest;
	struct cdirblock *prevblock;
	struct canode anode;
	int removedlen;
	ULONG prev;

	LOCK(de.dirblock);

	/* get previous block */
	if (!GetAnode(&anode, de.dirblock->blk.anodenr, g))
		return FALSE;
	prev = 0;
	while (anode.blocknr != de.dirblock->blocknr && anode.next)
	{
		prev = anode.nr;
		if (!GetAnode(&anode, anode.next, g))
			return FALSE;
	}

	/* savety check */
	if (anode.blocknr != de.dirblock->blocknr)
	{
		ErrorMsg(AFS_ERROR_CACHE_INCONSISTENCY, NULL, g);
		return FALSE;
	}

	/* Get dirblock in question
	 * Special case : previous == 0!!->add new head!!
	 */
	if (prev)
	{
		if (!GetAnode(&anode, prev, g))
			return FALSE;
		if (!(prevblock = LoadDirBlock(anode.blocknr, g)))
			return FALSE;
	}

	/* Add new entry */
	if (prev && (dest = CheckFit(prevblock, to->next, g)))
	{
		memcpy(dest, to, to->next);
		*(UBYTE *)NEXTENTRY(dest) = 0;  /* end of dirblock */
		result->direntry = dest;
		result->dirblock = prevblock;
	}
	else
	{
		/* make new dirblock .. */
		ULONG parent;
		struct canode newanode;
		struct cdirblock *newblock;
		ULONG newanodenr;

		newanode.clustersize = 1;
		parent = de.dirblock->blk.parent;
		if (!(newanode.blocknr = AllocReservedBlock(g)))
			return FALSE;

		if (!prev)
		{
			if (!GetAnode(&anode, de.dirblock->blk.anodenr, g))
			{
				FreeReservedBlock(newanode.blocknr, g);
				return FALSE;
			}
			if (!(newanodenr = AllocAnode (anode.next ? anode.next : anode.nr, g)))
			{
				FreeReservedBlock (newanode.blocknr, g);
				return FALSE;
			}
			newanode.nr = anode.nr;
			newanode.next = anode.nr = newanodenr;
		}
		else
		{
			if (!(newanodenr = AllocAnode (anode.nr, g)))
			{
				FreeReservedBlock (newanode.blocknr, g);
				return FALSE;
			}
			newanode.nr = newanodenr;
			newanode.next = anode.next;
			anode.next = newanode.nr;
		}

		if (!SaveAnode(&anode, anode.nr, g))
		{
			FreeAnode(newanodenr, g);
			FreeReservedBlock(newanode.blocknr, g);
			return FALSE;
		}
		newblock = MakeDirBlock(newanode.blocknr, newanode.nr, de.dirblock->blk.anodenr, parent, g);
		if (!newblock)
		{
			/* undo the chain changes: the head anode itself was not
			 * rewritten yet (newanode is saved only after MakeDirBlock)
			 */
			if (!prev)
			{
				/* the old head contents were copied to newanodenr;
				 * the head anode still has its original contents */
				FreeAnode (newanodenr, g);
			}
			else
			{
				anode.next = newanode.next;
				if (!SaveAnode(&anode, anode.nr, g))
					return FALSE;
				FreeAnode (newanodenr, g);
			}
			FreeReservedBlock (newanode.blocknr, g);
			return FALSE;
		}
		if (!SaveAnode(&newanode, newanode.nr, g))
			return FALSE;

		/* add entry */
		dest = FIRSTENTRY(newblock);
		memcpy(dest, to, to->next);
		*(UBYTE *)NEXTENTRY(dest) = 0;  /* end of dirblock */
		result->direntry = dest;
		result->dirblock = newblock;
	}

	LOCK(result->dirblock);
	MakeBlockDirty((struct cachedblock *)result->dirblock, g);

	/* remove old entry & make blocks dirty */
	removedlen = de.direntry->next;
	RemoveDirEntry(de, g);

	/* update references */
	UpdateChangedRef(de, result, -removedlen, g);
	return TRUE;
}

/*
 * There HAS to be sufficient space!!
 */
static void RenameInPlace(struct fileinfo from, struct direntry *to, struct fileinfo *result, globaldata * g)
{
	UBYTE *dest, *start, *end;
	SIPTR error;
	ULONG movelen;
	int diff;
	union objectinfo parent;

	LOCK(from.dirblock);

	/* change date parent */
	if (GetParent((union objectinfo *)&from, &parent, &error, g))
		Touch(&parent.file, g);

	/* make place for new entry */
	diff = to->next - from.direntry->next;
	dest = (UBYTE *)from.direntry + to->next;
	start = (UBYTE *)from.direntry + from.direntry->next;
	end = (UBYTE *)&(from.dirblock->blk) + g->rootblock->reserved_blksize;
	movelen = (diff > 0) ? (end - dest) : (end - start);
	memmove(dest, start, movelen);

	/* fill in new entry */
	memcpy((UBYTE *)from.direntry, to, to->next);

	/* fill in result and make block dirty */
	*result = from;
	MakeBlockDirty((struct cachedblock *)from.dirblock, g);

	/* update references */
	UpdateChangedRef(from, result, diff, g);
}

/* RemoveDirEntry
 *
 * Simply shift the directryentry out with memmove(dest, src, len)
 * References are not corrected (see changedirentry)
 * 
 * makes all fileinfo's in same block invalid !!
 */
static void RemoveDirEntry(struct fileinfo info, globaldata * g)
{
	CutDirEntry(info, g);
	MakeBlockDirty((struct cachedblock *)info.dirblock, g);     // %6.2
}

/* Removes the entry without marking its block dirty; TRUE if the block is
 * empty now
 */
static BOOL CutDirEntry(struct fileinfo info, globaldata * g)
{
	UBYTE *endofblok, *startofblok, *destofblok, *startofclear;
	UWORD clearlen;
	SIPTR error;
	union objectinfo parent;

	LOCK(info.dirblock);

	/* change date parent %6.5 */
	if (GetParent((union objectinfo *)&info, &parent, &error, g))
		Touch(&parent.file, g);

	/* remove direntry */
	destofblok = (UBYTE *)info.direntry;
	startofblok = destofblok + info.direntry->next;
	endofblok = (UBYTE *)&(info.dirblock->blk) + g->rootblock->reserved_blksize;
	startofclear = endofblok - info.direntry->next;
	clearlen = info.direntry->next;
	memmove(destofblok, startofblok, endofblok - startofblok);

	/* makes info invalid!! */
	if (info.direntry->next)
		memset(startofclear, 0, clearlen);
	return !FIRSTENTRY(info.dirblock)->next;
}


/* AddDirectoryEntry
 *
 * Add a directoryentry to a directory.
 * Tries to add the directoryentry at the end of an existing directoryblock. If
 * that fails, create a new one.
 *
 * Operates on currentvolume
 *
 * input : - dir: directory to add directoryentry too
 *          - newentry: the new directoryentry
 *
 * output: - newinfo: pointer to direntry and directoryblock the entry
 *          was added to
 *
 * NB: A) there should ALWAYS be at least one dirblock
 *     B) assumes CURRENTVOLUME 
 */
static BOOL AddDirectoryEntry(union objectinfo *dir, struct direntry *newentry,
							  struct fileinfo *newinfo, globaldata * g)
{
	struct canode anode;
	ULONG anodeoffset = 0, diranodenr;
	struct cdirblock *blok;
	struct direntry *entry = NULL;
	BOOL done = FALSE, eof = FALSE;
	UCOUNT i;

	if (!dir || IsVolume(*dir))
		diranodenr = ANODE_ROOTDIR;
	else
		diranodenr = dir->file.direntry->anode;

	/* check if space in existing dirblocks */
	if (!GetAnode(&anode, diranodenr, g))
		return FALSE;
	while (!done && !eof)
	{
		if (!(blok = LoadDirBlock(anode.blocknr + anodeoffset, g)))
			break;

		entry = (struct direntry *)&blok->blk.entries;

		/* goto end of dirblock; i = aantal gebruikte bytes */
		for (i = 0; entry->next; entry = NEXTENTRY(entry))
			i += entry->next;

		/* does it fit in this block? (keep space for trailing 0) */
		if (i + newentry->next + 1 < DB_ENTRYSPACE)
		{
			memcpy(entry, newentry, newentry->next);
			*(UBYTE *)NEXTENTRY(entry) = 0;     // dirblock afsluiten

			done = TRUE;
			break;
		}
		if (!NextBlock(&anode, &anodeoffset, g))
		{
			if (anode.blocknr == (ULONG)~0UL)
				return FALSE;
			eof = TRUE;
		}
	}

	/* a dirblock in the chain could not be loaded: fail (adding the
	 * entry to a truncated view of the directory could corrupt it)
	 */
	if (!done && !eof)
		return FALSE;

	/* no->new dirblock (eof <=> anode is end of chain)
	 * We will make the new dirblock at the >start< of
	 * the chain.
	 * We always allocate new anode
	 */
	if (!done && eof)
	{
		ULONG parent;
		struct canode newanode;
		ULONG newanodenr;

		newanode.clustersize = 1;
		parent = blok->blk.parent;
		if (!(newanode.blocknr = AllocReservedBlock(g)))
			return FALSE;
		if (!GetAnode(&anode, diranodenr, g))
		{
			FreeReservedBlock(newanode.blocknr, g);
			return FALSE;
		}
		if (!(newanodenr = AllocAnode (anode.next ? anode.next : anode.nr, g)))
		{
			FreeReservedBlock (newanode.blocknr, g);
			return FALSE;
		}
		newanode.nr = diranodenr;
		newanode.next = anode.nr = newanodenr;
		if (!SaveAnode(&anode, anode.nr, g))
		{
			FreeAnode(newanodenr, g);
			FreeReservedBlock(newanode.blocknr, g);
			return FALSE;
		}
		blok = MakeDirBlock (newanode.blocknr, newanode.nr, diranodenr, parent, g);
		if (!blok)
		{
			/* the old head contents were saved under newanodenr, but
			 * the head anode (diranodenr) itself is unchanged - just
			 * discard the copy
			 */
			FreeAnode (newanodenr, g);
			FreeReservedBlock (newanode.blocknr, g);
			return FALSE;
		}
		if (!SaveAnode(&newanode, newanode.nr, g))
			return FALSE;
		entry = (struct direntry *)&blok->blk.entries;
		memcpy(entry, newentry, newentry->next);
		*(UBYTE *)NEXTENTRY(entry) = 0;     // mark end of dirblock

	}

	/* fill newinfo */
	newinfo->direntry = entry;
	newinfo->dirblock = blok;

	/* update notify */
	PFSUpdateNotify(blok->blk.anodenr, &entry->nlength, entry->anode, g);
	if (blok)
	{
		LOCK(blok);
		MakeBlockDirty((struct cachedblock *)blok, g);

	}
	Touch(&dir->file, g);
	return TRUE;
}


/*
 * Update references
 * diff is direntry size difference (new - original)
 * Returns TRUE if a reference still points into from.dirblock.
 */
static BOOL UpdateChangedRef(struct fileinfo from, struct fileinfo *to, int diff, globaldata * g)
{
	struct volumedata *volume = from.dirblock->volume;
	listentry_t *fe;
	BOOL held = FALSE;

	for (fe = HeadOf(&volume->fileentries); fe->next; fe = fe->next)
	{
		/* only dirs and files can be in a directory, but the volume *
		 * of volumeinfos can never point to a cached block, so a 
		 * type != ETF_VOLUME check is not necessary. Just check the
		 * dirblock pointer
		 */
		if (fe->info.file.dirblock == from.dirblock)
		{
			/* is het de targetentry ? */
			if (fe->info.file.direntry == from.direntry)
			{
				if (to)
					fe->info.file = *to;
			}
			else
			{
				/* take only entries after target */
				if (fe->info.file.direntry > from.direntry)
				{
					fe->info.file.direntry = (struct direntry *)
						((UBYTE *)fe->info.file.direntry + diff);
				}
			}
		}

		/* check for exnext references */
		if (fe->type.flags.dir)
		{
			lockentry_t *dle = (lockentry_t *)fe;

			if (dle->nextentry.dirblock == from.dirblock)
			{
				if ((dle->nextentry.direntry == from.direntry)
					&& (!dle->nextentry.direntry->next))
				{
					GetNextEntry(dle, g);
				}
				else
				{
					/* take only entries after target */
					if (dle->nextentry.direntry > from.direntry)
					{
						dle->nextentry.direntry = (struct direntry *)
							((UBYTE *)dle->nextentry.direntry + diff);
					}
				}
			}
			if (dle->nextentry.dirblock == from.dirblock)
				held = TRUE;
		}
		if (fe->info.file.dirblock == from.dirblock)
			held = TRUE;
	}
	return held;
}


/* MakeDirEntry
 *
 * Used by L2.NewFile, L2.NewDir
 *
 * Make a new directoryentry. The filename is not checked. Allocates anode
 * for file/dir
 *
 * input : 
 *        - type: ST_FILE, ST_DIR etc ..
 *        - name: objectname
 *        - entrybuffer: place to put direntry (char buffer of size MAX_ENTRYSIZE)
 *
 * output: - info: objectinfo of new directoryentry
 */
static BOOL MakeDirEntry(BYTE type, UBYTE *name, UBYTE *entrybuffer, globaldata * g)
{
	UWORD entrysize;
	struct direntry *direntry;
	struct DateStamp time;
	MUFS(struct extrafields extrafields);

	entrysize = ((sizeof(struct direntry) + strlen(name)) & 0xfffe);
	if (g->dirextension)
		entrysize += 2;
	direntry = (struct direntry *)entrybuffer;
	memset(direntry, 0, entrysize);

#if MULTIUSER
	if (g->muFS_ready)
	{
		extrafields.link = 0;
		extrafields.uid = g->user->uid;
		extrafields.gid = g->user->gid;
		extrafields.prot = muGetDefProtection(g->action->dp_Port->mp_SigTask);
		direntry->protection = extrafields.prot;
		extrafields.prot &= 0xffffff00;
	}
#endif

	DateStamp(&time);
	if (!(direntry->anode = AllocAnode(0, g)))
		return FALSE;

	direntry->next = entrysize;
	direntry->type = type;
	// direntry->fsize        = 0;
	direntry->creationday = (UWORD)time.ds_Days;
	direntry->creationminute = (UWORD)time.ds_Minute;
	direntry->creationtick = (UWORD)time.ds_Tick;
	// direntry->protection  = 0x00;    // RWED
	direntry->nlength = strlen(name);

	// the trailing 0 of strcpy() creates the empty comment!
	// the flags field following this is 0 by the memset call
	strcpy((UBYTE *)&direntry->startofname, name);

#if MULTIUSER
	if (g->dirextension && g->muFS_ready)
		AddExtraFields(direntry, &extrafields);
#endif

	return TRUE;
}

/**********************************************************************/
/*                              LOWLEVEL                              */
/*                              LOWLEVEL                              */
/*                              LOWLEVEL                              */
/**********************************************************************/

/* NULL => failure 
 * The loaded dirblock is locked immediately (prevents flushing)
 * 
 */
struct cdirblock *LoadDirBlock(ULONG blocknr, globaldata * g)
{
	struct cdirblock *dirblk;
	struct volumedata *volume = g->currentvolume;

	DB(Trace(1, "LoadDirBlock", "loading block %lx\n", blocknr));
	// -I- check if already in cache
	if (!(dirblk = (struct cdirblock *)CheckCache(volume->dirblks, HASHM_DIR, blocknr, g)))
	{
		// -II- not in cache -> put it in
		if (!(dirblk = (struct cdirblock *)AllocLRU(g)))
			return NULL;

		DB(Trace(10, "LoadDirBlock", "loading block %lx from disk\n", blocknr));
		if (RawRead((UBYTE *)&dirblk->blk, RESCLUSTER, blocknr, g) == 0)
		{
			if (dirblk->blk.id == DBLKID)
			{
				struct direntry *entry = FIRSTENTRY(dirblk);
				ULONG offset = 0;

				while (entry->next)
				{
					ULONG commentoffset;

					if ((entry->next & 1) != 0 ||
						entry->next < sizeof(struct direntry) ||
						entry->next > MAX_ENTRYSIZE ||
						offset + entry->next + 1 > DB_ENTRYSPACE)
					{
						FreeLRU((struct cachedblock *)dirblk);
						return NULL;
					}
					/* Every consumer copies or rebuilds entries in a
					 * MAX_ENTRYSIZE buffer. Bound both variable strings
					 * before caching the block, including the comment
					 * length byte itself. Do not use the configured name
					 * limit: existing names may predate a limit change.
					 */
					commentoffset = offsetof(struct direntry, startofname) + entry->nlength;
					if (entry->nlength >= FNSIZE || commentoffset >= entry->next ||
						*COMMENT(entry) >= CMSIZE ||
						commentoffset + 1 + *COMMENT(entry) > entry->next)
					{
						FreeLRU((struct cachedblock *)dirblk);
						return NULL;
					}
					offset += entry->next;
					entry = NEXTENTRY(entry);
				}

				dirblk->volume = g->currentvolume;
				dirblk->blocknr = blocknr;
				dirblk->used = FALSE;
				dirblk->changeflag = FALSE;
				Hash(dirblk, volume->dirblks, HASHM_DIR);
				UpdateReference(blocknr, dirblk, g);    // %10
			}
			else
			{
				ULONG args[2];
				args[0] = dirblk->blk.id;
				args[1] = blocknr;
				FreeLRU((struct cachedblock *)dirblk);
				ErrorMsg(AFS_ERROR_DNV_WRONG_DIRID, args, g);
				return NULL;
			}
		}
		else
		{
			FreeLRU((struct cachedblock *)dirblk);
			DB(Trace(5, "LoadDirBlock", "loading block %lx failed\n", blocknr));
			// ErrorMsg(AFS_ERROR_DNV_LOAD_DIRBLOCK, NULL, g);    // #$%^&??
			// DebugOn;DebugMsgNum("blocknr", blocknr);
			return NULL;
		}
	}

	EXIT("LoadDirBlock");
	return dirblk;
}

/* TRUE (with *error set) if 'child' is or lies inside 'parent', or if the
 * chain of parents is broken or cyclic. FALSE if it does not.
 */
static BOOL IsChildOf(union objectinfo child, union objectinfo parent, SIPTR *error, globaldata * g)
{
	union objectinfo up;
	ULONG mark = 0, steps = 0;

	while (!IsSameOI(child, parent))
	{
		if (!GetParent(&child, &up, error, g))
			return *error != 0;		/* 0: reached the root */
		if ((IPTR)up.file.direntry > SPECIAL_FLUSHED)
		{
			/* Brent's cycle detection on the anode of each directory,
			 * like GetNextAnode: a valid chain never revisits one */
			ULONG nr = up.file.direntry->anode;

			if (nr == mark)
			{
				*error = PFS_ERROR_INCONSISTENT;
				return TRUE;
			}
			if (!(++steps & (steps - 1)))
				mark = nr;
		}
		child = up;
	}

	*error = ERROR_OBJECT_IN_USE;
	return TRUE;
}

FSIZE GetDEFileSize(struct direntry *direntry, globaldata *g)
{
	if (!LARGE_FILE_SIZE || !g->largefile)
		return direntry->fsize;
#if LARGE_FILE_SIZE
	else {
		struct extrafields extrafields;
		GetExtraFields(direntry, &extrafields);
		return ((FSIZE)direntry->fsize) | ((FSIZE)extrafields.fsizex << 32);
	}
#endif
}

ULONG GetDEFileSize32(struct direntry *direntry, globaldata *g)
{
	FSIZE size;
	
	if (!LARGE_FILE_SIZE || !g->largefile) {
		return direntry->fsize;
	} else {
		size = GetDEFileSize(direntry, g);
		if (size > MAXFILESIZE32)
			size = MAXFILESIZE32;
		return (ULONG)size;
	}
}

#if LARGE_FILE_SIZE
/* The fsizex field of an entry, NULL when it has none. */
static UWORD *FindFsizex(struct direntry *direntry)
{
	UWORD *fields = (UWORD *)(((UBYTE *)direntry) + direntry->next);
	UWORD flags, i, minoffset;

	if ((direntry->next & 1) != 0 ||
		direntry->next < sizeof(struct direntry) ||
		offsetof(struct direntry, startofname) + direntry->nlength >= direntry->next)
		return NULL;
	minoffset = (sizeof(struct direntry) + direntry->nlength + *COMMENT(direntry)) & 0xfffe;
	if (direntry->next < minoffset + 2)
		return NULL;

	flags = *(--fields);
	for (i = 0; i < sizeof(struct extrafields) / 2; i++, flags >>= 1)
	{
		if (flags & 1)
		{
			if ((UBYTE *)(fields - 1) < (UBYTE *)direntry + minoffset)
				return NULL;
			fields--;
			if (i == offsetof(struct extrafields, fsizex) / 2)
				return fields;
		}
	}
	return NULL;
}

/* Add the fsizex field before a file grows above 4G; SetDEFileSize keeps the entry length */
BOOL PrepareFileSize(fileentry_t *file, FSIZE size, SIPTR *error, globaldata *g)
{
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct direntry *destentry = (struct direntry *)entrybuffer;
	struct extrafields extrafields;
	union objectinfo directory;
	struct fileinfo fi;
	FSIZE oldsize;

	if (!g->largefile || size <= MAXFILESIZE32 || FindFsizex(file->le.info.file.direntry))
		return TRUE;

	oldsize = GetDEFileSize(file->le.info.file.direntry, g);
	memcpy(destentry, file->le.info.file.direntry, file->le.info.file.direntry->next);
	GetExtraFields(destentry, &extrafields);
	extrafields.fsizex = 1;		/* a zero field is not stored */
	AddExtraFields(destentry, &extrafields);
	if (!GetParent(&file->le.info, &directory, error, g))
		return FALSE;
	if (!ChangeDirEntry(file->le.info.file, destentry, &directory, &fi, g))
	{
		*error = ERROR_DISK_FULL;
		return FALSE;
	}

	/* the field exists now: give it the real size back */
	SetDEFileSize(file->le.info.file.direntry, oldsize, g);
	MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);
	return TRUE;
}
#endif

void SetDEFileSize(struct direntry *direntry, FSIZE size, globaldata *g)
{
	direntry->fsize = (ULONG)size;
#if LARGE_FILE_SIZE
	if (g->largefile)
	{
		/* the entry keeps its length: no field means the size is below 4G */
		UWORD *fsizex = FindFsizex(direntry);
		if (fsizex)
			*fsizex = (UWORD)(size >> 32);
	}
#endif
}

FSIZE GetDDFileSize(struct deldirentry *dde, globaldata *g)
{
	if (!LARGE_FILE_SIZE || !g->largefile || dde->filename[0] >= 16)
		return dde->fsize;
#if LARGE_FILE_SIZE
	else
	{
		UWORD fsizex;
		memcpy(&fsizex, &dde->filename[16], sizeof(fsizex));
		return ((FSIZE)dde->fsize) | ((FSIZE)fsizex << 32);
	}
#endif
}

ULONG GetDDFileSize32(struct deldirentry *dde, globaldata *g)
{
	FSIZE size = GetDDFileSize(dde, g);
	if (size > MAXFILESIZE32)
		size = MAXFILESIZE32;
	return (ULONG)size;
}

void SetDDFileSize(struct deldirentry *dde, FSIZE size, globaldata *g)
{
	dde->fsize = (ULONG)size;
#if LARGE_FILE_SIZE
	if (g->largefile) {
		UWORD fsizex = (UWORD)(size >> 32);
		memcpy(&dde->filename[16], &fsizex, sizeof(fsizex));
	}
#endif
}

/* UWORD view of the extra fields; going through the union keeps the compiler
 * from treating the UWORD accesses as not aliasing the ULONG fields */
union extrawords
{
	struct extrafields fields;
	UWORD w[sizeof(struct extrafields) / 2];
};

/*
 * GetExtraFields (normal file only)
 */
void GetExtraFields(struct direntry *direntry, struct extrafields *extrafields)
{
	union extrawords *extra = (union extrawords *)extrafields;
	UWORD *fields = (UWORD *)(((UBYTE *)direntry) + direntry->next);
	UWORD flags, i, minoffset;

	if ((direntry->next & 1) != 0 ||
		direntry->next < sizeof(struct direntry) ||
		offsetof(struct direntry, startofname) + direntry->nlength >= direntry->next)
	{
		memset(extra, 0, sizeof(*extra));
		extrafields->prot |= direntry->protection;
		return;
	}
	minoffset = (sizeof(struct direntry) + direntry->nlength + *COMMENT(direntry)) & 0xfffe;
	if (direntry->next < minoffset + 2)
	{
		memset(extra, 0, sizeof(*extra));
		extrafields->prot |= direntry->protection;
		return;
	}

	flags = *(--fields);
	for (i = 0; i < sizeof(struct extrafields) / 2; i++, flags >>= 1)
	{
		if (flags & 1)
		{
			if ((UBYTE *)(fields - 1) < (UBYTE *)direntry + minoffset)
			{
				memset(extra, 0, sizeof(*extra));
				break;
			}
			extra->w[i] = *(--fields);
		}
		else
		{
			extra->w[i] = 0;
		}
	}

	/* patch protection lower 8 bits */
	extrafields->prot |= direntry->protection;
}

#if DELDIR
#if MULTIUSER
static void GetExtraFieldsDD(struct extrafields *extrafields, globaldata * g);
static void GetExtraFieldsRoot(struct extrafields *extrafields, globaldata * g);
void GetExtraFieldsOI(union objectinfo *info, struct extrafields *extrafields, globaldata * g)
{
	if (IsVolume(*info))
		GetExtraFieldsRoot(extrafields, g);
	else if (IsDelDir(*info) || IsDelFile(*info))
		GetExtraFieldsDD(extrafields, g);
	else
		GetExtraFields(info->file.direntry, extrafields);
}

static void GetExtraFieldsDD(struct extrafields *extrafields, globaldata * g)
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;

	extrafields->link = 0;
	extrafields->uid = rext->blk.dd_uid;
	extrafields->gid = rext->blk.dd_gid;
	extrafields->prot = rext->blk.dd_protection;
}

static void GetExtraFieldsRoot(struct extrafields *extrafields, globaldata * g)
{
	memset(extrafields, 0, sizeof(struct extrafields));
}
#endif
#endif

void AddExtraFields(struct direntry *direntry, struct extrafields *extra)
{
	UWORD offset, *dirext;
	UWORD array[16], i = 0, j = 0;
	UWORD flags = 0, orvalue;
	union extrawords *words = (union extrawords *)extra;

	/* patch protection lower 8 bits */
	extra->prot &= 0xffffff00;
	offset = (sizeof(struct direntry) + (direntry->nlength) + *COMMENT(direntry)) & 0xfffe;
	dirext = (UWORD *)((UBYTE *)(direntry) + (UBYTE)offset);

	orvalue = 1;
	/* fill packed field array */
	for (i = 0; i < sizeof(struct extrafields) / 2; i++)
	{
		if (words->w[i])
		{
			array[j++] = words->w[i];
			flags |= orvalue;
		}

		orvalue <<= 1;
	}

	/* add fields to direntry */
	i = j;
	while (i)
		*dirext++ = array[--i];
	*dirext++ = flags;

	direntry->next = offset + 2 * j + 2;
}


/*
 * Updates size field of links
 */
BOOL UpdateLinks(struct direntry *object, globaldata * g)
{
	struct canode linklist;
	union objectinfo loi;
	struct extrafields extrafields;
	ULONG linknr;
	SIPTR error;
	struct anodewalk walk;

	ENTER("UpdateLinks");
	GetExtraFields(object, &extrafields);
	linknr = extrafields.link;
	walk.mark = 0;
	walk.steps = 0;
	while (linknr)
	{
		if (!GetNextAnode(&linklist, linknr, &walk, g))
			return FALSE;
		if (!FetchObject(linklist.blocknr, linklist.nr, &loi, &error, g))
			return FALSE;
		loi.file.direntry->fsize = GetDEFileSize32(object, g);
		MakeBlockDirty((struct cachedblock *)loi.file.dirblock, g);
		linknr = linklist.next;
	}
	return TRUE;
}

/*
 * Removes link from linklist and kills direntry
 */
static BOOL DeleteLink(struct fileinfo *link, SIPTR *error, globaldata * g)
{
	struct canode linknode, linklist;
	struct extrafields extrafields;
	union objectinfo object, directory;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	BOOL found;
	struct anodewalk walk;

	/* get node to remove */
	if (!GetAnode(&linknode, link->direntry->anode, g))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}
	GetExtraFields(link->direntry, &extrafields);

	/* An unreadable master cannot safely be unlinked: fail before
	 * anything is changed. A missing master (dangling link) is fine.
	 */
	found = FetchObject(linknode.clustersize, extrafields.link, &object, error, g);
	if (!found && *error != ERROR_OBJECT_NOT_FOUND)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return DOSFALSE;
	}

	/* delete old entry */
	ChangeDirEntry(*link, NULL, NULL, NULL, g);

	if (!found)
		goto free_link;

	/* refetch the master: removing the entry can have moved it */
	if (!FetchObject(linknode.clustersize, extrafields.link, &object, error, g))
		goto link_failure;
	GetExtraFields(object.file.direntry, &extrafields);

	/* if the object lists our link as the first link, redirect it to the next one */
	if (extrafields.link == linknode.nr)
	{
		extrafields.link = linknode.next;
		memcpy(entrybuffer, object.file.direntry, object.file.direntry->next);
		AddExtraFields((struct direntry *)entrybuffer, &extrafields);
		if (!GetParent(&object, &directory, error, g)) {
			g->currentvolume->metadata_error = TRUE;
			*error = PFS_ERROR_INCONSISTENT;
			return DOSFALSE;	// should never happen
		}
		else {
			if (!ChangeDirEntry(object.file, (struct direntry *)entrybuffer, &directory, &object.file, g))
			{
				g->currentvolume->metadata_error = TRUE;
				*error = ERROR_DISK_FULL;
				return DOSFALSE;
			}
		}
	}
	/* otherwise simply remove the link from the list of links */
	else
	{
		if (!GetAnode(&linklist, extrafields.link, g))
			goto link_failure;
		walk.mark = extrafields.link;
		walk.steps = 0;
		while (linklist.next != linknode.nr && linklist.next)
			if (!GetNextAnode(&linklist, linklist.next, &walk, g))
				goto link_failure;

		if (linklist.next == linknode.nr)
		{
			linklist.next = linknode.next;
			if (!SaveAnode(&linklist, linklist.nr, g))
			{
				*error = PFS_ERROR_INCONSISTENT;
				return DOSFALSE;
			}
		}
		/* else: our node was not in the chain (already inconsistent) */
	}

free_link:
	if (!FreeAnode(linknode.nr, g))
		goto link_failure;
	return DOSTRUE;

link_failure:
	g->currentvolume->metadata_error = TRUE;
	*error = PFS_ERROR_INCONSISTENT;
	return DOSFALSE;
}

/*
 * Removes head of linklist and promotes first link as
 * master (NB: object is the main object, NOT a link).
 * Returns 1 if promoted, 0 if no link exists, or -1 on failure.
 */
static LONG RemapLinks(struct fileinfo *object, globaldata * g)
{
	struct extrafields extrafields;
	struct canode linknode;
	union objectinfo link, directory;
	struct direntry *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	SIPTR error;
	UWORD fsizex;

	ENTER("RemapLinks");
	/* get head of linklist */
	GetExtraFields(object->direntry, &extrafields);
	if (extrafields.link == 0)
		return FALSE;
	fsizex = extrafields.fsizex;

	/* The first link must be readable before it can be promoted. */
	{
		ULONG linknr = extrafields.link;

		for (;;)
		{
			if (!GetAnode(&linknode, linknr, g))
				return -1;
			if (FetchObject(linknode.blocknr, linknode.nr, &link, &error, g))
				break;
			return -1;
		}
	}

	/* Promote it from link to object */
	destentry = (struct direntry *)entrybuffer;
	memcpy(destentry, link.file.direntry, link.file.direntry->next);
	GetExtraFields(link.file.direntry, &extrafields);
	destentry->type = object->direntry->type;		// is this necessary?
	destentry->fsize = object->direntry->fsize;		// is this necessary?
	destentry->anode = object->direntry->anode;     // is this necessary?

	extrafields.link = linknode.next;
	extrafields.fsizex = fsizex;	/* a link has no size above 4G */
	AddExtraFields(destentry, &extrafields);

	/* Free old linklist node */
	if (!FreeAnode(linknode.nr, g))
		return -1;

	/* Remove source direntry */
	ChangeDirEntry(*object, NULL, NULL, NULL, g);

	/* Refetch new head (can have become invalid) */
	if (FetchObject(linknode.blocknr, linknode.nr, &link, &error, g) &&
		GetParent(&link, &directory, &error, g) &&
		ChangeDirEntry(link.file, destentry, &directory, &link.file, g))
	{
		/* object directory has changed; update link chain
		 * new directory is the old chain head was in: linknode.linkdir (== linknode.blocknr)
		 * A promoted directory also gets the new parent in its dirblocks.
		 */
		if (!UpdateLinkDir(link.file.direntry, linknode.blocknr, g) ||
			(destentry->type == ST_USERDIR &&
			 !SetDirParent(destentry->anode, linknode.blocknr, g)))
		{
			g->currentvolume->metadata_error = TRUE;
			return -1;
		}
	}
	else
	{
		g->currentvolume->metadata_error = TRUE;
		return -1;
	}
	return TRUE;
}

/*
 * Update linklist to reflect new directory of linked to object
 */
static BOOL UpdateLinkDir(struct direntry *object, ULONG newdiran, globaldata * g)
{
	struct canode linklist;
	struct extrafields extrafields;
	ULONG linknr;
	struct anodewalk walk;

	ENTER("UpdateLinkDir");
	GetExtraFields(object, &extrafields);
	linknr = extrafields.link;
	walk.mark = 0;
	walk.steps = 0;
	while (linknr)
	{
		/* update linklist: change clustersize (== object dir) */
		if (!GetNextAnode(&linklist, linknr, &walk, g))
			return FALSE;
		linklist.clustersize = newdiran;
		if (!SaveAnode(&linklist, linklist.nr, g))
			return FALSE;
		linknr = linklist.next;
	}
	return TRUE;
}

/* 
 * Update linklist to reflect moved node
 * (is supercopy of UpdateLinkDir)
 */
static BOOL MoveLink(struct direntry *object, ULONG newdiran, globaldata *g)
{
	struct canode linklist;
	struct extrafields extrafields;
	ULONG linknr;
	struct anodewalk walk;

	ENTER("MoveLink");
	GetExtraFields(object, &extrafields);

	/* check if is link or linked to */
	if (!(linknr = extrafields.link))
	{
		return TRUE;
	}

	/* check filetype */
	if (object->type == ST_LINKDIR || object->type == ST_LINKFILE)
	{
		/* it is a link -> just change the linkdir */
		if (!GetAnode(&linklist, object->anode, g))
			return FALSE;
		linklist.blocknr = newdiran;
		if (!SaveAnode(&linklist, linklist.nr, g))
			return FALSE;
	}
	else
	{
		/* it is the head (linked to) */
		walk.mark = 0;
		walk.steps = 0;
		while (linknr)
		{
			/* update linklist: change clustersize (== object dir) */
			if (!GetNextAnode(&linklist, linknr, &walk, g))
				return FALSE;
			linklist.clustersize = newdiran;	/* the object's directory */
			if (!SaveAnode(&linklist, linklist.nr, g))
				return FALSE;
			linknr = linklist.next;
		}
	}
	return TRUE;
}

/*
 * Set the parent of all dirblocks of a directory.
 * Returns FALSE if a dirblock or anode could not be read.
 */
static BOOL SetDirParent(ULONG dirnodenr, ULONG parentnr, globaldata * g)
{
	struct canode anode;
	ULONG anodeoffset = 0;
	struct cdirblock *blk;

	if (!GetAnode(&anode, dirnodenr, g))
		return FALSE;
	do
	{
		if (!(blk = LoadDirBlock(anode.blocknr + anodeoffset, g)))
			return FALSE;
		blk->blk.parent = parentnr;
		MakeBlockDirty((struct cachedblock *)blk, g);
	} while (NextBlock(&anode, &anodeoffset, g));

	/* NextBlock also fails if the next anode cannot be read */
	return anode.blocknr != (ULONG)~0UL;
}


#if DELDIR

/**********************************************************************/
/*                               DELDIR                               */
/**********************************************************************/

/* SearchInDeldir
 *
 * Search an object in the del-directory and return the objectinfo if found
 *
 * input : - delname: name of object to be searched for
 * output: - result: the searched for object
 * result: deldirentry * or NULL
 */
static struct deldirentry *SearchInDeldir(STRPTR delname, union objectinfo *result, SIPTR *error, globaldata * g)
{
	struct deldirentry *dde;
	struct cdeldirblock *dblk;
	UBYTE *delnumptr;
	UBYTE intl_name[PATHSIZE];	/* the part before the separator is <= 253 chars, so ctodstr fits */
	unsigned slotnr = 0, offset;
	UBYTE *digit;

	ENTER("SearchInDeldir");
	if (!(delnumptr = strrchr(delname, DELENTRY_SEP)))
		return FALSE;               /* no delentry seperator */
	digit = delnumptr + 1;
	if (!*digit)
		return NULL;
	for (; *digit; digit++)
	{
		if (*digit < '0' || *digit > '9' || slotnr > MAXDELDIR * DELENTRIES_PER_BLOCK)
			return NULL;
		slotnr = slotnr * 10 + (*digit - '0');
	}
	if (!g->currentvolume->rblkextension ||
		slotnr >= g->currentvolume->rblkextension->blk.deldirsize * DELENTRIES_PER_BLOCK)
		return NULL;

	*delnumptr = 0;             /* patch string to get filename part  */
	ctodstr(delname, intl_name);

	/* truncate to maximum length */
	if (intl_name[0] > FILENAMESIZE - 1)
		intl_name[0] = FILENAMESIZE - 1;

	intltoupper(intl_name);     /* international uppercase objectname */
	*delnumptr = DELENTRY_SEP;

	/* 4.3: get deldir block */
	if (!(dblk = GetDeldirBlock(slotnr/DELENTRIES_PER_BLOCK, g)))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return FALSE;
	}

	offset = slotnr % DELENTRIES_PER_BLOCK;

	dde = &dblk->blk.entries[offset];
	if (dde->filename[0] >= sizeof(dde->filename))
	{
		*error = PFS_ERROR_INCONSISTENT;
		return NULL;
	}
	if (intlcmp(intl_name, dde->filename))
	{
		if (!IsDelfileValid(dde, dblk, error, g))
			return NULL;

		result->delfile.special = SPECIAL_DELFILE;
		result->delfile.slotnr  = slotnr;
		LOCK(dblk);
		return dde;
	}

	return NULL;
}

/*
 * Test if delfile is valid by scanning it's blocks
 */
static BOOL IsDelfileValid(struct deldirentry *dde, struct cdeldirblock *ddblk, SIPTR *error, globaldata * g)
{
	struct canode anode;
	LONG taken;

	/* check if deldirentry actually used */
	if (!dde->anodenr)
		return FALSE;

	/* scan all blocks in the anodelist for validness */
	for (anode.nr = dde->anodenr; anode.nr; anode.nr = anode.next)
	{
		if (!GetAnode(&anode, anode.nr, g) || (taken = BlockTaken(&anode, g)) < 0)
		{
			*error = PFS_ERROR_INCONSISTENT;
			return FALSE;
		}
		if (taken)
		{
			/* free attached anodechain */
			if (!FreeAnodesInChain(dde->anodenr, g))
			{
				*error = PFS_ERROR_INCONSISTENT;
				return FALSE;
			}
			dde->anodenr = 0;
			MakeBlockDirty((struct cachedblock *)ddblk, g);
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * Check if the blocks referenced by an anode are taken
 */
static LONG BlockTaken(struct canode *anode, globaldata * g)
{
	ULONG size, bmoffset, bmseqnr, field, i, j, blocknr;
	ULONG startbit, endbit;
	UWORD bit;
	struct cbitmapblock *bitmap;

	if (!anode->clustersize)
		return FALSE;
	if (!alloc_data.longsperbmb || !alloc_data.no_bmb ||
		anode->blocknr < alloc_data.bitmapstart ||
		anode->blocknr >= g->currentvolume->numblocks ||
		anode->clustersize > g->currentvolume->numblocks - anode->blocknr)
		return -1;

	startbit = anode->blocknr - alloc_data.bitmapstart;
	endbit = startbit + anode->clustersize - 1;
	i = startbit / 32;     // longwordnr
	size = (endbit / 32) - i + 1;
	bmseqnr = i / alloc_data.longsperbmb;
	bmoffset = i % alloc_data.longsperbmb;

	while (size)
	{
		/* get first bitmapblock */
		bitmap = GetBitmapBlock(bmseqnr, g);
		if (!bitmap)
			return -1;

		/* check all blocks */
		while (bmoffset < alloc_data.longsperbmb)
		{
			/* check all bits in field */
			field = bitmap->blk.bitmap[bmoffset];
			for (bit = 0, j = 1UL << 31; bit < 32; j >>= 1, bit++)
			{
				if (!(field & j))
				{
					/* block is taken, check it out */
					blocknr = (bmseqnr * alloc_data.longsperbmb + bmoffset) * 32 + bit +
						alloc_data.bitmapstart;
					if (blocknr >= anode->blocknr && blocknr < anode->blocknr + anode->clustersize)
						return TRUE;
				}
			}
			bmoffset++;
			if (!--size)
				break;
		}

		/* get ready for next block */
		bmseqnr = (bmseqnr + 1) % (alloc_data.no_bmb);
		bmoffset = 0;
	}

	return FALSE;
}

/*
 * fill in fib. fib->fib_DiskKey must be the deldirentry number
 */
static BOOL FillDelfileFib(struct deldirentry *dde, ULONG slotnr, struct FileInfoBlock *fib, globaldata *g)
{
	struct crootblockextension *rext;
	UBYTE appbuffer[6];
	UBYTE *nameptr;
	unsigned i;
	FSIZE size;

	if ((!dde && !(dde = GetDeldirEntryQuick(slotnr, g))) ||
		dde->filename[0] >= sizeof(dde->filename))
		return FALSE;

	rext = g->currentvolume->rblkextension;
	size = GetDDFileSize(dde, g);
	fib->fib_DirEntryType = \
		fib->fib_EntryType = ST_FILE;
	fib->fib_Protection = rext->blk.dd_protection;
	fib->fib_Size = GetDDFileSize32(dde, g);
	fib->fib_NumBlocks = (size >> BLOCKSHIFT) + ((size & BLOCKSIZEMASK) != 0);
	fib->fib_Date.ds_Days = dde->creationday;
	fib->fib_Date.ds_Minute = dde->creationminute;
	fib->fib_Date.ds_Tick = dde->creationtick;
	fib->fib_Comment[0] = 0;
	fib->fib_OwnerUID = rext->blk.dd_uid;
	fib->fib_OwnerGID = rext->blk.dd_gid;

	/* get filename */
	nameptr = &fib->fib_FileName[1];
	strncpy(nameptr, &dde->filename[1], dde->filename[0]);
	nameptr += dde->filename[0];
	*nameptr++ = DELENTRY_SEP;

	/* append number appendix */
	fib->fib_DiskKey = slotnr;

	for (i = stcu_d(appbuffer, fib->fib_DiskKey); i < 3; i++)
		*nameptr++ = '0';
	strcpy(nameptr, appbuffer);
	fib->fib_FileName[0] = strlen(&fib->fib_FileName[1]);
	return TRUE;
}

/*
 * Get a >valid< deldirentry starting from deldirentrynr ddnr
 * deldir is assumed present and enabled
 */
static struct deldirentry *GetDeldirEntry(IPTR *ddnr, SIPTR *error, globaldata * g)
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;
	struct cdeldirblock *ddblk;
	struct deldirentry *dde;
	ULONG count;
	UWORD oldlock;

	if (!rext || rext->blk.deldirsize > MAXDELDIR_BLOCKS)
	{
		*error = PFS_ERROR_INCONSISTENT;
		return NULL;
	}
	count = rext->blk.deldirsize * DELENTRIES_PER_BLOCK;
	while (*ddnr < count)
	{
		/* get deldirentry */
		if (!(ddblk = GetDeldirBlock(*ddnr/DELENTRIES_PER_BLOCK, g)))
		{
			*error = PFS_ERROR_INCONSISTENT;
			break;
		}

		oldlock = ddblk->used;
		LOCK(ddblk);
		dde = &ddblk->blk.entries[*ddnr%DELENTRIES_PER_BLOCK];
		if (dde->filename[0] >= sizeof(dde->filename))
		{
			ddblk->used = oldlock;
			*error = PFS_ERROR_INCONSISTENT;
			return NULL;
		}

		/* check if dde valid */
		if (IsDelfileValid(dde, ddblk, error, g))
		{
			/* later --> check if blocks retaken !! */
			/* can be done by scanning bitmap!!     */
			ddblk->used = oldlock;
			return dde;
		}
		if (*error)
		{
			ddblk->used = oldlock;
			return NULL;
		}

		(*ddnr)++;
		ddblk->used = oldlock;
	}

	/* nothing found */
	return NULL;
}

/*
 * Get deldirentry deldirentrynr (NO CHECK ON VALIDITY
 * deldir is assumed present and enabled
 */
struct deldirentry *GetDeldirEntryQuick(ULONG ddnr, globaldata *g)
{
	struct cdeldirblock *ddblk;

	/* get deldirentry */
	if (!g->currentvolume->rblkextension ||
		ddnr >= g->currentvolume->rblkextension->blk.deldirsize * DELENTRIES_PER_BLOCK ||
		!(ddblk = GetDeldirBlock(ddnr/DELENTRIES_PER_BLOCK, g)))
		return NULL;

	return &ddblk->blk.entries[ddnr%DELENTRIES_PER_BLOCK];
}

static ULONG FillInDDEData(struct ExAllData *buffer, LONG type,
		struct deldirentry *dde, ULONG ddenr, ULONG spaceleft, globaldata *g)
{
	UWORD nameoffset, commentoffset = 0;
	UBYTE *nameptr;
	UCOUNT size;
	unsigned i;
	UBYTE appbuffer[6];

	/* get location to put name */
	switch (type)
	{
		case ED_NAME:
			size = offsetof(struct ExAllData, ed_Type);
			break;
		case ED_TYPE:
			size = offsetof(struct ExAllData, ed_Size);
			break;
		case ED_SIZE:
			size = offsetof(struct ExAllData, ed_Prot);
			break;
		case ED_PROTECTION:
			size = offsetof(struct ExAllData, ed_Days);
			break;
		case ED_DATE:
			size = offsetof(struct ExAllData, ed_Comment);
			break;
		case ED_COMMENT:
			size = offsetof(struct ExAllData, ed_OwnerUID);
			break;
		case ED_OWNER:
#if defined(__MORPHOS__)
			size = offsetof(struct ExAllData, ed_Size64);
			break;
		case ED_SIZE64:
#endif
			size = sizeof(struct ExAllData);
			break;
		default:
			size = offsetof(struct ExAllData, ed_Type);
			break;
	}

	/* size of name */
	nameoffset = size;
	size += dde->filename[0] + 4 + 1;   /* extra ddenr (3+1) and termination zero (1) */

	/* size of comment */
	if (type >= ED_COMMENT)
	{
		commentoffset = size;
		size++;
	}

	/* check fit */
	size = (size + 1) & 0xfffe;
	if (size > spaceleft)
		return 0;

	/* copy */
	buffer->ed_Next = NULL;
	switch (type)
	{
#if defined(__MORPHOS__)
		case ED_SIZE64:
			buffer->ed_Size64 = GetDDFileSize(dde, g);
#endif

		case ED_OWNER:
			buffer->ed_OwnerUID = g->currentvolume->rblkextension->blk.dd_uid;
			buffer->ed_OwnerGID = g->currentvolume->rblkextension->blk.dd_gid;

		case ED_COMMENT:
			buffer->ed_Comment = (UBYTE *)buffer + commentoffset;
			*((UBYTE *)buffer + commentoffset) = 0x0;

		case ED_DATE:
			buffer->ed_Days = dde->creationday;
			buffer->ed_Mins = dde->creationminute;
			buffer->ed_Ticks = dde->creationtick;

		case ED_PROTECTION:
			buffer->ed_Prot = g->currentvolume->rblkextension->blk.dd_protection;

		case ED_SIZE:
			buffer->ed_Size = GetDDFileSize32(dde, g);

		case ED_TYPE:
			buffer->ed_Type = ST_FILE;

		case ED_NAME:
			/* filename */
			nameptr = (UBYTE *)buffer + nameoffset;
			strncpy(nameptr, &dde->filename[1], dde->filename[0]);
			nameptr += dde->filename[0];
			*nameptr++ = DELENTRY_SEP;

			/* append nr */
			for (i = stcu_d(appbuffer, ddenr); i < 3; i++)
				*nameptr++ = '0';
			strcpy(nameptr, appbuffer);

		default:
			buffer->ed_Name = (UBYTE *)buffer + nameoffset;
	}

	return size;
}


static struct cdeldirblock *GetDeldirBlock(ULONG seqnr, globaldata *g)
{
	struct volumedata *volume = g->currentvolume;
	struct crootblockextension *rext;
	struct cdeldirblock *ddblk;
	ULONG blocknr;

	rext = volume->rblkextension;

	if (!rext || seqnr > MAXDELDIR || seqnr >= rext->blk.deldirsize)
	{
		DB(Trace(5,"GetDeldirBlock","seqnr out of range = %lx\n", seqnr));
		ErrorMsg (AFS_ERROR_DELDIR_INVALID, NULL, g);
		return NULL;
	}

	/* get blocknr */
	if (!(blocknr = rext->blk.deldir[seqnr]))
	{
		DB(Trace(5,"GetDeldirBlock","ERR: index zero\n"));
		ErrorMsg (AFS_ERROR_DELDIR_INVALID, NULL, g);
		return NULL;
	}

	/* check cache */
	for (ddblk = HeadOf(&volume->deldirblks); ddblk->next; ddblk=ddblk->next)    
	{
		if (ddblk->blk.seqnr == seqnr)
		{
			MakeLRU (ddblk);
			return ddblk;
		}
	}

	/* alloc cache */
	if (!(ddblk = (struct cdeldirblock *)AllocLRU(g)))
	{
		DB(Trace(5,"GetDeldirBlock","ERR: alloclru failed\n"));
		return NULL;
	}

	/* read block */
	if (RawRead ((UBYTE*)&ddblk->blk, RESCLUSTER, blocknr, g) != 0)
	{
		DB(Trace(5,"GetDeldirBlock","Read ERR: seqnr = %d blocknr = %lx\n", seqnr, blocknr));
		FreeLRU ((struct cachedblock *)ddblk);
		return NULL;
	}

	/* check it */
	if (ddblk->blk.id != DELDIRID || ddblk->blk.seqnr != seqnr)
	{
		ErrorMsg (AFS_ERROR_DELDIR_INVALID, NULL, g);
		FreeLRU ((struct cachedblock *)ddblk);
		return NULL;
	}
	
	/* initialize it */
	ddblk->volume     = volume;
	ddblk->blocknr    = blocknr;
	ddblk->used       = FALSE;
	ddblk->changeflag = FALSE;

	/* add to cache and return */	
	MinAddHead (&volume->deldirblks, ddblk);
	return ddblk;
}

struct cdeldirblock *NewDeldirBlock(UWORD seqnr, globaldata *g)
{
	struct volumedata *volume = g->currentvolume;
	struct crootblockextension *rext;
	struct cdeldirblock *ddblk;
	ULONG blocknr;

	rext = volume->rblkextension;

	if (seqnr > MAXDELDIR)
	{
		DB(Trace(5, "NewDelDirBlock", "seqnr out of range = %lx\n", seqnr));
		return NULL;
	}

	/* alloc block and LRU slot */
	if (!(ddblk = (struct cdeldirblock *)AllocLRU(g)) ||
		!(blocknr = AllocReservedBlock(g)) )
	{
		if (ddblk)
			FreeLRU((struct cachedblock *)ddblk);
		return NULL;
	}

	/* make reference */
	rext->blk.deldir[seqnr] = blocknr;

	/* fill block */
	ddblk->volume     = volume;
	ddblk->blocknr    = blocknr;
	ddblk->used       = FALSE;
	ddblk->blk.id     = DELDIRID;
	ddblk->blk.seqnr  = seqnr;
	ddblk->changeflag = TRUE;
	ddblk->blk.protection		= DELENTRY_PROT;	/* re..re..re.. */
	ddblk->blk.creationday		= volume->rootblk->creationday;
	ddblk->blk.creationminute	= volume->rootblk->creationminute;
	ddblk->blk.creationtick		= volume->rootblk->creationtick;

	/* add to cache and return */
	MinAddHead(&volume->deldirblks, ddblk);
	return ddblk;
}

/* Allocate deldirslot. Free anodechain attached to slot and clear it.
 * An intermediate update is possible, due to FreeAnodesInChain()
 */
static int AllocDeldirSlot(globaldata * g)
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;
	struct cdeldirblock *ddblk;
	struct deldirentry *dde;
	int ddnr = 0;
	ULONG anodenr;

	/* get deldirentry and update roving ptr */
	if (!rext)
		return -1;
	ddnr = rext->blk.deldirroving;
	if (!rext->blk.deldirsize || ddnr >= rext->blk.deldirsize * DELENTRIES_PER_BLOCK ||
		!(ddblk = GetDeldirBlock(ddnr/DELENTRIES_PER_BLOCK,g)))
	{
		return -1;
	}

	dde = &ddblk->blk.entries[ddnr%DELENTRIES_PER_BLOCK];
	rext->blk.deldirroving = (rext->blk.deldirroving + 1) %
		(rext->blk.deldirsize*DELENTRIES_PER_BLOCK);
	MakeBlockDirty((struct cachedblock *)ddblk, g);

	anodenr = dde->anodenr;
	if (anodenr)
	{
		/* clear it for reuse */
		dde->anodenr = 0;

		/* free attached anodechain */
		if (!FreeAnodesInChain(anodenr, g))
			return -1;
	}

	DB(Trace(1, "AllocDelDirSlot", "Allocate slot %ld\n", ddnr));
	return ddnr;
}


/* Add a file to the deldir. 
 * Deldir assumed enabled here, and info assumed a file (ST_FILE)
 * ddnr is deldir slot to use. Slot is assumed to be allocated by
 * AllocDeldirSlot()
 */
static BOOL AddToDeldir(union objectinfo *info, int ddnr, globaldata * g)
{
	struct cdeldirblock *ddblk;
	struct deldirentry *dde;
	struct direntry *de = info->file.direntry;
	struct crootblockextension *rext;
	struct DateStamp time;

	DB(Trace(1, "AddToDeldir", "slotnr %ld\n", ddnr));
	/* get deldirentry to put it in */
	if (ddnr < 0 || !(ddblk = GetDeldirBlock(ddnr/DELENTRIES_PER_BLOCK, g)))
		return FALSE;
	dde = &ddblk->blk.entries[ddnr%DELENTRIES_PER_BLOCK];

	/* put new one in */
	dde->anodenr = de->anode;
	SetDDFileSize(dde, GetDEFileSize(de, g), g);
	dde->creationday = de->creationday;
	dde->creationminute = de->creationminute;
	dde->creationtick = de->creationtick;
	dde->filename[0] = min(DELENTRYFNSIZE - 1, de->nlength);
	strncpy(&dde->filename[1], &de->startofname, dde->filename[0]);

	/* Touch deldir block. Inserted here, simply because this the only
	 * place touching the deldir will be needed.
	 * Note: Only this copy is touched ...
	 */
	DateStamp(&time);
	rext = g->currentvolume->rblkextension;
	ddblk->blk.creationday = rext->blk.dd_creationday = (UWORD)time.ds_Days;
	ddblk->blk.creationminute = rext->blk.dd_creationminute = (UWORD)time.ds_Minute;
	ddblk->blk.creationtick = rext->blk.dd_creationtick = (UWORD)time.ds_Tick;

	/* dirtify block */
	MakeBlockDirty((struct cachedblock *)ddblk, g);
	return TRUE;
}

/* Set number of deldir blocks (Has to be single threaded)
 * If 0 then deldir is disabled (but MODE_DELDIR stays;
 * InitModules() detect that the number of deldirblocks is 0)
 * There must be a currentvolume
 * Returns error (0 = success)
 */
ULONG SetDeldir(int nbr, globaldata *g) 
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;
	struct cdeldirblock *ddblk, *next;
	listentry_t *list;
	int i, j;
	ULONG error = 0;

	/* check range */
	if (nbr < 0 || nbr > MAXDELDIR_BLOCKS)
		return ERROR_BAD_NUMBER;

	/* check if there are locks on any deldir, delfile */
	for (list = HeadOf(&g->currentvolume->fileentries); list->next; list=list->next)
	{
		if (IsDelDir(list->info) || IsDelFile(list->info))
			return ERROR_OBJECT_IN_USE;
	}

	if (!UpdateDisk(g))
		return PFS_ERROR_INCONSISTENT;

	for (i = nbr; i < rext->blk.deldirsize; i++)
	{
		if ((ddblk = GetDeldirBlock(i, g)))
		{
			for (j = 0; j < DELENTRIES_PER_BLOCK; j++)
			{
				if (ddblk->blk.entries[j].anodenr)
				{
					FreeAnodesInChain(ddblk->blk.entries[j].anodenr, g);
					ddblk->blk.entries[j].anodenr = 0;
				}
			}
		}
	}

	/* flush cache */
	for (ddblk = HeadOf(&g->currentvolume->deldirblks); (next=ddblk->next); ddblk=next)
	{
		FlushBlock((struct cachedblock *)ddblk, g);
		MinRemove(LRU_CHAIN(ddblk));
		MinAddHead(&g->glob_lrudata.LRUpool, LRU_CHAIN(ddblk));
		// i.p.v. FreeLRU((struct cachedblock *)ddblk, g);
	}

	/* free unwanted deldir blocks. ResToBeFreed, not FreeReservedBlock:
	 * the committed rootblockextension still references these blocks
	 * until the next update, so they must not become allocatable before
	 * that update's rootblock write (state overlap)
	 */
	for (i = nbr; i < rext->blk.deldirsize; i++)
	{
		ResToBeFreed(rext->blk.deldir[i], g);
		rext->blk.deldir[i] = 0;
	}

	/* allocate wanted ones */
	for (i = rext->blk.deldirsize; i < nbr; i++)
	{
		if (!NewDeldirBlock(i,g))
		{
			nbr = i;
			error = ERROR_DISK_FULL;
			break;
		}
	}

	/* if deldir size increases, start roving in a the new area 
	 * if deldir size decreases, start roving from the start
	 */
	if (nbr > rext->blk.deldirsize)
		rext->blk.deldirroving = rext->blk.deldirsize * DELENTRIES_PER_BLOCK;
	else
		rext->blk.deldirroving = 0;

	/* enable/disable */
	rext->blk.deldirsize = nbr;
	g->deldirenabled = (nbr > 0);

	MakeBlockDirty((struct cachedblock *)rext, g);
	UpdateDisk(g);
	return error;
}
#endif
