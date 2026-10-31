# Tier metadata for the shell harnesses, read from ./tiers.mk so make and sh
# cannot disagree about what a tier is. tiers.mk is kept in a fixed
# "TIER_<FIELD>_<tier> = value" shape, one word per value, for this parser.
#
# Sourced, not executed:   . "$ROOT/tests/m68k/tiers.sh"

TIERS_MK=${TIERS_MK:-$ROOT/tiers.mk}

tiers() {	# the release tiers, plus the extra ones when EXTRA is set
	sed -n 's/^RELEASE_TIERS[ 	]*=[ 	]*//p' "$TIERS_MK"
	[ -z "${EXTRA:-}" ] || sed -n 's/^EXTRA_TIERS[ 	]*=[ 	]*//p' "$TIERS_MK"
}

tier_get() {	# $1=field $2=tier
	sed -n "s/^TIER_$1_$2[ 	]*=[ 	]*//p" "$TIERS_MK"
}

# Highest CPU Musashi implements, from tiers.mk.
emu_maxcpu() {
	sed -n 's/^EMU_MAXCPU[ 	]*=[ 	]*//p' "$TIERS_MK"
}

tier_gccflag() { tier_get CFLAGS "$1"; }
tier_mincpu() { tier_get MINCPU "$1"; }
tier_maxcpu() { tier_get MAXCPU "$1"; }
tier_large() { tier_get LARGE "$1"; }

# Can this toolchain build this tier? See TIER_SKIP_<tier> in tiers.mk.
tier_builds() {	# $1=tier $2=toolchain
	case " $(tier_get SKIP "$1") " in *" $2 "*) return 1 ;; esac
	return 0
}

# CPU names are five-digit numbers, so a numeric comparison is also the right
# ordering: 68000 < 68020 < 68030 < 68040 < 68060. Tier names are three digits
# and an optional letter, 080a, which is why only MINCPU is compared here.
tier_runs_on() {	# $1=tier $2=emulated cpu
	[ "$(tier_mincpu "$1")" -le "$2" ]
}

tier_emu_cpu() {	# $1=tier $2=default cpu -> the CPU to emulate for this tier
	m=$(tier_mincpu "$1")
	if [ "$m" -gt "$2" ]; then echo "$m"; else echo "$2"; fi
}

# Can any emulated CPU run this tier at all? False for a tier whose minimum is
# above what Musashi implements, which has no column anywhere.
tier_emulatable() {	# $1=tier
	[ "$(tier_mincpu "$1")" -le "$(emu_maxcpu)" ]
}

# The emulated CPU list a harness needs: its own default plus every tier
# minimum, or a tier would have no column it could run in. A minimum above the
# emulator's ceiling is left out, or vamos would be asked for a core it has not
# got.
tier_cpus() {	# $@ = default cpu list
	for t in $(tiers); do
		tier_emulatable "$t" || continue
		m=$(tier_mincpu "$t")
		case " $* " in *" $m "*) ;; *) set -- "$@" "$m" ;; esac
	done
	echo "$@"
}
