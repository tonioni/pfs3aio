
putproc:
	move.b d0,(a3)+
	rts

_rawdofmt:
	movem.l a2/a3/a6,-(sp)
	move.l 4.w,a6
	move.l 8+16(sp),a1
	move.l 4+16(sp),a0
	move.l 0+16(sp),a3
	lea putproc(pc),a2
	jsr -0x20a(a6)
	movem.l (sp)+,a2/a3/a6
	rts
