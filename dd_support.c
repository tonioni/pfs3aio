/* $Id: dd_support.c 1.9 1999/02/22 16:33:43 Michiel Exp Michiel $ */
/* $Log: dd_support.c $
 * Revision 1.9  1999/02/22  16:33:43  Michiel
 * Changes for increasing deldir capacity
 *
 * Revision 1.8  1998/09/27  11:26:37  Michiel
 * ErrorMsg param
 *
 * Revision 1.7  1997/03/03  22:04:04  Michiel
 * Release 16.21
 *
 * Revision 1.6  1996/03/29  17:02:54  Michiel
 * update sleeproutines
 *
 * Revision 1.5  1996/01/30  12:49:23  Michiel
 * --- working tree overlap ---
 * new notify routines
 * */

#include "kswrapper.h"

/**********************
 * function prototypes
 */

static ULONG RenameDisk(UBYTE *, globaldata *);

static void NotifyUser(struct NotifyRequest *nr, globaldata *g);
static struct notifyobject *CheckNotify(ULONG parentanodenr, DSTR objectname,
	struct notifyobject *no, BOOL checkparent, globaldata *);
void PFSDoNotify(struct fileinfo *object, BOOL checkparent, globaldata *g);
void PFSUpdateNotify(ULONG dirnode, DSTR filename, ULONG anodenr, globaldata *g);

/* AFS extensions */
#if EXTRAPACKETS
static BOOL Sleep (SIPTR *error, globaldata *g);
static BOOL Awake (SIPTR *error, globaldata *g);
static BOOL Alarm (SIPTR *error, globaldata *g);
static LONG UpdateAnode (ULONG old, ULONG new, globaldata *g);
void HandleSleepMsg (globaldata *g);
#endif

/**********************
 * macros
 */

#define SkipColon(filename,pathname)                                \
do {                                                                   \
	if ((filename = strchr ((pathname), ':')))                       \
		filename++;                                                 \
	else                                                            \
		filename=(pathname);                                        \
} while (0)

#define LocateFile(parentfi,filename,objectfi,error)                \
do {                                                                   \
	if (!FindObject (parentfi, filename, &(objectfi), error, g))    \
		return DOSFALSE;                                            \
																	\
	if (IsVolume(objectfi))                                         \
	{                                                               \
		*error = ERROR_OBJECT_WRONG_TYPE;                           \
		return DOSFALSE;                                            \
	}                                                               \
} while (0)

#if MULTIUSER
#define muGetRelationship(extrafields)                              \
	((g->muFS_ready) ?                                              \
	(muGetRelationshipA (g->user, ((extrafields).uid << 16) + (extrafields).gid, NULL)) :   \
	((((extrafields).uid == muNOBODY_UID) << muRelB_NO_OWNER) | muRelF_NOBODY))

#if DELDIR
#define CheckPropertyAccess(objectfi,extrafields,flags, error)      \
do {                                                                   \
	GetExtraFieldsOI (&(objectfi), &(extrafields), g);                  \
	if (g->muFS_ready)                                              \
		flags = muGetRelationshipA (g->user, ((extrafields).uid << 16) + (extrafields).gid, NULL);  \
	else                                                                                        \
		flags = (((extrafields).uid == muNOBODY_UID) << muRelB_NO_OWNER) | muRelF_NOBODY;         \
																	\
	if (!(flags & muRel_PROPERTY_ACCESS))                           \
	{                                                               \
		*error = ERROR_WRITE_PROTECTED;                             \
		return DOSFALSE;                                            \
	}                                                               \
} while (0)
#else /* DELDIR */
#define CheckPropertyAccess(objectfi,extrafields,flags, error)      \
do {                                                                   \
	GetExtraFields ((objectfi).file.direntry, &(extrafields));          \
	if (g->muFS_ready)                                              \
		flags = muGetRelationshipA (g->user, ((extrafields).uid << 16) + (extrafields).gid, NULL);  \
	else                                                                                        \
		flags = (((extrafields).uid == muNOBODY_UID) << muRelB_NO_OWNER) | muRelF_NOBODY;         \
																	\
	if (!(flags & muRel_PROPERTY_ACCESS))                           \
	{                                                               \
		*error = ERROR_WRITE_PROTECTED;                             \
		return DOSFALSE;                                            \
	}                                                               \
} while (0)
#endif /* DELDIR */

