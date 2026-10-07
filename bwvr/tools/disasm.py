#!/usr/bin/env python3
"""Disassemble a range of runblack.exe with decomp symbol names attached.

This is the fast reverse engineering loop. Ghidra is better for exploring,
but a full analysis pass on a 4.88 MB .text takes tens of minutes, and most
questions here are "what does this one function do". Capstone answers that
in under a second.

Usage:
    python disasm.py 0x0082F810              # one function, size from symbols
    python disasm.py 0x0082F810 0x110        # explicit length
    python disasm.py --find LH3DRender       # list matching symbols
    python disasm.py --xref 0x00EC8010       # who touches this address

All addresses are virtual addresses at image base 0x00400000.
"""

import argparse
import os
import re
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("capstone is required: pip install capstone")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "Black & White Game", "runblack.exe")
SYMBOLS = os.path.join(ROOT, "third_party", "bw1-decomp", "config", "BW1W120",
                       "symbols.txt")


class Image:
    """A loaded PE, with virtual address to file offset mapping."""

    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = handle.read()

        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError("not a PE file")

        coff = e_lfanew + 4
        section_count = struct.unpack_from("<H", self.data, coff + 2)[0]
        opt_size = struct.unpack_from("<H", self.data, coff + 16)[0]
        opt = coff + 20
        self.image_base = struct.unpack_from("<I", self.data, opt + 28)[0]

        self.sections = []
        table = opt + opt_size
        for index in range(section_count):
            entry = table + index * 40
            name = self.data[entry:entry + 8].rstrip(b"\0").decode("latin-1")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", self.data,
                                                               entry + 8)
            self.sections.append({
                "name": name,
                "va": self.image_base + vaddr,
                "vsize": vsize,
                "raw": rawptr,
                "rawsize": rawsize,
            })

    def section_of(self, va):
        for section in self.sections:
            if section["va"] <= va < section["va"] + max(section["vsize"],
                                                         section["rawsize"]):
                return section
        return None

    def offset(self, va):
        section = self.section_of(va)
        if section is None:
            return None
        delta = va - section["va"]
        if delta >= section["rawsize"]:
            return None
        return section["raw"] + delta

    def read(self, va, length):
        start = self.offset(va)
        if start is None:
            return None
        return self.data[start:start + length]

    def dword(self, va):
        raw = self.read(va, 4)
        if raw is None or len(raw) < 4:
            return None
        return struct.unpack("<I", raw)[0]

    def float_at(self, va):
        raw = self.read(va, 4)
        if raw is None or len(raw) < 4:
            return None
        return struct.unpack("<f", raw)[0]


SYMBOL_LINE = re.compile(
    r"^(?P<name>\S+)\s*=\s*\.(?P<section>\w+):0x(?P<addr>[0-9A-Fa-f]+);"
    r"(?:\s*//\s*(?P<comment>.*))?$")


# Pairs of addresses whose names the bw1-decomp symbol file has swapped.
SWAPPED_NAMES = [
    (0x005B3C70, 0x005B5E70),  # HandStateHolding::Update, ObtainRequiredHandPosition
]


def load_symbols(path):
    """Return {address: (name, size)} from a bw1-decomp symbols file."""
    table = {}
    if not os.path.exists(path):
        return table
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = SYMBOL_LINE.match(line.strip())
            if match is None:
                continue
            addr = int(match.group("addr"), 16)
            name = match.group("name")
            size = 0
            comment = match.group("comment") or ""
            size_match = re.search(r"size:0x([0-9A-Fa-f]+)", comment)
            if size_match:
                size = int(size_match.group(1), 16)
            table[addr] = (name, size)
    # The symbol file swaps these two names. The vtable and the call sites
    # prove it, see docs/hand.md, "Two symbol names are swapped". Keep each
    # size with its address, and swap only the names.
    for first, second in SWAPPED_NAMES:
        if first in table and second in table:
            first_name, first_size = table[first]
            second_name, second_size = table[second]
            table[first] = (second_name, first_size)
            table[second] = (first_name, second_size)
    return table


def demangle(name):
    """Turn a few common MSVC decorations into something readable.

    This is not a full demangler. It only pulls out the class and method,
    which is all that a disassembly listing needs.
    """
    if not name.startswith("?"):
        return name
    body = name[1:]
    parts = body.split("@@")[0].split("@")
    parts = [p for p in parts if p]
    if not parts:
        return name
    parts.reverse()
    return "::".join(parts)


def label_for(addr, symbols):
    entry = symbols.get(addr)
    if entry is None:
        return None
    return demangle(entry[0])


def nearest(addr, symbols, sorted_addrs):
    """Name the symbol that contains addr, as name+delta."""
    import bisect
    index = bisect.bisect_right(sorted_addrs, addr) - 1
    if index < 0:
        return None
    base = sorted_addrs[index]
    name, size = symbols[base]
    if size and addr >= base + size:
        return None
    if addr == base:
        return demangle(name)
    return "%s+0x%X" % (demangle(name), addr - base)


