# Build pfs3aio with every toolchain, one binary per target CPU from each.
# This is what a bare `make` runs; ./makefile is upstream's single-binary build.
#
#   make help
#
# Compilation is delegated to makefile.gcc, so flags stay in one place. This
# file only selects a toolchain and an output directory.
# See TOOLCHAINS.md for where each toolchain comes from and what was measured.

SHELL := /bin/bash

OUT ?= compare

.DEFAULT_GOAL := all

comma := ,
empty :=
space := $(empty) $(empty)

# The gcc versions come from one image family, so there is one way to get a
# toolchain. They all install into /opt/amiga, which the auto-detection in
# makefile.gcc finds, so none of them needs AMIGA_PREFIX.
GCC_IMAGE ?= docker.io/stefanreinauer/amiga-gcc
GCC6_TAG ?= gcc-v6.5.0b-20251218
GCC13_TAG ?= gcc-v13.4
GCC16_TAG ?= gcc-v16.2

# gcc 6.5 takes a dated tag, not the floating one: that now carries a build
# which miscompiles intlcmp at 68020+. See TOOLCHAINS.md, "Issue 3".

# gcc 16 defaults to C23, where struct.h:306 "typedef enum {false, true}
# bool;" is a syntax error. gcc 6.5 does not know -std=gnu17 at all; 13.4
# accepts it but does not need it, and leaving it off keeps its output
# byte-identical to what the previous locally built image produced.
GCC6_EXTRA ?=
GCC13_EXTRA ?=
GCC16_EXTRA ?= -std=gnu17

# Appended to every toolchain. Empty by default so results match a plain
# "make".
COMMON_EXTRA ?=

PODMAN ?= podman

# Pin these to compare two builds byte for byte; unset they are today's date.
NOWDATE ?=
NOWTIME ?=
DATES = $(if $(NOWDATE),NOWDATE='\"$(NOWDATE)\"') $(if $(NOWTIME),NOWTIME='\"$(NOWTIME)\"')

# Which fork and which source, tag or commit, stamped into the version string.
# Computed here so the containers never run git against the mounted repo, which
# it refuses as dubious ownership.
FORK ?= jpu
PFS_REF ?= $(shell git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD 2>/dev/null)
ORIGIN = FORK=$(FORK) PFS_REF=$(PFS_REF)

include tiers.mk

# Only tiers that reach the 68060 are scanned by check-68060.
TIERS_060 = $(foreach t,$(TIERS),$(if $(filter 68060,$(TIER_MAXCPU_$(t))),$(t)))
# Tiers a given toolchain can build, see TIER_SKIP_<tier> in tiers.mk.
TIERS_FOR = $(foreach t,$(TIERS),$(if $(filter $(1),$(TIER_SKIP_$(t))),,$(t)))
TIERS_NOT_060 = $(filter-out $(TIERS_060),$(TIERS))

# Pairs toolchain:tier that tiers.mk excludes, so install and dist never pick
# up a stale build of one that is left in $(OUT).
SKIPPED = $(foreach t,$(ALL_TIERS),$(foreach s,$(TIER_SKIP_$(t)),$(s):$(t)))

# Install destination. Layout matches fat95 and sfs under /opt/AmigaOS:
# <version>/<cpu>/<binary>.
INSTALL_DIR ?= /opt/AmigaOS/pfs/v20.0

# Repo read-write at /src (objects go to /out, nothing is written into /src),
# output at /out. label=disable avoids relabelling the repo for SELinux.
RUN = $(PODMAN) run --rm --security-opt label=disable \
	-v $(CURDIR):/src -v $(CURDIR)/$(OUT):/out -w /src

BUILD = $(MAKE) --no-print-directory -f makefile.gcc

GCC_TOOLCHAINS = gcc6 gcc13 gcc16
TOOLCHAINS = $(GCC_TOOLCHAINS)

.PHONY: all $(TOOLCHAINS) sizes sibcall-proof install \
	$(addprefix asm-,$(TOOLCHAINS)) check-68060 check-linea verify bench driverbench dist \
	clean help

all: $(TOOLCHAINS) sizes