#else /* MULTIUSER */
#define muGetRelationship(extrafields)
#define CheckPropertyAccess(objectfi,extrafields,flags,error)   
#endif /* MULTIUSER */

/*
 * BPTR, BOOL, listentry_t *, union objectinfo *
 */
#define GetFileInfoFromLock(argument, access, fe, fi)               \
do {                                                                   \
	if ((fe = LockEntryFromLock (argument)))                        \
	{                                                               \
		if (!CheckVolume ((fe)->le.volume, access, error, g))         \
			return DOSFALSE;                                        \
		UpdateLE ((listentry_t *)(fe), g);                          \
		if ((fe)->le.dirblocknr)                                    \
		{                                                           \
			*error = PFS_ERROR_INCONSISTENT;                        \
			return DOSFALSE;                                        \
		}                                                           \
		fi = &(fe)->le.info;                                        \
	}                                                               \
	else                                                            \
	{                                                               \
		if (!g->currentvolume)                                      \
		{                                                           \
			*error = ERROR_NO_DISK;                                 \
			return DOSFALSE;                                        \
		}                                                           \
		fi = NULL;                                                  \
	}                                                               \
} while (0)



/**********************
 * rename disk
 */

static ULONG RenameDisk (UBYTE *newname, globaldata *g)  //%4.5
{
	UBYTE *diskname;
	struct volumedata *volume = g->currentvolume;
	UBYTE oldname[DNSIZE];

	ENTER("RenameDisk");

	/* Never write a root containing uncommitted or lost metadata. */
	if (volume->metadata_error)
		return PFS_ERROR_INCONSISTENT;
	if (g->dirty && (!UpdateDisk(g) || g->dirty || volume->metadata_error))
		return g->softprotect ? ERROR_DISK_WRITE_PROTECTED : PFS_ERROR_INCONSISTENT;

	/* Write the renamed, already committed root directly. */
	diskname = volume->rootblk->diskname;
	CopyMem(diskname, oldname, DNSIZE);
	*diskname = strlen(newname);
	CopyMem(newname, diskname+1, *diskname);
	ULONG err = RawWrite((UBYTE *)volume->rootblk, 1, ROOTBLOCK, g);   /* %7.2 */
	if (!err) {
		UpdateAndMotorOff(g);
		volume->rootblockchangeflag = FALSE;
	} else {
		CopyMem(oldname, diskname, DNSIZE);
	}
	return err;
}


/**********************
 * notify stuff
 */

static void NotifyUser (struct NotifyRequest *nr, globaldata *g)
{
  struct NotifyMessage *msg;

	if (nr->nr_MsgCount && (nr->nr_Flags & NRF_WAIT_REPLY))
	{
		nr->nr_Flags |= NRF_MAGIC;
		return;
	}

	if (nr->nr_Flags & NRF_SEND_MESSAGE)
	{
		if (!(msg = AllocMemP (sizeof(struct NotifyMessage), g)))
			return;

		msg->nm_ExecMessage.mn_ReplyPort = g->notifyport;
		msg->nm_ExecMessage.mn_Length = sizeof(struct NotifyMessage); 
		msg->nm_Class = NOTIFY_CLASS;
		msg->nm_Code = NOTIFY_CODE;
		msg->nm_NReq = nr;
		PutMsg (nr->nr_stuff.nr_Msg.nr_Port, &msg->nm_ExecMessage);
		nr->nr_MsgCount++;
	}
	else if (nr->nr_Flags & NRF_SEND_SIGNAL)
	{
		Signal (nr->nr_stuff.nr_Signal.nr_Task, 1<<nr->nr_stuff.nr_Signal.nr_SignalNum);
	}
}

