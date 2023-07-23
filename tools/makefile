
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""

CC=/opt/amiga/bin/m68k-amigaos-gcc

LIBS=-lamiga -lgcc -lnix13 -lnix -s

CFLAGS=-Os -fbbb=+ -m68000 -noixemul -fomit-frame-pointer -mregparm=3 -msmall-code -s \
	-DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME)

all: setdeldir.o setfnsize.o setlargefile.o
	$(CC) -o setdeldir setdeldir.o $(CFLAGS) $(LIBS)
	$(CC) -o setfnsize setfnsize.o $(CFLAGS) $(LIBS)
	$(CC) -o setlargefile setlargefile.o $(CFLAGS) $(LIBS)

setdeldir.o: setdeldir.c
	$(CC) $(CFLAGS) -I. -c -o $@ setdeldir.c

setfnsize.o: setfnsize.c
	$(CC) $(CFLAGS) -I. -c -o $@ setfnsize.c

setlargefile.o: setlargefile.c
	$(CC) $(CFLAGS) -I. -c -o $@ setlargefile.c