help:
	@echo "Targets:"
	@echo "  all             Build with every toolchain, then print sizes"
	@echo "  gcc6            gcc 6.5.0b        -> $(OUT)/gcc6/{$(subst $(space),$(comma),$(TIERS))}/pfs3aio"
	@echo "  gcc13           gcc 13.4.0b       -> $(OUT)/gcc13/{$(subst $(space),$(comma),$(TIERS))}/pfs3aio"
	@echo "  gcc16           gcc 16.2.0b       -> $(OUT)/gcc16/{$(subst $(space),$(comma),$(TIERS))}/pfs3aio"
	@echo "  sizes           Print the size table for whatever has been built"
	@echo "  sibcall-proof   Show the RawRead miscompilation with/without the flag"
	@echo "  install         Build every toolchain, install as pfs3aio.<tc> per build"
	@echo "  dist            Same set into $(DIST_DIR)/, which git ignores"
	@echo "  check-68060     Static audit: instructions the 68060 does not implement"
	@echo "  check-linea     Static audit: 68080 instructions in the line-A space"
	@echo "  verify          both audits plus format, write and read back under emulation"
	@echo "  bench           Synthetic workload, every toolchain and build"
	@echo "  driverbench     The real driver: format, write, read, list, create"
	@echo "  clean           Remove $(OUT)/"
	@echo ""
	@echo "Tiers: $(RELEASE_TIERS)"
	@echo "       EXTRA=1 adds $(EXTRA_TIERS); TIERS=\"030 040\" picks any"
	@echo "Knobs: OUT= COMMON_EXTRA= GCC_IMAGE= GCC6_TAG= GCC13_TAG="
	@echo "       GCC16_TAG= PODMAN="
	@echo "       INSTALL_DIR=$(INSTALL_DIR) INSTALL_TOOLCHAINS='$(INSTALL_TOOLCHAINS)'"
	@echo "Example: make install INSTALL_TOOLCHAINS=gcc16"

$(OUT):
	mkdir -p $(OUT)

# One rule per gcc version, differing only in tag and standard flag.
define GCC_RULES
$(1): | $$(OUT)
	@echo "=== $(2) ($$(GCC_IMAGE):$$($(3)_TAG)) ==="
	$$(RUN) $$(GCC_IMAGE):$$($(3)_TAG) make --no-print-directory -f makefile.gcc \
		OUTDIR=/out/$(1) TIERS="$$(call TIERS_FOR,$(1))" $$(DATES) $$(ORIGIN) \
		EXTRA_CFLAGS="$$($(3)_EXTRA) $$(COMMON_EXTRA)"

# Assembly for the 68060 audit, with that toolchain's own flags.
asm-$(1): | $$(OUT)
	$$(RUN) $$(GCC_IMAGE):$$($(3)_TAG) make --no-print-directory -f makefile.gcc \
		OUTDIR=/out/$(1) TIERS="$$(call TIERS_FOR,$(1))" $$(DATES) $$(ORIGIN) \
		EXTRA_CFLAGS="$$($(3)_EXTRA) $$(COMMON_EXTRA)" asm
endef

$(eval $(call GCC_RULES,gcc6,gcc 6.5.0b,GCC6))
$(eval $(call GCC_RULES,gcc13,gcc 13.4.0b,GCC13))
$(eval $(call GCC_RULES,gcc16,gcc 16.2.0b,GCC16))

# The 68060 cannot be emulated here (Musashi stops at 68040, which has the
# 64-bit multiply a 68060 lacks), so this is the only thing standing between a
# green test run and a binary that will not boot. Read from generated assembly,
# never from objdump.
check-68060: $(addprefix asm-,$(TOOLCHAINS))
	@$(foreach t,$(TIERS_NOT_060),echo "== 68060: $(t) tier not scanned: built $(TIER_CFLAGS_$(t)), reaches $(TIER_MAXCPU_$(t)) only, where 64-bit MULU.L is legal ==";)
	@rc=0; for tc in $(TOOLCHAINS); do \
		for cpu in $(TIERS_060); do \
			d=$(OUT)/$$tc/asm/$$cpu; \
			[ -d "$$d" ] || continue; \
			sh tests/check68060.sh "$$d" "$$tc/$$cpu" || rc=1; \
		done; \
	done; \
	[ $$rc -eq 0 ] && echo "== 68060: all builds clean ==" || { echo "== 68060: UNSAFE builds above =="; exit 1; }

# The 68080 cannot be emulated either. Only 080a is meant to have line-A.
check-linea: $(addprefix asm-,$(TOOLCHAINS))
	@rc=0; for tc in $(TOOLCHAINS); do \
		for cpu in $(TIERS); do \
			d=$(OUT)/$$tc/asm/$$cpu; \
			[ -d "$$d" ] || continue; \
			if [ "$$cpu" = 080a ]; then \
				sh tests/check-linea.sh "$$d" "$$tc/$$cpu" || true; \
			else \
				sh tests/check-linea.sh "$$d" "$$tc/$$cpu" || rc=1; \
			fi; \
		done; \
	done; \
	[ $$rc -eq 0 ] && echo "== line-A: only the 080a build has any ==" || { echo "== line-A: builds above carry it and must not =="; exit 1; }

