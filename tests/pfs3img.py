#!/usr/bin/env python3
"""Read the directory structure of a PFS3 image, for the black-box tests.

    python3 tests/pfs3img.py IMAGE [PATH]

Finds the first partition of the RDB, reads the rootblock, the reserved
bitmap and the anodes, and lists the dirblock chain of PATH (default the root
directory): for each block its anode, its location and its number of entries.
Only the layouts the tests format are read: no supermode (MODE_SUPERINDEX).
"""
import struct
import sys

ROOTBLOCK = 2
ANODE_ROOTDIR = 5
MODE_SPLITTED_ANODES = 2
MODE_SUPERINDEX = 128
ID_DB = 0x4442


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.part = self._partition()
        self.secsize = self.part[1]
        self.base = self.part[0]
        rb = self.sectors(ROOTBLOCK, 1)
        self.options = u32(rb, 4)
        self.lastreserved = u32(rb, 52)
        self.firstreserved = u32(rb, 56)
        self.reserved_free = u32(rb, 60)
        self.resblksize = u16(rb, 64)
        self.indexblocks = [u32(rb, 116 + 4 * i) for i in range(99)]
        if self.options & MODE_SUPERINDEX:
            raise ValueError("supermode is not supported")
        self.split = bool(self.options & MODE_SPLITTED_ANODES)
        self.rescluster = self.resblksize // self.secsize
        self.anodesperblock = (self.resblksize - 16) // 12
        self.indexperblock = (self.resblksize - 12) // 4

    def close(self):
        self.f.close()

    def _partition(self):
        """(byte offset, sector size) of the first partition."""
        for blk in range(16):
            self.f.seek(blk * 512)
            b = self.f.read(512)
            if b[:4] == b"RDSK":
                break
        else:
            raise ValueError("no RDB")
        part = u32(b, 28)
        self.f.seek(part * 512)
        p = self.f.read(512)
        if p[:4] != b"PART":
            raise ValueError("no PART block")
        env = [u32(p, 128 + 4 * i) for i in range(17)]
        sizeblock, surfaces, bpt, lowcyl = env[1], env[3], env[5], env[9]
        secsize = sizeblock * 4
        return lowcyl * surfaces * bpt * secsize, secsize

    def sectors(self, nr, n):
        self.f.seek(self.base + nr * self.secsize)
        return self.f.read(n * self.secsize)

    def reserved(self, nr):
        return self.sectors(nr, self.rescluster)

    def anode(self, nr):
        """(clustersize, blocknr, next) of anode nr."""
        if self.split:
            seqnr, offset = nr >> 16, nr & 0xffff
        else:
            seqnr, offset = divmod(nr, self.anodesperblock)
        idx, pos = divmod(seqnr, self.indexperblock)
        ib = self.reserved(self.indexblocks[idx])
        ab = self.reserved(u32(ib, 12 + 4 * pos))
        if u32(ab, 8) != seqnr:
            raise ValueError("anode block %d has seqnr %d" % (seqnr, u32(ab, 8)))
        o = 16 + 12 * offset
        return u32(ab, o), u32(ab, o + 4), u32(ab, o + 8)

    def entries(self, blk):
        """(name, type, anode) of the entries of a dirblock."""
        out, o = [], 20
        while o < len(blk) and blk[o]:
            size, typ = blk[o], struct.unpack_from(">b", blk, o + 1)[0]
            an = u32(blk, o + 2)
            nl = blk[o + 17]
            out.append((blk[o + 18:o + 18 + nl].decode("latin-1"), typ, an))
            o += size
        return out

    def chain(self, head):
        """[(anode, blocknr, entries)] of the dirblocks of directory head."""
        out, nr, seen = [], head, set()
        while nr:
            if nr in seen:
                raise ValueError("anode chain loops at %d" % nr)
            seen.add(nr)
            _, blocknr, nxt = self.anode(nr)
            blk = self.reserved(blocknr)
            if u16(blk, 0) != ID_DB or u32(blk, 12) != head:
                raise ValueError("block %d of directory %d is not its dirblock" % (blocknr, head))
            out.append((nr, blocknr, self.entries(blk)))
            nr = nxt
        return out

    def lookup(self, path):
        """Head anode of the directory path."""
        head = ANODE_ROOTDIR
        for part in [p for p in path.split("/") if p]:
            for _, _, ents in self.chain(head):
                hit = [an for name, typ, an in ents if name.lower() == part.lower() and typ > 0]
                if hit:
                    head = hit[0]
                    break
            else:
                raise KeyError(path)
        return head

    def is_free(self, blocknr):
        """Reserved block blocknr is free in the reserved bitmap."""
        t = (blocknr - self.firstreserved) // self.rescluster
        bm = self.sectors(ROOTBLOCK, (512 + 12 + t // 32 * 4 + 4 + self.secsize - 1) // self.secsize)
        return bool(u32(bm, 512 + 12 + t // 32 * 4) >> (31 - t % 32) & 1)

    def bitmap_free(self):
        """Free reserved blocks counted in the reserved bitmap (bit set = free)."""
        n = (self.lastreserved - self.firstreserved) // self.rescluster + 1
        bm = self.sectors(ROOTBLOCK, (512 + 12 + (n + 31) // 32 * 4 + self.secsize - 1) // self.secsize + 1)
        words = struct.unpack_from(">%dI" % ((n + 31) // 32), bm, 512 + 12)
        free = sum(bin(w).count("1") for w in words)
        extra = (n + 31) // 32 * 32 - n
        return free - bin(words[-1] & ((1 << extra) - 1)).count("1") if extra else free


def main():
    img = Image(sys.argv[1])
    path = sys.argv[2] if len(sys.argv) > 2 else "/"
    ch = img.chain(img.lookup(path))
    for an, blocknr, ents in ch:
        print("anode %6d block %8d entries %3d" % (an, blocknr, len(ents)))
    print("%d blocks, %d empty; reserved free %d (bitmap %d)" % (
        len(ch), sum(1 for c in ch if not c[2]), img.reserved_free, img.bitmap_free()))


if __name__ == "__main__":
    main()
