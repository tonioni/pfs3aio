
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""
.DEFAULT_GOAL := all

CC=/opt/amiga/bin/m68k-amigaos-gcc
LHA ?= lha

# Distribution version only; the handler's ABI version remains in
# versionhistory.doc. Full history is needed for untagged CI builds.
FULL_VERSION ?= $(shell git describe --tags --dirty --always 2>/dev/null | sed -e 's/^release_//' -e 's/^v//')
export FULL_VERSION
DISTDIR := dist
PYTHON ?= python3
LARGE_FILE_SIZE ?= 0
ifeq ($(LARGE_FILE_SIZE),0)
BUILD_DIR := build/normal
TARGET := pfs3aio
else ifeq ($(LARGE_FILE_SIZE),1)
BUILD_DIR := build/largefile
TARGET := pfs3aio-largefile
else
$(error LARGE_FILE_SIZE must be 0 or 1)
endif
LDFLAGS += -Wl,-T,pfs3aio.ld

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
	-DLARGE_FILE_SIZE=$(LARGE_FILE_SIZE) -DEXTRAPACKETS=1 -DSIZEFIELD -DDELDIR=1 \
	-DMULTIUSER=0 -DPROTECTION=0 -DVERSION23=1 -DROLLOVER=1 \
	-DLIMIT_MAXTRANSFER=0x20000 -DUNSAFEQUIT=1 \
	-DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME) \
	-DDETECTDEBUG=0 -DWARN_EXPERIMENTAL=0

OBJECTS = startup.o boot.o init.o assroutines.o messages.o volume.o lock.o dostohandlerinterface.o anodes.o directory.o lru.o disk.o \
	update.o allocation.o CheckAccess.o diskchange.o resethandler.o kswrapper.o format.o fsresource.o end.o
OBJS = $(addprefix $(BUILD_DIR)/,$(OBJECTS))
C_OBJS = $(filter-out $(BUILD_DIR)/startup.o $(BUILD_DIR)/end.o,$(OBJS))
MISSING_DEPS = $(foreach obj,$(C_OBJS),$(if $(wildcard $(obj:.o=.d)),,$(obj)))

-include $(C_OBJS:.o=.d)

.PHONY: all largefile all-variants floppy lha archive check clean force-deps
$(MISSING_DEPS): force-deps
force-deps:

all: $(TARGET) $(TARGET)-custom

$(TARGET): $(OBJS) pfs3aio.ld tools/hunk.py
	$(CC) -o $@ $(OBJS) $(CFLAGS) $(LDFLAGS) $(LIBS)
	$(PYTHON) tools/hunk.py normal $@

$(BUILD_DIR)/custom.o: custom.S $(TARGET) tools/hunk.py | $(BUILD_DIR)
	$(PYTHON) tools/hunk.py normal $(TARGET)
	$(CC) -m68000 -DDRIVER_BINARY='"$(TARGET)"' -c -o $@ custom.S

$(TARGET)-custom: $(BUILD_DIR)/custom.o $(TARGET) pfs3aio.ld tools/hunk.py
	$(CC) -m68000 -nostdlib -s $(LDFLAGS) -o $@ $(BUILD_DIR)/custom.o
	$(PYTHON) tools/hunk.py custom $(TARGET) $@

largefile:
	$(MAKE) LARGE_FILE_SIZE=1 all

all-variants:
	$(MAKE) LARGE_FILE_SIZE=0 all
	$(MAKE) LARGE_FILE_SIZE=1 all

$(OBJS): makefile

# 880K AmigaDOS floppy holding the handlers.
FLOPPY_FILES = pfs3aio pfs3aio-custom pfs3aio-largefile pfs3aio-largefile-custom LICENSE versionhistory.doc
floppy: all-variants
	$(PYTHON) tools/mkadf.py pfs3aio.adf PFS3AIO $(FLOPPY_FILES)

check:
	bash tests/live/run.sh

# Build in a fresh staging directory, so repeated invocations cannot retain
# obsolete files in an existing archive. Only publish a CRC-checked archive.
lha: all-variants
	$(MAKE) archive

# CI packages the already-built matrix artifacts through this target.
archive: dist.readme.in
	$(PYTHON) tools/hunk.py custom pfs3aio pfs3aio-custom
	$(PYTHON) tools/hunk.py custom pfs3aio-largefile pfs3aio-largefile-custom
	@set -eu; \
	version="$$FULL_VERSION"; \
	case "$$version" in \
	  ''|*[!A-Za-z0-9._+-]*) echo "Invalid FULL_VERSION: $$version" >&2; exit 1;; \
	esac; \
	mkdir -p $(DISTDIR); \
	stage=$$(mktemp -d "$(DISTDIR)/.lha.XXXXXX"); \
	trap 'rm -rf "$$stage"' EXIT HUP INT TERM; \
	package="pfs3aio_$$version"; \
	mkdir "$$stage/$$package"; \
	cp -p pfs3aio pfs3aio-custom pfs3aio-largefile pfs3aio-largefile-custom \
	  LICENSE versionhistory.doc "$$stage/$$package/"; \
	sed "s/@VERSION@/$$version/g" dist.readme.in > "$$stage/$$package.readme"; \
	cp "$$stage/$$package.readme" "$$stage/$$package/pfs3aio.readme"; \
	{ printf 'pfs3aio build %s\n' "$$version"; \
	  git rev-parse HEAD 2>/dev/null || printf 'Source revision unavailable\n'; \
	  $(CC) --version | head -n 1; \
	  printf 'pfs3aio[-custom]: LARGE_FILE_SIZE=0\n'; \
	  printf 'pfs3aio-largefile[-custom]: LARGE_FILE_SIZE=1\n'; \
	} > "$$stage/$$package/Build.txt"; \
	(cd "$$stage" && $(LHA) co5 "$$package.lha" "$$package"); \
	$(LHA) t "$$stage/$$package.lha"; \
	mv "$$stage/$$package.lha" "$(DISTDIR)/$$package.lha"; \
	mv "$$stage/$$package.readme" "$(DISTDIR)/$$package.readme"

clean:
	$(RM) build/normal/*.o build/normal/*.d build/largefile/*.o build/largefile/*.d
	$(RM) pfs3aio pfs3aio-custom pfs3aio-largefile pfs3aio-largefile-custom pfs3aio.adf

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.s makefile | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -c -o $@ $<

$(C_OBJS): $(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -MMD -MP -c -o $@ $<