def annotate(image, symbols, sorted_addrs, insn):
    """Build a trailing comment for one instruction."""
    notes = []

    # Direct call or jump to a named function.
    if insn.mnemonic in ("call", "jmp") or insn.mnemonic.startswith("j"):
        operand = insn.op_str.strip()
        if operand.startswith("0x"):
            target = int(operand, 16)
            name = nearest(target, symbols, sorted_addrs)
            if name:
                notes.append("-> %s" % name)

    # Any absolute address in the operand that lands in a known section.
    for literal in re.findall(r"0x[0-9a-f]{6,8}", insn.op_str):
        value = int(literal, 16)
        if value == 0:
            continue
        section = image.section_of(value)
        if section is None:
            continue
        name = label_for(value, symbols)
        if name:
            notes.append("%s = %s" % (literal, name))
        elif section["name"] in (".rdata", ".data", ".data1"):
            word = image.dword(value)
            fvalue = image.float_at(value)
            detail = "%s [%s]" % (literal, section["name"])
            if word is not None:
                detail += " dword=0x%08X" % word
                if fvalue is not None and (abs(fvalue) > 1e-6
                                           and abs(fvalue) < 1e9):
                    detail += " float=%g" % fvalue
            notes.append(detail)

    return "  ; " + "  ".join(notes) if notes else ""


def disassemble(image, symbols, start, length):
    sorted_addrs = sorted(symbols)
    code = image.read(start, length)
    if code is None:
        sys.exit("address 0x%08X is not mapped" % start)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False

    header = nearest(start, symbols, sorted_addrs) or "unnamed"
    print("=" * 78)
    print("0x%08X  %s   (%d bytes)" % (start, header, length))
    print("=" * 78)

    for insn in md.disasm(code, start):
        label = label_for(insn.address, symbols)
        if label and insn.address != start:
            print()
            print("%s:" % label)
        comment = annotate(image, symbols, sorted_addrs, insn)
        print("  0x%08X  %-22s %-32s%s" % (
            insn.address, insn.bytes.hex(), "%s %s" % (insn.mnemonic, insn.op_str),
            comment))


def find_symbols(symbols, pattern):
    regex = re.compile(pattern, re.IGNORECASE)
    hits = []
    for addr, (name, size) in symbols.items():
        pretty = demangle(name)
        if regex.search(name) or regex.search(pretty):
            hits.append((addr, pretty, size, name))
    for addr, pretty, size, raw in sorted(hits):
        print("0x%08X  size 0x%-6X  %s" % (addr, size, pretty))
        if pretty != raw:
            print("                        %s" % raw)
    print("\n%d match(es)" % len(hits))


def find_callers(image, symbols, target):
    """Find every `call` and `jmp` that reaches target by relative offset.

    find_xrefs only sees absolute addresses, so it misses ordinary calls.
    E8 and E9 encode a signed displacement from the end of the instruction,
    which is how nearly every call in this binary is written.
    """
    sorted_addrs = sorted(symbols)
    hits = []
    for section in image.sections:
        if section["name"] != ".text":
            continue
        blob = image.data[section["raw"]:section["raw"] + section["rawsize"]]
        for index in range(len(blob) - 5):
            opcode = blob[index]
            if opcode not in (0xE8, 0xE9):
                continue
            rel = struct.unpack_from("<i", blob, index + 1)[0]
            site = section["va"] + index
            if site + 5 + rel == target:
                hits.append((site, "call" if opcode == 0xE8 else "jmp"))

    print("%d site(s) reach 0x%08X" % (len(hits), target))
    by_owner = {}
    for site, kind in hits:
        owner = nearest(site, symbols, sorted_addrs) or "?"
        by_owner.setdefault(owner.split("+")[0], []).append((site, kind))
    for owner in sorted(by_owner):
        places = by_owner[owner]
        detail = ", ".join("0x%08X %s" % (a, k) for a, k in places[:4])
        if len(places) > 4:
            detail += ", and %d more" % (len(places) - 4)
        print("  %-44s %s" % (owner, detail))


def find_xrefs(image, symbols, target):
    """Scan .text for any 4 byte little endian occurrence of target."""
    sorted_addrs = sorted(symbols)
    needle = struct.pack("<I", target)
    hits = []
    for section in image.sections:
        if section["name"] not in (".text", ".rdata", ".data"):
            continue
        blob = image.data[section["raw"]:section["raw"] + section["rawsize"]]
        pos = blob.find(needle)
        while pos != -1:
            va = section["va"] + pos
            hits.append((va, section["name"]))
            pos = blob.find(needle, pos + 1)

    print("references to 0x%08X: %d" % (target, len(hits)))
    for va, section_name in hits:
        owner = nearest(va, symbols, sorted_addrs) if section_name == ".text" else None
        print("  0x%08X  [%s]%s" % (va, section_name,
                                    "  in %s" % owner if owner else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address", nargs="?", help="virtual address, e.g. 0x0082F810")
    parser.add_argument("length", nargs="?", help="byte count, default from symbols")
    parser.add_argument("--find", help="list symbols matching this regex")
    parser.add_argument("--xref", help="find references to this address")
    parser.add_argument("--callers", help="find calls and jumps to this address")
    parser.add_argument("--exe", default=EXE)
    parser.add_argument("--symbols", default=SYMBOLS)
    args = parser.parse_args()

    image = Image(args.exe)
    symbols = load_symbols(args.symbols)

    if args.find:
        find_symbols(symbols, args.find)
        return
    if args.xref:
        find_xrefs(image, symbols, int(args.xref, 16))
        return
    if args.callers:
        find_callers(image, symbols, int(args.callers, 16))
        return
    if not args.address:
        parser.error("give an address, --find, --xref or --callers")

    start = int(args.address, 16)
    if args.length:
        length = int(args.length, 16 if args.length.startswith("0x") else 10)
    else:
        entry = symbols.get(start)
        length = entry[1] if entry and entry[1] else 0x80

    disassemble(image, symbols, start, length)


if __name__ == "__main__":
    main()
