
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""
.DEFAULT_GOAL := all

CC=/opt/amiga/bin/m68k-amigaos-gcc

LIBS=-nostdlib -lamiga -lgcc -lnix13 -lnix -s

# -fno-optimize-sibling-calls works around a code generation bug in
# amiga-gcc 13.x with -mregparm=3: a sibling (tail) call to a function
# whose 4th data argument is passed in the callee-saved register d2 is
# emitted with the caller's d2 restored in the epilogue BEFORE the jump
# and the argument never loaded at all. In this tree that miscompiles
# the RawRead/RawWrite -> RawReadWrite_TD/_DS tail calls (5-argument
# functions since the 2018 merge): every raw disk access gets blocknr=0.
# amiga-gcc 6.x does not show this. Verified with a 25-line reproducer;
# with this flag gcc emits the correct move.l d1,d2 + jsr sequence.
# -fno-strict-aliasing: the code reads structs through other pointer types,
# which gcc 12 and later reorder.
CFLAGS=-Os -fbbb=+ -m68000 -noixemul -fomit-frame-pointer -nostartfiles -mregparm=3 -fno-optimize-sibling-calls -fno-strict-aliasing -msmall-code -s \
	-DMIN_LIB_VERSION=33 -DKSWRAPPER=1 \
	-DSCSIDIRECT=1 -DNSD=1 -DTD64=1 -DTRACKDISK=1 \
	-DLARGE_FILE_SIZE=0 -DEXTRAPACKETS=1 -DSIZEFIELD -DDELDIR=1 \
	-DMULTIUSER=0 -DPROTECTION=0 -DVERSION23=1 -DROLLOVER=1 \
	-DLIMIT_MAXTRANSFER=0x20000 -DUNSAFEQUIT=1 \
	-DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME) \
	-DDETECTDEBUG=0 -DWARN_EXPERIMENTAL=0

OBJS = startup.o boot.o init.o assroutines.o messages.o volume.o lock.o dostohandlerinterface.o anodes.o directory.o lru.o disk.o \
	update.o allocation.o CheckAccess.o diskchange.o resethandler.o kswrapper.o format.o fsresource.o end.o
C_OBJS = $(filter-out startup.o end.o,$(OBJS))
MISSING_DEPS = $(foreach obj,$(C_OBJS),$(if $(wildcard $(obj:.o=.d)),,$(obj)))

-include $(C_OBJS:.o=.d)

.PHONY: all force-deps
$(MISSING_DEPS): force-deps
force-deps:

all: pfs3aio

pfs3aio: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

startup.o: startup.s
	$(CC) $(CFLAGS) -I. -c -o $@ startup.s

end.o: end.s
	$(CC) $(CFLAGS) -I. -c -o $@ end.s

rawdofmt.o: rawdofmt.s
	$(CC) $(CFLAGS) -I. -c -o $@ rawdofmt.s

$(C_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -I. -MMD -MP -c -o $@ $<
