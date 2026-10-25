#!/usr/bin/env python3
"""flexdisk: list, read and write files on a FLEX disk image (for the tests).

usage: flexdisk.py IMAGE list
       flexdisk.py IMAGE get NAME.EXT OUTFILE      (raw sector data, 252 bytes each)
       flexdisk.py IMAGE gettext NAME.EXT OUTFILE  (a text file, CR -> newline)
       flexdisk.py IMAGE put INFILE NAME.EXT       (replaces an existing file)
       flexdisk.py IMAGE puttext INFILE NAME.EXT   (newline -> CR)
       flexdisk.py IMAGE delete NAME.EXT
       flexdisk.py IMAGE patch NAME.EXT|@T/S ADDR OLDHEX NEWHEX
            (in a binary file, or the chain of sectors starting at track T
             sector S, change the bytes that load at ADDR, in place, if they
             are OLDHEX)

The image is a plain sector dump (track 0 sector 1 first, 256-byte
sectors), optionally behind a VDK header ("dk" ...), as XRoar uses for
Dragon disks; the geometry comes from the System Information Record.
Layout, from the FLEX Advanced Programmer's Guide: track 0 holds the boot
sectors, the System Information Record (sector 3) and the directory (from
sector 5); every other sector starts with a link to the next one (track,
sector) and a 16-bit record number, then 252 bytes of data.  Free sectors
form a chain that the SIR points at.
"""
import sys

SECSIZE = 256
SIR = (0, 3)
DIR = (0, 5)


class Disk:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.raw = bytearray(f.read())
        self.off = 0
        if self.raw[:2] == b'dk':                   # VDK header
            self.off = self.raw[2] | self.raw[3] << 8
        self.max_track = self.max_sector = 255      # until the SIR is read
        sir = self.sector(*SIR)                     # (on track 0: any geometry)
        self.max_track = sir[0x26]
        self.max_sector = sir[0x27]
        if not self.max_sector:
            sys.exit('flexdisk: %s: no FLEX System Information Record' % path)

    def pos(self, t, s):
        if not (1 <= s <= self.max_sector and t <= self.max_track):
            sys.exit('flexdisk: bad disk address %d/%d' % (t, s))
        return self.off + (t * self.max_sector + s - 1) * SECSIZE

    def sector(self, t, s):
        p = self.pos(t, s)
        return self.raw[p:p + SECSIZE]

    def write(self, t, s, data):
        p = self.pos(t, s)
        self.raw[p:p + SECSIZE] = data

    def save(self):
        with open(self.path, 'wb') as f:
            f.write(self.raw)

    def entries(self):
        """(sector address, offset, entry bytes) for every directory slot"""
        t, s = DIR
        while (t, s) != (0, 0):
            sec = self.sector(t, s)
            for i in range(10):
                o = 16 + 24 * i
                yield (t, s), o, sec[o:o + 24]
            t, s = sec[0], sec[1]

    def find(self, name):
        for addr, o, e in self.entries():
            if e[0] and not e[0] & 0x80 and entry_name(e) == name:
                return addr, o, e
        return None

    def chain(self, t, s):
        while (t, s) != (0, 0):
            sec = self.sector(t, s)
            yield (t, s), sec
            t, s = sec[0], sec[1]

    def read(self, name):
        found = self.find(name)
        if not found:
            sys.exit('flexdisk: %s: no file %s' % (self.path, name))
        e = found[2]
        return b''.join(bytes(sec[4:]) for _, sec in self.chain(e[13], e[14]))

    def patch(self, name, addr, old, new):
        """change the bytes of binary file `name` that load at addr..,
        in place (the file keeps its sectors)"""
        if name.startswith('@'):
            start = tuple(int(v) for v in name[1:].split('/'))
        else:
            found = self.find(name)
            if not found:
                sys.exit('flexdisk: %s: no file %s' % (self.path, name))
            start = (found[2][13], found[2][14])
        # (sector address, offset in sector) of every data byte, in order
        where = [(ts, k) for ts, sec in self.chain(*start) for k in range(4, 256)]
        data = bytes(self.sector(t, s)[k] for (t, s), k in where)
        targets = {}                    # load address -> index into data
        i = 0
        while i < len(data):
            if data[i] == 0x02 and i + 4 <= len(data):
                a, n = data[i + 1] << 8 | data[i + 2], data[i + 3]
                for j in range(n):
                    targets[(a + j) & 0xffff] = i + 4 + j
                i += 4 + n
            elif data[i] == 0x16:
                i += 3
            else:
                i += 1
        for j, (o, b) in enumerate(zip(old, new)):
            idx = targets.get((addr + j) & 0xffff)
            if idx is None or data[idx] != o:
                sys.exit('flexdisk: %s: byte at %04X is not %02X' % (name, addr + j, o))
            (t, s), k = where[idx]
            sec = bytearray(self.sector(t, s))
            sec[k] = b
            self.write(t, s, sec)

    def free_chain(self, sir):
        return sir[0x1d], sir[0x1e]

    def delete(self, name):
        found = self.find(name)
        if not found:
            return False
        (dt, ds), o, e = found
        sir = bytearray(self.sector(*SIR))
        first, last = (e[13], e[14]), (e[15], e[16])
        count = e[17] << 8 | e[18]
        # the file's sectors go on the end of the free chain
        if sir[0x1f] or sir[0x20]:
            ls = bytearray(self.sector(sir[0x1f], sir[0x20]))
            ls[0], ls[1] = first
            self.write(sir[0x1f], sir[0x20], ls)
        else:
            sir[0x1d], sir[0x1e] = first
        sir[0x1f], sir[0x20] = last
        free = (sir[0x21] << 8 | sir[0x22]) + count
        sir[0x21], sir[0x22] = free >> 8, free & 0xff
        self.write(*SIR, sir)
        d = bytearray(self.sector(dt, ds))
        d[o] |= 0x80
        self.write(dt, ds, d)
        return True

    def put(self, name, data):
        self.delete(name)
        chunks = [data[i:i + 252] for i in range(0, len(data), 252)] or [b'']
        sir = bytearray(self.sector(*SIR))
        free = sir[0x21] << 8 | sir[0x22]
        if len(chunks) > free:
            sys.exit('flexdisk: %s: disk full' % self.path)
        slot = next(((a, o) for a, o, e in self.entries() if e[0] == 0 or e[0] & 0x80), None)
        if not slot:
            sys.exit('flexdisk: %s: directory full' % self.path)
        t, s = sir[0x1d], sir[0x1e]
        first = (t, s)
        for rec, chunk in enumerate(chunks, 1):
            sec = bytearray(self.sector(t, s))
            nxt = (sec[0], sec[1])
            sec[2], sec[3] = rec >> 8, rec & 0xff
            sec[4:] = chunk.ljust(252, b'\0')
            if rec == len(chunks):
                last = (t, s)
                sec[0] = sec[1] = 0
            self.write(t, s, sec)
            t, s = nxt
        sir[0x1d], sir[0x1e] = t, s
        if (t, s) == (0, 0):
            sir[0x1f] = sir[0x20] = 0
        free -= len(chunks)
        sir[0x21], sir[0x22] = free >> 8, free & 0xff
        self.write(*SIR, sir)
        (dt, ds), o = slot
        d = bytearray(self.sector(dt, ds))
        base, ext = (name.split('.') + [''])[:2]
        e = bytearray(24)
        e[0:8] = base.encode().ljust(8, b'\0')
        e[8:11] = ext.encode().ljust(3, b'\0')
        e[13], e[14] = first
        e[15], e[16] = last
        e[17], e[18] = len(chunks) >> 8, len(chunks) & 0xff
        e[21:24] = sir[0x23:0x26]                   # the disk's date
        d[o:o + 24] = e
        self.write(dt, ds, d)


