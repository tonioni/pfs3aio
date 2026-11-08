	
#include <exec/exec_lib.i>

	.globl _rawdofmt
	.globl _entrypoint
	.globl _PFS3ResidentEnd
	.globl _ResidentAddToFSResource
	.globl _shortname
	.globl _version

	bra.s _entrypoint
	dc.w 0			/* pad: the fake segment needs longword alignment */

	/* Fake segment published in FileSystem.resource (fse_SegList =
	 * MKBADDR(_PFS3FakeSeg)). It must precede _PFS3Resident: C:Version
	 * identifies a handler by scanning its seglist forward for the
	 * romtag. CreateProc enters a seglist at BADDR+4, hence the bra
	 * to the real entrypoint. */
	.globl _PFS3FakeSeg
_PFS3FakeSeg:
	dc.l 0			/* next segment: end of list */
	bra.w _entrypoint	/* seglist entry point (BADDR+4) */

_PFS3Resident:
	dc.w 0x4afc
	dc.l _PFS3Resident
	dc.l _PFS3ResidentEnd
	dc.b 1,20,0,78
	dc.l _shortname
	dc.l _version+6
	dc.l _ResidentAddToFSResource

_entrypoint:

		sub.l a3,a3
		move.l	4.w,a6

	/* stack large enough? */

		sub.l a1,a1
		jsr _LVOFindTask(a6)
		move.l d0,a0
		move.l 62(a0),d0
		sub.l 58(a0),d0
		cmp.l #4000,d0
		bcc.b 3f

	/* allocate stackswap memory */

		move.l #4000+12,d0
		moveq #1,d1
		jsr	_LVOAllocMem(a6)
		move.l	d0,a3
		tst.l d0
		beq nostack
		add.l	#12,d0
		move.l	d0,(a3)
		add.l	#4000,d0
		move.l	d0,4(a3)
		move.l	d0,8(a3)
		move.l	a3,a0

	/* check exec revision */

		cmp.w #37,0x14(a6)	/* SysBase->LibNode.lib_Version */
		bcc.b 1f

	/* stackswap V36 and older */

		bsr	myStackSwap
		bra.b	2f

	/* stackswap V37 */

1:
		jsr	_LVOStackSwap(a6)
2:

	/* remember sss address and enter filesystem */
3:
		move.l	a3,-(a7)

		bsr	_EntryPoint

		move.l (sp)+,d2
		beq 3f
		move.l d2,a0

		cmp.w #37,20(a6)
		bcc.b 1f

		bsr myStackSwap
		bra.b 2f

1:
		jsr	_LVOStackSwap(a6)
2:

	/* free stack */

		move.l d2,a1
		move.l	#4000+12,d0
		jsr	_LVOFreeMem(a6)	

3:
	/* exit AFS */
end:
		moveq	#0,d0	
		rts

	/* no memory for the stack: fail the startup packet (DOSFALSE,
	 * ERROR_NO_FREE_STORE) instead of leaving DOS waiting forever */
nostack:
		sub.l	a1,a1
		jsr	_LVOFindTask(a6)
		add.l	#92,d0			/* pr_MsgPort */
		move.l	d0,a3
		move.l	a3,a0
		jsr	_LVOWaitPort(a6)
		move.l	a3,a0
		jsr	_LVOGetMsg(a6)
		move.l	d0,a0
		move.l	10(a0),a1		/* ln_Name: the DosPacket */
		moveq	#0,d0
		move.l	d0,12(a1)		/* dp_Res1 = DOSFALSE */
		moveq	#103,d0
		move.l	d0,16(a1)		/* dp_Res2 = ERROR_NO_FREE_STORE */
		move.l	28(a1),d0		/* dp_Arg3: DeviceNode */
		beq.b	1f
		lsl.l	#2,d0
		move.l	d0,a0
		clr.l	8(a0)			/* dn_Task */
1:
		move.l	4(a1),a0		/* reply port */
		move.l	a3,4(a1)		/* dp_Port = our port */
		move.l	a1,d0
		move.l	(a1),a1			/* dp_Link */
		move.l	d0,10(a1)		/* ln_Name = packet */
		clr.l	(a1)
		clr.l	4(a1)
		jsr	_LVOPutMsg(a6)
		bra.b	end

myStackSwap:

		jsr	_LVODisable(a6)

		move.l 276(a6),a1

		move.l	62(a1),d0
		move.l	4(a0),62(a1)
		move.l	d0,4(a0)

		move.l	58(a1),d0
		move.l	(a0),58(a1)
		move.l	d0,(a0)

		move.l	8(a0),54(a1)

		move.l	(a0),a1
		move.l	8(a0),a1
		move.l	(sp)+,d0
		move.l	sp,8(a0)
		move.l	d0,-(a1)
		move.l	a1,sp

		jmp	_LVOEnable(a6)
