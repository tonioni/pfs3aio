#!/usr/bin/env python3
# Stages a GitHub release: renamed assets, the release body and the command
# that creates the draft.
#
#   python3 release-table.py <tag> [--dist DIR] [--out DIR]
#
# Every binary in DIST is copied to OUT/<tag>/ as
# pfs3aio-<tag>-<tier>-<toolchain> (pfs3aio-large-<tag>-... for the large file
# tiers), and the table links to those assets. A
# release is always a full rebuild: every binary must carry [<fork>/<tag>] in
# its own $VER string, so one left over from an earlier build is refused.
# Columns come from the binaries and from tiers.mk, never from a second copy of
# the naming rules.
#
# Nothing is created on GitHub: the gh command is printed, not run.

import argparse
import os
import re
import shutil
import sys

REPO = "pulchart/pfs3aio"
HERE = os.path.dirname(os.path.abspath(__file__))
TIERS_MK = os.environ.get("TIERS_MK", os.path.join(HERE, "tiers.mk"))
NOTES_DIR = os.path.join(HERE, "release-notes")
FFS_NAME_MAX = 30
STAMP_RE = re.compile(r"[A-Za-z0-9._-]+$")

# Column order; a toolchain not listed goes last, alphabetically.
ORDER = ["gcc13", "gcc6", "gcc15", "gcc16", "vbcc"]

LEGEND = "_Green: the builds I use most._"

# One table per volume format: LARGE_FILE_SIZE of the tier in tiers.mk decides.
FORMATS = [
    ("0", "PFS\\1", "Files up to 4 GiB."),
    ("2", "PFS\\2", "Files above 4 GiB. These builds format new volumes as PFS\\2 with the "
                    "large file flag, which the builds above refuse."),
]


def die(msg):
    sys.exit("release-table.py: " + msg)


def tier_field(field, tier):
    pat = re.compile(r"^TIER_%s_%s\s*=\s*(.+?)\s*$" % (field, tier), re.M)
    m = pat.search(open(TIERS_MK).read())
    return m.group(1) if m else None


def is_large(tier):
    return tier_field("LARGE", tier) == "2"


def shown(tier):
    # The l of a large file tier is the PFS\\2 table and the large asset prefix.
    return tier[:-1] if is_large(tier) and tier.endswith("l") else tier


def runs_on(tier, stamp):
    lo, hi = tier_field("MINCPU", tier), tier_field("MAXCPU", tier)
    if not lo or not hi:
        die("tier %s is not in %s" % (tier, TIERS_MK))
    # A top of 68060 means open ended, as -DPFS_TOP does in version.mk: the
    # 68080 runs those builds too.
    if lo == hi:
        out = "%s only" % lo
    elif hi == "68060":
        out = "%s+" % lo
    else:
        out = "%s to %s" % (lo, hi)
    # TIER_NOTE_<tier> separates two builds naming the same CPU. It holds for
    # binaries stamped TIER_NOTE_SINCE_<tier> or later; stamps are dates, so
    # they compare as strings.
    note = tier_field("NOTE", tier)
    since = tier_field("NOTE_SINCE", tier)
    if note and (not since or stamp >= since):
        out = "%s (%s)" % (out, note)
    return out


def picks():
    m = re.search(r"^RELEASE_PICKS\s*=\s*(.*?)\s*$", open(TIERS_MK).read(), re.M)
    out = set(m.group(1).split()) if m else set()
    for p in out:
        if not tier_field("MINCPU", p.split(":")[0]):
            die("RELEASE_PICKS names %s, and that tier is not in %s" % (p, TIERS_MK))
    return out


def brackets(path):
    with open(path, "rb") as f:
        blob = f.read()
    m = re.search(rb"\$VER:[^\x00]*?((?:\[[^\]]*\])+)", blob)
    return m.group(1).decode() if m else None


def cc_of(bracket):
    return bracket[1:].split("/", 1)[0]


def ref_of(bracket):
    # The origin bracket, [<fork>/<tag or commit>], names the release the
    # binary belongs to.
    m = re.search(r"\[[^/\]]+/([^\]]+)\]$", bracket)
    if not m:
        die("no origin bracket in %s" % bracket)
    return m.group(1)


