
CC=/opt/amiga/bin/m68k-amigaos-gcc

LIBS=-lamiga -lgcc -lnix13 -lnix -s

CFLAGS=-Os -fbbb=+ -m68000 -noixemul -fomit-frame-pointer -mregparm=3 -msmall-code -s

TOOLS = setdeldir setfnsize setlargefile

# pfsformat writes the partition for a full format through pfsdoctor's device layer
PFSFORMAT_OBJS = pfsformat.o fullformat.o pfsdoctor-device.o

.PHONY: all clean

all: $(TOOLS) pfsformat

$(TOOLS): %: %.o
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

%.o: %.c makefile versions.h
	$(CC) $(CFLAGS) -I. -c -o $@ $<

setdeldir.o: ../blocks.h
pfsformat.o: ../pfs3disk.h

pfsformat: $(PFSFORMAT_OBJS)
	$(CC) -o $@ $(PFSFORMAT_OBJS) $(CFLAGS) $(LIBS)

fullformat.o: fullformat.c makefile ../pfsdoctor/doctor.h ../pfsdoctor/pfs3.h ../pfs3disk.h
	$(CC) $(CFLAGS) -I. -I../pfsdoctor -c -o $@ $<

pfsdoctor-device.o: ../pfsdoctor/device.c makefile ../pfsdoctor/doctor.h ../pfsdoctor/pfs3.h ../pfs3disk.h
	$(CC) $(CFLAGS) -I../pfsdoctor -c -o $@ $<

clean:
	$(RM) $(TOOLS) $(TOOLS:=.o) pfsformat $(PFSFORMAT_OBJS)
