
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""

CC=/opt/amiga/bin/m68k-amigaos-gcc

LIBS=-lamiga -lgcc -lnix13 -lnix -s

CFLAGS=-Os -fbbb=+ -m68000 -noixemul -fomit-frame-pointer -mregparm=3 -msmall-code -s \
	-DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME)

TOOLS = setdeldir setfnsize setlargefile

.PHONY: all clean

all: $(TOOLS)

$(TOOLS): %: %.o
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

%.o: %.c makefile
	$(CC) $(CFLAGS) -I. -c -o $@ $<

setdeldir.o: ../blocks.h

clean:
	$(RM) $(TOOLS) $(TOOLS:=.o)