/*
 * Checks if there is a notify for the object identified by anodenr
 */
static struct notifyobject *
CheckNotify (ULONG parentanodenr, DSTR objectname, struct notifyobject *no,
			 BOOL checkparent, globaldata *g)
{
	if (!no)
		no = HeadOf(&g->currentvolume->notifylist);
	else
		no = no->next;

	for ( ; no->next; no=no->next)
	{
		/* file match */
		if (no->parentanodenr == parentanodenr)
		{
			if (intlcmp(no->objectname, objectname))
				return no;
		}

		/* parent match */
		if (checkparent && no->anodenr == parentanodenr)
			return no;
	}

	return NULL;
}

/*
 * Notifies change of object & dir object is in
 */
void PFSDoNotify (struct fileinfo *object, BOOL checkparent, globaldata *g)
{
	struct notifyobject *no = NULL;

	if (!object || !object->direntry || !object->dirblock || !g->currentvolume)
		return;

#if DELDIR
	if ((IPTR)object->direntry <= SPECIAL_DELFILE)
		return;
#endif

	while ((no = CheckNotify (object->dirblock->blk.anodenr,
				(DSTR)&object->direntry->nlength, no, checkparent, g)))
		NotifyUser(no->req, g);
}


/*
 * To be called when a object named filename is created in dir
 * dirnode. This routine checks if there is a partially parsed
 * notify request in that directory that matches the newly
 * created object. If so, the parsing of the notify request
 * can continue.
 */
void PFSUpdateNotify (ULONG dirnode, DSTR filename, ULONG anodenr, globaldata *g)
{
  struct notifyobject *no = NULL;
  UBYTE *temp;

	for (no = HeadOf(&g->currentvolume->notifylist); no->next; no=no->next)
	{
		/* check for parsed path match */
		if (no->parentanodenr == dirnode)
		{
			if (no->unparsed)
			{
				/* check if we can continue parsing */
				temp = strchr(no->unparsed, '/');
				if (temp)
					*temp = 0;
		
				if (intlcdcmp(no->unparsed, filename))
				{
					if (temp)
						no->unparsed = *(temp+1) ? (temp+1) : NULL;
					else
						no->unparsed = NULL;

					no->parentanodenr = anodenr;
				}

				if (temp)
					*temp = '/';
			}
			else
			{
				/* check if new object is a notify object */
				if (intlcmp(no->objectname, filename))
				{
					no->anodenr = anodenr;
				}
			}
		}
	}
}


/**********************
 * AFS extension
 */

#if EXTRAPACKETS
/*
 * Enter MODE_SLEEP
 *
 * All direct directory block references are replaced by an
 * anode reference. The reference can later be restored by
 * searching the object in the directory identified by the
 * diranodenr.
 *
 * Examine chains have to be broken since sleepwalkers are
 * allowed to change directory order
 */
