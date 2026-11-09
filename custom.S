/* Toni Wilen's custom-loader scheme, in GNU assembler syntax.
 * The normal executable is embedded verbatim. tools/hunk.py verifies the
 * one-CODE-hunk/one-self-relocation-group layout required below.
 * The outer executable has no relocations, for broken RDB loaders.
 */

	.text
	.globl _CustomStart
_CustomStart:
	move.l d2,-(sp)
	move.l 4.w,a6
	lea DriverStart+0x1c(pc),a0
	move.l (a0)+,d0          /* CODE size, in longwords */
	move.l a0,a1            /* handler entry point */
	lsl.l #2,d0
	add.l d0,a0             /* HUNK_RELOC32 */
	tst.l (a0)
	bmi.s relocated         /* this loaded copy was already relocated */
	moveq #-1,d0
	move.l d0,(a0)+         /* mark this copy; leave the input file unchanged */
	move.l (a0)+,d2         /* relocation count */
	addq.l #4,a0            /* target hunk number (zero) */
	move.l a1,d1
reloc:
	move.l (a0)+,d0
	add.l d1,0(a1,d0.l)
	subq.l #1,d2
	bne.s reloc

	/* Flush patched instructions on cached CPUs, where Exec provides it.
	 * This must be reachable after the relocation loop. CacheClearU may
	 * clobber a1, so preserve the handler entry point across the call.
	 */
	cmp.w #36,20(a6)
	bcs.s relocated
	move.l a1,-(sp)
	jsr -636(a6)            /* CacheClearU */
	move.l (sp)+,a1
relocated:
	move.l (sp)+,d2
	jmp (a1)

	.balign 4
DriverStart:
	.incbin DRIVER_BINARY
