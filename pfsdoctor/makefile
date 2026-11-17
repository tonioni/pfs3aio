
CC=/opt/amiga/bin/m68k-amigaos-gcc

LIBS=-lamiga -lgcc -lnix13 -s

CFLAGS=-Os -m68000 -noixemul -fomit-frame-pointer

OBJS = console.o access.o device.o fullscan.o standardscan.o stats.o

.PHONY: all clean

all: $(OBJS)
	$(CC) -o pfsdoctor $^ $(CFLAGS) $(LIBS)

$(OBJS): doctor.h pfs3.h ../pfs3disk.h makefile

clean:
	$(RM) $(OBJS) pfsdoctor

console.o: console.c
	$(CC) $(CFLAGS) -I. -c -o $@ console.c

access.o: access.c
	$(CC) $(CFLAGS) -I. -c -o $@ access.c

device.o: device.c
	$(CC) $(CFLAGS) -I. -c -o $@ device.c

fullscan.o: fullscan.c
	$(CC) $(CFLAGS) -I. -c -o $@ fullscan.c

standardscan.o: standardscan.c
	$(CC) $(CFLAGS) -I. -c -o $@ standardscan.c

stats.o: stats.c
	$(CC) $(CFLAGS) -I. -c -o $@ stats.c
