; $Id: AssRoutines.asm 10.6 1996/01/30 12:56:07 Michiel Exp Michiel $
; $Log: AssRoutines.asm $
;; Revision 10.6  1996/01/30  12:56:07  Michiel
;; intlcdcmp added
;; --- working tree overlap ---
;;
;; Revision 10.5  1996/01/03  09:54:34  Michiel
;; using SysBase
;;
;; Revision 10.4  1995/07/27  12:26:52  Michiel
;; New startup code
;; AfsDie function
;;
;; Revision 10.3  1995/07/10  04:56:05  Michiel
;; Added V36 replacement StackSwap routine
;;
;; Revision 10.2  1995/04/13  13:23:24  Michiel
;; Protection routines added
;;
;; Revision 10.1  1994/10/24  11:28:41  Michiel
;; First RCS revision
;;

	include "exec/funcdef.i"
	include "exec/exec_lib.i"
	include "exec/memory.i"
	include "exec/tasks.i"


;-----------------------------------------------------------------------------
;	REFS
;-----------------------------------------------------------------------------

		xdef	@divide
		xdef	@ctodstr	
		xdef	@intltoupper
		xdef	@intlcmp
		xdef	@intlcdcmp

		xdef	@myStackSwap

		xdef	@AfsDie
		xdef	@InitStack

		xref	_SysBase
		xref	@EntryPoint

;-----------------------------------------------------------------------------
	SECTION text,code
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; StackSwap and DIE routines
;-----------------------------------------------------------------------------

; entrypoint of program!!
@InitStack

	; allocate stackswap memory

		move.l	($4).w,a6
		move.l	#StackSwapStruct_SIZEOF,d0
		move.l	#MEMF_CLEAR,d1
		jsr	_LVOAllocMem(a6)
		move.l	d0,a3

	; allocate stackswap memory

		move.l	#5000,d0
		; move.l	#(MEMF_CLEAR|MEMF_PUBLIC),d1
		move.l	#$10001,d1
		jsr	_LVOAllocMem(a6)
		move.l	d0,stk_Lower(a3)
		add.l	#5000,d0
		move.l	d0,stk_Upper(a3)
		move.l	d0,stk_Pointer(a3)

	; do the stackswap
	; check exec revision

		move.w	$14(a6),d0	; SysBase->LibNode.lib_Version
		cmp.w	#36,d0

		beq	ss_V36
		bcs	ss_V35
		bra	ss_V37

	; stackswap V35
ss_V35		moveq.l	#0,d0
		jsr	_LVOWait(a6)

	; stackswap V36
ss_V36		move.l	a3,a0
		bsr	@myStackSwap
		bra	ss_after

	; stackswap V37
ss_V37		move.l	a3,a0
		jsr	_LVOStackSwap(a6)
ss_after

	; remember sss address and enter filesystem

		move.l	a3,-(a7)
		jmp	@EntryPoint


;-----------------------------------------------------------------------------
;	DIE
;-----------------------------------------------------------------------------

@AfsDie

	; find my task structure

		move.l	($4).w,a6
		suba.l	a1,a1
		jsr	_LVOFindTask(a6)
		move.l	d0,a0

	; clear stack and get StackSwapStruct

		move.l	TC_SPUPPER(a0),a7
		move.l	TC_SPLOWER(a0),a3
		lea	-4(a7),a7
		move.l	(a7)+,a0
		jsr	_LVOStackSwap(a6)

	; free stack

		move.l	a3,a1
		move.l	#5000,d0
		jsr	_LVOFreeMem(a6)	

	; exit AFS

		moveq	#0,d0	
		rts
		

;-----------------------------------------------------------------------------
;	void myStackSwap (struct StackSwapStruct *)
;-----------------------------------------------------------------------------
_myStackSwap:
@myStackSwap:	

		move.l	($4).w,a6
		suba.l	a1,a1
		move.l	a0,-(a7)
		jsr	_LVOFindTask(a6)
		move.l	d0,-(a7)
		jsr	_LVODisable(a6)

		move.l	(a7)+,a1		; my task
		move.l	(a7)+,a0		; my stack
		move.l	(a7)+,d0		; return address
		move.l	stk_Lower(a0),TC_SPLOWER(a1)
		move.l	stk_Upper(a0),TC_SPUPPER(a1)
		move.l	stk_Pointer(a0),a7	; new stackptr
		move.l	a7,TC_SPREG(a1)
		move.l	d0,-(a7)

		jmp	_LVOEnable(a6)

;-----------------------------------------------------------------------------
;	ULONG divide (UWORD d0, UWORD d1)
;-----------------------------------------------------------------------------
_divide:
@divide:	divu	d1,d0
		rts

;-----------------------------------------------------------------------------
;	void ctodstr(cstring *a0, dstring *a1)
;
;	converts cstring a0 to dstring a1
;-----------------------------------------------------------------------------
_ctodstr:
@ctodstr:
		move.l	a1,-(a7)
		lea	1(a1),a1
		moveq	#-1,d0
ctodstr_loop	addq	#1,d0
		move.b	(a0)+,(a1)+
		bne	ctodstr_loop
		move.l	(a7)+,a1
		move.b	d0,(a1)
		rts