static BOOL Sleep (SIPTR *error, globaldata *g)
{
	listentry_t *le;
	struct volumedata *volume = g->currentvolume;

	if (volume)
	{
		if (volume->metadata_error ||
			(g->dirty && (!UpdateDisk(g) || g->dirty)) ||
			UpdateDataCache(g))
		{
			*error = PFS_ERROR_INCONSISTENT;
			return FALSE;
		}

		/* Resolve every parent before releasing any live references. */
		for (le=HeadOf (&volume->fileentries); le->next; le=le->next)
		{
			le->diranodenr = 0;
			le->sleepref = FALSE;
			if (!IsVolumeEntry(le)
#if DELDIR
				&& !IsDelDir(le->info) && !IsDelFile(le->info)
#endif
				)
			{
				if ((le->dirblocknr && !LoadDirBlock(le->dirblocknr, g)) ||
					!le->info.file.dirblock)
				{
					*error = PFS_ERROR_INCONSISTENT;
					return FALSE;
				}
				le->diranodenr = le->info.file.dirblock->blk.anodenr;
				le->sleepref = TRUE;
			}
		}

		for (le=HeadOf (&volume->fileentries); le->next; le=le->next)
		{
			if (IsFileEntry(le))
			{
				fileentry_t *fe = (fileentry_t *)le;

				if (fe->anodechain)
					DetachAnodeChain (fe->anodechain, g);
				fe->anodechain = NULL;
				fe->currnode = NULL;
			}
			if (le->sleepref)
			{
#if DELDIR
				le->info.deldir.special = SPECIAL_FLUSHED;
#else
				le->info.file.direntry = NULL;
#endif
				le->info.file.dirblock = NULL;
			}
			le->dirblocknr = 0;
			le->dirblockoffset = 0;

			/* terminate examine chains */
			if (le->type.flags.dir
#if DELDIR
				&& !IsDelDir(le->info)
#endif
				)
			{
				((lockentry_t *)le)->nextentry.direntry = NULL;
				((lockentry_t *)le)->nextentry.dirblock = NULL;
				((lockentry_t *)le)->nextdirblocknr = 0;
				((lockentry_t *)le)->nextdirblockoffset = 0;
			}
		}


		/* flush cache. Watch it: FreeUnusedResources also calls
		 * the conventional FlushBlock! 
		 */
		FreeUnusedResources (volume, g);
		FlushDataCache(g);
	}

	/* set sleepmode */
	g->sleepmode = TRUE;
	g->sleep_wake_error = 0;

	/* enter sleepmode */
	g->DoCommand = SleepCommands;
	return TRUE;
}

/*
 * Leave MODE_SLEEP
 *
 * Reload all essential blocks. References are restored by searching
 * by anodenr in the directory identified by diranodenr (using FetchObject)
 */
