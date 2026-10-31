# What goes into the version string, as bare tokens that boot.c turns into
# strings; see TOOLCHAINS.md, "Telling the variants apart".
#
# Included by makefile.gcc. GNUmakefile passes FORK and
# PFS_REF down so the containers never run git: the repo is mounted from the
# host and git inside refuses it as dubious ownership. Outside a checkout the
# shell call comes back empty and the origin bracket is left out, which is also
# what a build from upstream's ./makefile gets.

FORK ?= jpu
# The tag when one points at HEAD, the short hash otherwise. Published sets are
# tagged, so their binaries name the tag. Keep tag names to letters, digits,
# dot, dash and underscore: the value travels as a bare token.
PFS_REF ?= $(shell git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD 2>/dev/null)

ORIGIN_TAG = $(if $(PFS_REF),-DPFS_FORK=$(FORK) -DPFS_REF=$(PFS_REF))

# boot.c is the only object that bakes these in, so it alone is rebuilt when
# they change. The file is rewritten only when its content differs, so a commit
# or a change in working state costs one object, not a full rebuild.
ORIGINSTAMP = $(OUTDIR)/.origin
$(shell mkdir -p $(OUTDIR); \
	echo "$(ORIGIN_TAG)" > $(OUTDIR)/.origin.new; \
	cmp -s $(OUTDIR)/.origin.new $(OUTDIR)/.origin \
		|| mv $(OUTDIR)/.origin.new $(OUTDIR)/.origin; \
	rm -f $(OUTDIR)/.origin.new)

# PFS_TIER is what the build is tuned for, PFS_MIN with PFS_ONLY, PFS_TOP or
# PFS_MAX is where it runs. Predefines cannot express either.
#   PFS_ONLY  one CPU, minimum and maximum the same
#   PFS_TOP   open ended, the maximum is the highest CPU there is
#   PFS_MAX   a closed range
define TIER_TAG_RULE
TIER_TAG_$(1) = -DPFS_TIER=$(1) -DPFS_MIN=$$(patsubst 68%,%,$$(TIER_MINCPU_$(1))) \
	$$(if $$(filter $$(TIER_MINCPU_$(1)),$$(TIER_MAXCPU_$(1))),-DPFS_ONLY=1,\
		$$(if $$(filter 68060,$$(TIER_MAXCPU_$(1))),-DPFS_TOP=1,-DPFS_MAX=$$(patsubst 68%,%,$$(TIER_MAXCPU_$(1))))) \
	$$(ORIGIN_TAG)
endef

$(foreach t,$(TIERS),$(eval $(call TIER_TAG_RULE,$(t))))