# Functional check of every build under emulation, plus the static audits that
# emulation cannot do. Needs amifuse and rdbtool.
verify: all check-68060 check-linea
	sh tests/m68k/variants.sh $(OUT)

# Emulated cost of each build: bench is a synthetic workload, driverbench the
# real driver. Results in BENCHMARK.md, method in TOOLCHAINS.md.
bench:
	sh tests/m68k/bench.sh

driverbench: all
	sh tests/m68k/driverbench.sh $(OUT)

sizes:
	@echo ""
	@printf "%-12s" toolchain; for c in $(TIERS); do printf " %8s" $$c; done; echo
	@for t in $(TOOLCHAINS); do \
		printf "%-12s" $$t; \
		for c in $(TIERS); do \
			printf " %8s" $$(stat -c%s $(OUT)/$$t/$$c/pfs3aio 2>/dev/null || echo -); \
		done; echo; \
	done

# Builds disk.o both ways with gcc 13.4 and prints RawRead. With sibling calls
# enabled the "move.l d1,d2" that passes blocknr is missing. See TOOLCHAINS.md.
sibcall-proof: | $(OUT)
	@$(RUN) $(GCC_IMAGE):$(GCC13_TAG) make --no-print-directory -f makefile.gcc \
		OUTDIR=/out/proof-on EXTRA_CFLAGS="-foptimize-sibling-calls" >/dev/null
	@$(RUN) $(GCC_IMAGE):$(GCC13_TAG) make --no-print-directory -f makefile.gcc \
		OUTDIR=/out/proof-off EXTRA_CFLAGS="-fno-optimize-sibling-calls" >/dev/null
	@for m in on off; do \
		echo ""; echo "=== gcc 13.4, sibling calls $$m ==="; \
		$(RUN) $(GCC_IMAGE):$(GCC13_TAG) m68k-amigaos-objdump -d /out/proof-$$m/obj/000/disk.o \
		| awk '/_RawRead:/{p=1} p{print; if(++n>12) exit}'; \
	done
	@echo ""
	@echo "'move.l d1,d2' loads blocknr for the callee. Absent in the 'on' listing."

# Installs every toolchain side by side as pfs3aio.<toolchain>, every build, so
# variants can be compared on hardware without rebuilding between flashes. The
# ROM builder config names the toolchain it wants.
INSTALL_TOOLCHAINS ?= $(TOOLCHAINS)

# The release set, one directory per target CPU like install. Git ignores it;
# release-table.py turns it into the assets of a GitHub release.
#
# Publish from a tagged commit: the binaries name whatever PFS_REF resolves to,
# so a set built off a tag says [jpu/<tag>] and one built anywhere else says
# [jpu/<hash>].
DIST_DIR ?= dist

install: $(INSTALL_TOOLCHAINS)
	@for cpu in $(TIERS); do \
		mkdir -p "$(INSTALL_DIR)/$$cpu"; \
		for tc in $(INSTALL_TOOLCHAINS); do \
			case " $(SKIPPED) " in *" $$tc:$$cpu "*) continue;; esac; \
			src="$(OUT)/$$tc/$$cpu/pfs3aio"; \
			dst="$(INSTALL_DIR)/$$cpu/pfs3aio.$$tc"; \
			[ -f "$$src" ] || { printf "  %-8s %-6s -> skipped, see TIER_SKIP in tiers.mk\n" "$$tc" "$$cpu"; continue; }; \
			install -m 644 "$$src" "$$dst" || exit 1; \
			printf "  %-8s %-6s -> %s (%s bytes)\n" "$$tc" "$$cpu" "$$dst" "$$(stat -c%s "$$dst")"; \
		done; \
	done

# Only the release tiers, whatever TIERS or EXTRA say.
dist: override TIERS = $(RELEASE_TIERS)
dist: $(INSTALL_TOOLCHAINS)
	@for cpu in $(TIERS); do \
		mkdir -p "$(DIST_DIR)/$$cpu"; \
		for tc in $(INSTALL_TOOLCHAINS); do \
			case " $(SKIPPED) " in *" $$tc:$$cpu "*) continue;; esac; \
			src="$(OUT)/$$tc/$$cpu/pfs3aio"; \
			[ -f "$$src" ] || continue; \
			dst="$(DIST_DIR)/$$cpu/pfs3aio.$$tc"; \
			install -m 644 "$$src" "$$dst" || exit 1; \
			printf "  %-8s %-6s -> %s (%s bytes)\n" "$$tc" "$$cpu" "$$dst" "$$(stat -c%s "$$dst")"; \
		done; \
	done

clean:
	rm -rf $(OUT)