static BOOL Awake (SIPTR *error, globaldata *g)
{
  listentry_t *le;
  struct volumedata *volume = g->currentvolume;
  struct rootblock *rootblock, *newroot;
  UBYTE *newext = NULL;
  ULONG rootsize;

	if (volume)
	{
		if (volume->metadata_error || g->dirty || alloc_data.rtbf_index)
			goto failed;

		/* Read into separate buffers: a short or failed read must not
		 * overwrite the mounted volume's only copy of its metadata. */
		rootblock = volume->rootblk;
		rootsize = (ULONG)rootblock->rblkcluster * BLOCKSIZE;
		newroot = AllocBufmem(rootsize, g);
		if (!newroot)
		{
			*error = ERROR_NO_FREE_STORE;
			return FALSE;
		}
		if (RawRead((UBYTE *)newroot, rootblock->rblkcluster, ROOTBLOCK, g) ||
			newroot->disktype != rootblock->disktype ||
			newroot->creationday != rootblock->creationday ||
			newroot->creationminute != rootblock->creationminute ||
			newroot->creationtick != rootblock->creationtick ||
			newroot->rblkcluster != rootblock->rblkcluster ||
			newroot->reserved_blksize != rootblock->reserved_blksize ||
			newroot->firstreserved != rootblock->firstreserved ||
			newroot->lastreserved != rootblock->lastreserved ||
			((newroot->options ^ rootblock->options) &
			 (MODE_HARDDISK | MODE_SPLITTED_ANODES | MODE_SUPERINDEX |
			  MODE_DIR_EXTENSION | MODE_LARGEFILE)) ||
			!!(newroot->extension && (newroot->options & MODE_EXTENSION)) !=
			!!volume->rblkextension)
			goto read_failed;

		if (volume->rblkextension)
		{
			newext = AllocBufmem(rootblock->reserved_blksize, g);
			if (!newext)
			{
				*error = ERROR_NO_FREE_STORE;
				FreeBufmem(newroot, g);
				return FALSE;
			}
			if (RawRead(newext, volume->rescluster, newroot->extension, g) ||
				((struct rootblockextension *)newext)->id != EXTENSIONID)
				goto read_failed;
		}

		memcpy(rootblock, newroot, rootsize);
		if (newext)
		{
			memcpy(&volume->rblkextension->blk, newext, rootblock->reserved_blksize);
			volume->rblkextension->blocknr = rootblock->extension;
			FreeBufmem(newext, g);
		}
		FreeBufmem(newroot, g);

		/* InitAllocation replaces this empty queue. */
		if (alloc_data.reservedtobefreed)
		{
			FreeMem(alloc_data.reservedtobefreed,
				sizeof(*alloc_data.reservedtobefreed) * alloc_data.rtbf_size);
			alloc_data.reservedtobefreed = NULL;
		}
		InitModules (volume, FALSE, g);
		if (volume->metadata_error || (g->dirty && (!UpdateDisk(g) || g->dirty)))
		{
			/* Do not reload or discard a partially applied postponed operation. */
			volume->metadata_error = TRUE;
			goto failed;
		}
		if (!andata.anblkbitmap)
		{
			*error = PFS_ERROR_INCONSISTENT;
			goto rollback;
		}

		/* restore references */
		for (le=HeadOf (&volume->fileentries); le->next; le=le->next)
		{
			if (le->sleepref)
			{
				if (!le->diranodenr || !le->anodenr)
				{
					*error = ERROR_OBJECT_NOT_FOUND;
					goto rollback;
				}
				if (!FetchObject(le->diranodenr, le->anodenr, &le->info, error, g))
					goto rollback;
				if (!!le->type.flags.dir != !!IsDir(le->info) || IsSoftLink(le->info))
				{
					*error = ERROR_OBJECT_WRONG_TYPE;
					goto rollback;
				}
			}
#if DELDIR
			if (IsDelDir(le->info) && !g->deldirenabled)
			{
				*error = ERROR_OBJECT_NOT_FOUND;
				goto rollback;
			}
			if (IsDelFile(le->info))
			{
				struct deldirentry *dde = GetDeldirEntryQuick(le->info.delfile.slotnr, g);
				if (!dde || dde->anodenr != le->anodenr)
				{
					*error = PFS_ERROR_INCONSISTENT;
					goto rollback;
				}
			}
#endif
			if (IsFileEntry(le))
			{
				FSIZE offset;
				FSIZE filesize;
				fileentry_t *fe = (fileentry_t *)le;
				struct anodechainnode *node;
				ULONG anodeoffset;

				if (!le->anodenr)
				{
					*error = ERROR_OBJECT_NOT_FOUND;
					goto rollback;
				}
				if (!(fe->anodechain = GetAnodeChain(le->anodenr, g)))
				{
					*error = ERROR_NO_FREE_STORE;
					goto rollback;
				}
				offset = fe->offset;
#if DELDIR
				if (IsDelFile(le->info))
				{
					struct deldirentry *dde = GetDeldirEntryQuick(le->info.delfile.slotnr, g);
					if (!dde)
					{
						*error = PFS_ERROR_INCONSISTENT;
						goto rollback;
					}
					filesize = GetDDFileSize(dde, g);
				}
				else
#endif
					filesize = GetDEFileSize(le->info.file.direntry, g);
#if ROLLOVER
				if (IsRollover(le->info))
				{
					struct extrafields fields;
					GetExtraFields(le->info.file.direntry, &fields);
					if (filesize <= 0 || filesize > (FSIZE)0xffffffffUL ||
						(FSIZE)fields.rollpointer > filesize ||
						(FSIZE)fields.virtualsize >= filesize)
					{
						*error = PFS_ERROR_INCONSISTENT;
						goto rollback;
					}
				}
#endif
				fe->currnode = &fe->anodechain->head;
				fe->offset = fe->blockoffset = fe->anodeoffset = 0;
				/* The stored offset is physical even for rollover files. */
				if (SeekInFile(fe, offset, OFFSET_BEGINNING, error, g) == -1)
				{
					fe->offset = offset;
					goto rollback;
				}
				node = fe->currnode;
				anodeoffset = fe->anodeoffset;
				if ((offset < filesize || fe->blockoffset) &&
					!CorrectAnodeAC(&node, &anodeoffset, g))
				{
					*error = PFS_ERROR_INCONSISTENT;
					goto rollback;
				}
			}
		}
	}

	// --> prevent 'normal' reference-restore on loading
	//  directory blocks

	/* unset sleepmode */
	g->sleepmode = FALSE;

	/* awake */
	g->DoCommand = NormalCommands;
	return TRUE;

read_failed:
	if (newext)
		FreeBufmem(newext, g);
	FreeBufmem(newroot, g);
	goto failed;

rollback:
	/* All chains acquired by this attempt are owned by their entries.
	 * Drop them and clear references before invalidating clean cache blocks;
	 * a later wake must read the sleepwalker's latest disk contents. */
	for (le=HeadOf(&volume->fileentries); le->next; le=le->next)
	{
		if (IsFileEntry(le))
		{
			fileentry_t *fe = (fileentry_t *)le;
			if (fe->anodechain)
				DetachAnodeChain(fe->anodechain, g);
			fe->anodechain = NULL;
			fe->currnode = NULL;
		}
		if (le->sleepref)
		{
#if DELDIR
			le->info.deldir.special = SPECIAL_FLUSHED;
#else
			le->info.file.direntry = NULL;
#endif
			le->info.file.dirblock = NULL;
		}
		le->dirblocknr = le->dirblockoffset = 0;
	}
	FreeUnusedResources(volume, g);
	FlushDataCache(g);
	return FALSE;

failed:
	*error = PFS_ERROR_INCONSISTENT;
	return FALSE;
}