def entry_name(e):
    base = bytes(e[0:8]).rstrip(b'\0').decode('latin-1')
    ext = bytes(e[8:11]).rstrip(b'\0').decode('latin-1')
    return base + ('.' + ext if ext else '')


def from_text(data):
    return data.replace(b'\r\n', b'\n').replace(b'\n', b'\r')


def to_text(data):
    """FLEX text: CR ends lines, TAB n stands for n spaces, NUL pads"""
    out, i = bytearray(), 0
    while i < len(data):
        c = data[i]
        if c == 0x09 and i + 1 < len(data):
            out += b' ' * data[i + 1]
            i += 2
            continue
        if c == 0x0d:
            out += b'\n'
        elif c not in (0x00, 0x0a):
            out.append(c)
        i += 1
    return bytes(out)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    d = Disk(argv[1])
    cmd = argv[2]
    if cmd == 'list':
        sir = d.sector(*SIR)
        print('label %s, %d tracks x %d sectors, %d free' % (
            bytes(sir[0x10:0x1b]).rstrip(b'\0').decode('latin-1'), d.max_track + 1,
            d.max_sector, sir[0x21] << 8 | sir[0x22]))
        for _, _, e in d.entries():
            if e[0] and not e[0] & 0x80:
                print('%-12s %4d sectors' % (entry_name(e), e[17] << 8 | e[18]))
    elif cmd in ('get', 'gettext') and len(argv) == 5:
        data = d.read(argv[3].upper())
        with open(argv[4], 'wb') as f:
            f.write(to_text(data) if cmd == 'gettext' else data)
    elif cmd in ('put', 'puttext') and len(argv) == 5:
        with open(argv[3], 'rb') as f:
            data = f.read()
        d.put(argv[4].upper(), from_text(data) if cmd == 'puttext' else data)
        d.save()
    elif cmd == 'patch' and len(argv) == 7:
        old, new = bytes.fromhex(argv[5]), bytes.fromhex(argv[6])
        if len(old) != len(new):
            sys.exit('flexdisk: OLDHEX and NEWHEX must be the same length')
        d.patch(argv[3].upper(), int(argv[4], 16), old, new)
        d.save()
    elif cmd == 'delete' and len(argv) == 4:
        if not d.delete(argv[3].upper()):
            sys.exit('flexdisk: no file %s' % argv[3])
        d.save()
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