def rows(dist):
    out = []
    for tier in sorted(os.listdir(dist)):
        d = os.path.join(dist, tier)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            path = os.path.join(d, name)
            tag = brackets(path)
            if not tag:
                die("no $VER brackets in %s" % path)
            stamp = ref_of(tag)
            if not STAMP_RE.match(stamp):
                die("%s: stamp %r has characters that break a URL" % (path, stamp))
            if "." not in name:
                die("%s: expected pfs3aio.<toolchain>" % path)
            tc = name.split(".", 1)[1]
            out.append({
                "tier": tier,
                "tc": tc,
                "path": path,
                "cc": cc_of(tag),
                "stamp": stamp,
                "asset": "pfs3aio-%s%s-%s-%s" % ("large-" if is_large(tier) else "", stamp, shown(tier), tc),
            })
    return out


def url(tag, r):
    return "https://github.com/%s/releases/download/%s/%s" % (REPO, tag, r["asset"])


def badge(tag, r):
    color = "green" if "%s:%s" % (r["tier"], r["tc"]) in picks() else "lightgrey"
    img = ("https://img.shields.io/github/downloads/%s/%s/%s"
           "?displayAssetName=false&label=%s&color=%s"
           % (REPO, tag, r["asset"], r["cc"], color))
    return "[![%s](%s)](%s)" % (r["cc"], img, url(tag, r))


def render(tag, rs):
    tcs = sorted({r["tc"] for r in rs},
                 key=lambda t: (ORDER.index(t) if t in ORDER else len(ORDER), t))
    label = {r["tc"]: r["cc"] for r in rs}
    cell = {(r["tier"], r["tc"]): r for r in rs}
    tiers = sorted({r["tier"] for r in rs})

    lines = ["| build | runs on | " + " | ".join(label[t] for t in tcs) + " |",
             "|---|---|" + "---|" * len(tcs)]
    for tier in tiers:
        cells = [badge(tag, cell[(tier, t)]) if (tier, t) in cell else "-" for t in tcs]
        newest = max(r["stamp"] for r in rs if r["tier"] == tier)
        lines.append("| `%s` | %s | %s |" % (shown(tier), runs_on(tier, newest), " | ".join(cells)))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Stage a GitHub release for one tag.")
    ap.add_argument("tag")
    ap.add_argument("--dist", default=os.path.join(HERE, "dist"),
                    help="binaries, laid out <dist>/<tier>/pfs3aio.<tc>")
    ap.add_argument("--out", default=os.path.join(HERE, "release"),
                    help="staging directory, one subdirectory per tag")
    a = ap.parse_args()

    if not os.path.isdir(a.dist):
        die("no such directory: %s" % a.dist)
    news = os.path.join(NOTES_DIR, a.tag + ".md")

    rs = rows(a.dist)
    if not rs:
        die("no binaries in %s" % a.dist)
    for r in rs:
        if r["stamp"] != a.tag:
            die("%s carries [%s], not %s; rebuild everything with make dist PFS_REF=%s"
                % (r["path"], r["stamp"], a.tag, a.tag))
    mine = rs
    names = [r["asset"] for r in rs]
    if len(set(names)) != len(names):
        die("asset names are not unique")

    out = os.path.join(a.out, a.tag)
    os.makedirs(out, exist_ok=True)
    # Only what an earlier run of this script put there.
    for old in os.listdir(out):
        if old == "notes.md" or old.startswith("pfs3aio-"):
            os.remove(os.path.join(out, old))
    for r in mine:
        shutil.copy2(r["path"], os.path.join(out, r["asset"]))

    # "News" only when release-notes/<tag>.md has something to say.
    text = open(news).read().strip() if os.path.isfile(news) else ""
    sections = []
    for large, name, about in FORMATS:
        part = [r for r in rs if (tier_field("LARGE", r["tier"]) or "0") == large]
        if part:
            sections.append("### `%s`\n\n%s\n\n%s\n" % (name, about, render(a.tag, part)))
    body = ("## News\n\n" + text + "\n\n" if text else "") \
        + "## Downloads\n\n" + LEGEND + "\n\n" + "\n".join(sections)
    notes = os.path.join(out, "notes.md")
    with open(notes, "w") as f:
        f.write(body)

    print("%s: %d assets staged in %s" % (a.tag, len(mine), out))
    for r in mine:
        n = os.path.getsize(os.path.join(out, r["asset"]))
        warn = "  (%d chars, FFS limit %d)" % (len(r["asset"]), FFS_NAME_MAX) \
            if len(r["asset"]) > FFS_NAME_MAX else ""
        print("  %-36s %8d bytes%s" % (r["asset"], n, warn))
    print("\ncreate the draft after the tag is pushed:\n")
    print("gh release create %s --repo %s --draft --verify-tag --title %s \\\n  --notes-file %s \\"
          % (a.tag, REPO, a.tag, notes))
    print("  " + " \\\n  ".join(os.path.join(out, r["asset"]) for r in mine))


main()