/*
 * Disk accessing packets are pending --> wake up!
 * Handshaking sleeptask <-> AFS
 */
static BOOL Alarm (SIPTR *error, globaldata *g)
{
  ULONG sleepmask = 1UL << g->alarmsignal;

	/* signal task that we want to wake up */
	g->sleep_wake_error = 0;
	Signal (g->sleeptask, sleepmask);

	while (g->sleepmode)
	{
		WaitPort (g->sleepport);
		HandleSleepMsg (g);
		if (g->sleepmode && g->sleep_wake_error)
		{
			*error = g->sleep_wake_error;
			return FALSE;
		}
	}
	return TRUE;
}



/*
 * Reflect changed anode in locklist references
 * returns number of references updated
 * (MODE_SLEEP only)
 */
static LONG UpdateAnode (ULONG old, ULONG new, globaldata *g)
{
  listentry_t *le;
  struct notifyobject *no;
  LONG i=0;
  struct volumedata *volume = g->currentvolume;

	if (!volume)
		return 0;

	/*
	 * Update fileentry list
	 */

	for (le=(listentry_t *)HeadOf(&volume->fileentries); le->next; le=le->next)
	{
		if (!IsVolumeEntry(le))
		{
			if (le->anodenr == old)
			{
				i++;
				if (!new)
					;   /* invalidate le */
				le->anodenr = new;
			}

			if (le->diranodenr == old)
			{
				i++;
				le->diranodenr = new;
			}
		}
	}

	/*
	 * Update notify list
	 */

	for (no=HeadOf(&volume->notifylist); no->next; no=no->next)
	{
		if (no->anodenr == old)
		{
			i++;
			no->anodenr = new;
		}
		if (no->parentanodenr == old)
		{
			i++;
			no->parentanodenr = new;
		}
	}

	return i;
}

/*
 * Message handler for messages on the sleepport
 */
void HandleSleepMsg (globaldata *g)
{
  struct Message *msg;
  struct DosPacket *saved_action = g->action;

	while ((msg = GetMsg (g->sleepport)))
	{
		g->action = (struct DosPacket *)msg->mn_Node.ln_Name;
		if (g->sleepmode)
		{
			(g->DoCommand)(g->action, g);
		}
		else
		{
			g->action->dp_Res1 = DOSFALSE;
			g->action->dp_Res2 = ERROR_ACTION_NOT_KNOWN;
		}

		ReturnPacket (g->action, g->sleepport, g);
	}
	g->action = saved_action;
}

#endif
