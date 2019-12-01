
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""

CC=/opt/amiga/bin/m68k-amigaos-gcc

LIBS=-nostdlib -lamiga -lgcc -lnix13 -lnix -s

CFLAGS=-Os -fbbb=+ -m68000 -noixemul -fomit-frame-pointer -nostartfiles -mregparm=3 -msmall-code -s \
	-DMIN_LIB_VERSION=33 -DKSWRAPPER=1 \
	-DSCSIDIRECT=1 -DNSD=1 -DTD64=1 -DTRACKDISK=1 \
	-DLARGE_FILE_SIZE=0 -DEXTRAPACKETS=1 -DSIZEFIELD -DDELDIR=1 \
	-DMULTIUSER=0 -DPROTECTION=0 -DVERSION23=1 -DROLLOVER=1 \
	-DLIMIT_MAXTRANSFER=0x20000 -DUNSAFEQUIT=1 \
	-DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME)

OBJS = startup.o boot.o init.o assroutines.o messages.o volume.o lock.o dostohandlerinterface.o anodes.o directory.o lru.o disk.o \
	update.o allocation.o CheckAccess.o diskchange.o resethandler.o kswrapper.o format.o fsresource.o end.o

all: $(OBJS)
	$(CC) -o pfs3aio $^ $(CFLAGS) $(LIBS)

startup.o: startup.s
	$(CC) $(CFLAGS) -I. -c -o $@ startup.s

end.o: end.s
	$(CC) $(CFLAGS) -I. -c -o $@ end.s

rawdofmt.o: rawdofmt.s
	$(CC) $(CFLAGS) -I. -c -o $@ rawdofmt.s

debug.o: debug.c
	$(CC) $(CFLAGS) -I. -c -o $@ debug.c

assroutines.o: assroutines.c
	$(CC) $(CFLAGS) -I. -c -o $@ assroutines.c

messages.o: messages.c
	$(CC) $(CFLAGS) -I. -c -o $@ messages.c

boot.o: boot.c
	$(CC) $(CFLAGS) -I. -c -o $@ boot.c

directory.o: directory.c
	$(CC) $(CFLAGS) -I. -c -o $@ directory.c

dostohandlerinterface.o: dostohandlerinterface.c dd_funcs.c
	$(CC) $(CFLAGS) -I. -c -o $@ dostohandlerinterface.c

disk.o: disk.c
	$(CC) $(CFLAGS) -I. -c -o $@ disk.c

init.o: init.c
	$(CC) $(CFLAGS) -I. -c -o $@ init.c

diskchange.o: diskchange.c
	$(CC) $(CFLAGS) -I. -c -o $@ diskchange.c

resethandler.o: resethandler.c
	$(CC) $(CFLAGS) -I. -c -o $@ resethandler.c

volume.o: volume.c
	$(CC) $(CFLAGS) -I. -c -o $@ volume.c

allocation.o: allocation.c
	$(CC) $(CFLAGS) -I. -c -o $@ allocation.c

lock.o: lock.c
	$(CC) $(CFLAGS) -I. -c -o $@ lock.c

anodes.o: anodes.c
	$(CC) $(CFLAGS) -I. -c -o $@ anodes.c

format.o: format.c
	$(CC) $(CFLAGS) -I. -c -o $@ format.c

lru.o: lru.c
	$(CC) $(CFLAGS) -I. -c -o $@ lru.c

update.o: update.c
	$(CC) $(CFLAGS) -I. -c -o $@ update.c

CheckAccess.o: CheckAccess.c
	$(CC) $(CFLAGS) -I. -c -o $@ CheckAccess.c

fsresource.o: fsresource.c
	$(CC) $(CFLAGS) -I. -c -o $@ fsresource.c

kswrapper.o: kswrapper.c
	$(CC) $(CFLAGS) -I. -c -o $@ kswrapper.c
