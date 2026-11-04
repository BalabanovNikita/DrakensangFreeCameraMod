"""Helpers for analysing the in-memory dump of drakensang.exe (_mod/dump/drakensang_mem.bin).

The dump is the image as mapped at 0x400000, so file offset = VA - 0x400000.
"""
import os
import re
import struct
import sys

import capstone

BASE = 0x400000
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from game_paths import D1_DIR, dump_file  # noqa: E402

DUMP = os.environ.get('DUMP') or dump_file(D1_DIR, 'drakensang_mem.bin')

_md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_md.detail = False


class Img:
    def __init__(self, path=DUMP):
        if not os.path.exists(path):
            sys.exit('dump not found (%r): set DUMP, or run the game once with [dev] dump_image=1' % path)
        self.d = open(path, 'rb').read()
        e_lfanew = struct.unpack_from('<I', self.d, 0x3C)[0]
        nsec = struct.unpack_from('<H', self.d, e_lfanew + 6)[0]
        optsz = struct.unpack_from('<H', self.d, e_lfanew + 20)[0]
        sec = e_lfanew + 24 + optsz
        self.sections = {}
        for i in range(nsec):
            name = self.d[sec + i * 40: sec + i * 40 + 8].rstrip(b'\0').decode()
            vsize, va = struct.unpack_from('<II', self.d, sec + i * 40 + 8)
            self.sections[name] = (BASE + va, BASE + va + vsize)
        self.text = self.sections['.text']
        self._xref_cache = None

    # ---- raw access --------------------------------------------------------
    def off(self, va):
        return va - BASE

    def u8(self, va):
        return self.d[va - BASE]

    def u32(self, va):
        return struct.unpack_from('<I', self.d, va - BASE)[0]

    def f32(self, va):
        return struct.unpack_from('<f', self.d, va - BASE)[0]

    def bytes(self, va, n):
        return self.d[va - BASE: va - BASE + n]

    def cstr(self, va, maxlen=256):
        o = va - BASE
        e = self.d.find(b'\0', o, o + maxlen)
        return self.d[o:e if e >= 0 else o + maxlen].decode('latin1')

    def in_text(self, va):
        return self.text[0] <= va < self.text[1]

    # ---- strings -----------------------------------------------------------
    def find_str(self, s):
        """VAs of exact NUL-terminated string s (preceded by NUL or alignment)."""
        b = s.encode('latin1') + b'\0'
        res = []
        for m in re.finditer(re.escape(b), self.d):
            o = m.start()
            if o == 0 or self.d[o - 1] == 0:
                res.append(BASE + o)
        return res

    def find_bytes(self, b, start=None, end=None):
        lo = (start or BASE) - BASE
        hi = (end or BASE + len(self.d)) - BASE
        res = []
        i = self.d.find(b, lo, hi)
        while i >= 0:
            res.append(BASE + i)
            i = self.d.find(b, i + 1, hi)
        return res

    # ---- code --------------------------------------------------------------
    def dis(self, va, n=30, size=None):
        code = self.bytes(va, size or n * 15)
        out = []
        for ins in _md.disasm(code, va):
            out.append(ins)
            if size is None and len(out) >= n:
                break
        return out

    def pdis(self, va, n=30, size=None, mark=None):
        for ins in self.dis(va, n, size):
            m = '>>' if mark is not None and ins.address == mark else '  '
            print('%s %08x  %-22s %s %s' % (m, ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))

    def imm_refs(self, value):
        """Locations in .text where the 4-byte little-endian value occurs (candidate xrefs)."""
        return [a for a in self.find_bytes(struct.pack('<I', value), *self.text)]

    def xrefs(self, value):
        """Instructions in .text that reference `value` as an immediate/displacement."""
        res = []
        for a in self.imm_refs(value):
            # try to decode an instruction that contains the operand bytes at a
            for back in range(1, 8):
                ins = self.dis(a - back, 1)
                if ins and ins[0].address + ins[0].size > a + 3 and ins[0].address <= a - 1:
                    if '%x' % value in ins[0].op_str:
                        res.append(ins[0])
                        break
        return res

    def func_start(self, va, limit=0x4000):
        """Heuristic: walk back to the byte after int3/nop padding or a ret."""
        o = va - BASE
        lo = max(self.text[0] - BASE, o - limit)
        i = o
        while i > lo:
            if self.d[i - 1] == 0xCC and self.d[i] != 0xCC:
                return BASE + i
            i -= 1
        return None

    def call_targets(self, va, n=200):
        res = []
        for ins in self.dis(va, n):
            if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
                res.append((ins.address, int(ins.op_str, 16)))
            if ins.mnemonic == 'ret':
                break
        return res

    def callers(self, target):
        """All direct `call rel32` / `jmp rel32` sites targeting `target` in .text."""
        res = []
        t0, t1 = self.text
        d = self.d
        o0, o1 = t0 - BASE, t1 - BASE - 5
        # scan for E8/E9 opcodes; compute target
        for op in (b'\xe8', b'\xe9'):
            i = d.find(op, o0, o1)
            while i >= 0:
                rel = struct.unpack_from('<i', d, i + 1)[0]
                if BASE + i + 5 + rel == target:
                    res.append(BASE + i)
                i = d.find(op, i + 1, o1)
        return sorted(res)


def h(x):
    return '%08x' % x