;-----------------------------------------------------------------------------
;	void intltoupper(dstr *a0)
;
;	converts dstring a0 to uppercase in international mode
;	zie intlcmp
;-----------------------------------------------------------------------------
_intltoupper
@intltoupper
		moveq	#0,d0
		move.b	(a0)+,d0
		beq	intlto_exit
		subq.w	#1,d0

intlto_loop	move.b	(a0),d1
		cmp.b	#$61,d1
		blo	intlto_next
		cmp.b	#$7a,d1
		bls	intlto_conv
		cmp.b	#$e0,d1
		blo	intlto_next
		cmp.b	#$f6,d1
		bls	intlto_conv
		cmp.b	#$f8,d1
		blo	intlto_next
		cmp.b	#$fe,d1
		bls	intlto_conv
		bra	intlto_next

intlto_conv	sub.b	#$20,d1
intlto_next	move.b	d1,(a0)+
		dbra	d0,intlto_loop

intlto_exit	rts


;-----------------------------------------------------------------------------
;	bool intlcmp(dstr *a0, dstr *a1)
;	
;	compares dstring a with dstring b in international mode.
;	a must be 'uppercased' as follows:
;
;	0x00 - 0x60 -> 0x00 - 0x60
;	0x61 - 0x7a -> 0x41 - 0x5a diff 0x20
;	0x7b - 0xdf -> 0x7b - 0xdf
;	0xe0 - 0xf6 -> 0xc0 - 0xd6 diff 0x20
;	0xf7	    -> 0xf7
;	0xf8 - 0xfe -> 0xd8 - 0xde diff 0x20
;	0xff	    -> 0xff
;
;	So if match then (d0 = d1) \/ (d1-d0 = 0x20)
;-----------------------------------------------------------------------------
_intlcmp:
@intlcmp:	move.l	d2,-(a7)
		moveq	#0,d2
		move.b	(a0)+,d2	;check size
		cmp.b	(a1)+,d2
		bne	intl_nomatch

		subq.w	#1,d2		;decrease for dbra
		bmi	intl_match2

intl_compchar	move.b	(a0)+,d0
		move.b	(a1)+,d1
		sub.b	d0,d1	
		bvs	intl_nomatch
		beq	intl_match	;d0 = d1 ?
		cmp.b	#$20,d1
		bne	intl_nomatch	;d1 - d0 = 0x20 ? (dan d0 max e0)
		cmp.b	#$41,d0
		blo	intl_nomatch
		cmp.b	#$5a,d0
		bls	intl_match
		cmp.b	#$c0,d0
		blo	intl_nomatch
		cmp.b	#$d7,d0
		beq	intl_nomatch
		cmp.b	#$de,d0
		bhi	intl_nomatch

intl_match	dbra	d2,intl_compchar
intl_match2	moveq	#1,d0
		move.l	(a7)+,d2
		rts

intl_nomatch	moveq	#0,d0
		move.l	(a7)+,d2
		rts


;-----------------------------------------------------------------------------
;	bool intlcdcmp(cstr *a0, dstr *a1)
;	
;	compares cstring a with dstring b in international mode.
;	a must be 'uppercased' as follows:
;
;	0x00 - 0x60 -> 0x00 - 0x60
;	0x61 - 0x7a -> 0x41 - 0x5a diff 0x20
;	0x7b - 0xdf -> 0x7b - 0xdf
;	0xe0 - 0xf6 -> 0xc0 - 0xd6 diff 0x20
;	0xf7	    -> 0xf7
;	0xf8 - 0xfe -> 0xd8 - 0xde diff 0x20
;	0xff	    -> 0xff
;
;	So if match then (d0 = d1) \/ (d1-d0 = 0x20)
;-----------------------------------------------------------------------------
_intlcdcmp:
@intlcdcmp:	move.l	d2,-(a7)
		moveq	#0,d2
		move.b	(a1)+,d2	;size
		subq.w	#1,d2		;decrease for dbra
		bpl	intl2_compchar
		tst.b	(a0)		;zero length string
		beq	intl2_match2
		bra	intl2_nomatch

intl2_compchar	move.b	(a0)+,d0
		beq	intl2_nomatch
		move.b	(a1)+,d1
		sub.b	d0,d1	
		bvs	intl2_nomatch
		beq	intl2_match	;d0 = d1 ?
		cmp.b	#$20,d1
		bne	intl2_nomatch	;d1 - d0 = 0x20 ? (dan d0 max e0)
		cmp.b	#$41,d0
		blo	intl2_nomatch
		cmp.b	#$5a,d0
		bls	intl2_match
		cmp.b	#$c0,d0
		blo	intl2_nomatch
		cmp.b	#$d7,d0
		beq	intl2_nomatch
		cmp.b	#$de,d0
		bhi	intl2_nomatch

intl2_match	dbra	d2,intl2_compchar
		tst.b	(a0)
		bne	intl2_nomatch	;length unequal
intl2_match2	moveq	#1,d0
		move.l	(a7)+,d2
		rts

intl2_nomatch	moveq	#0,d0
		move.l	(a7)+,d2
		rts

	END

