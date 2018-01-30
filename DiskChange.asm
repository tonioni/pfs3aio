; $Id: DiskChange.asm 10.2 1998/05/27 20:16:13 Michiel Exp Michiel $
; $Log: DiskChange.asm $
;; Revision 10.2  1998/05/27  20:16:13  Michiel
;; Overdue update
;;
;; Revision 10.1  1994/10/24  11:28:41  Michiel
;; First RCS revision
;;

* diskhandler.asm

	include "include:exec/types.i"
	include "include:exec/funcdef.i"
	include "include:exec/macros.i"
	include "include:exec/exec_lib.i"

 STRUCTURE IntData,0

	APTR 	id_Task
	APTR 	id_Interrupt
	ULONG 	id_Signal
	LABEL 	id_SIZEOF

;-----------------------------------------------------------------------------
;	REFS
;-----------------------------------------------------------------------------

		xdef _DiskChangeHandler
		xdef @DiskChangeHandler

;-----------------------------------------------------------------------------
	SECTION text,code
;-----------------------------------------------------------------------------

* DiskChangeHandler
*
* D0, D1, A0, A1, A5, A6: scratch
* A1 = is_Data
*

_DiskChangeHandler:
@DiskChangeHandler:

		move.l	a0,-(a7)
		move.l	4,a6
		move.l	id_Signal(a1),d0
		move.l	id_Task(a1),a1
		jsr	_LVOSignal(a6)
		move.l	(a7)+,a0
		moveq	#0,d0		;enable following servers
		rts

	END
