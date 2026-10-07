# Import bw1-decomp symbols into Ghidra.
#
# Reads config/BW1W120/symbols.txt from the openblack/bw1-decomp repository and
# applies every symbol to the open program. Ghidra's MSVC demangler turns the
# mangled names into readable C++ signatures afterwards.
#
# The installed v1.42 runblack.exe is the decrypted v1.20 image at identical
# addresses, so BW1W120 symbols land correctly. The script verifies this with
# known anchors before it writes anything.
#
# @category BlackAndWhiteVR
# @runtime PyGhidra

import re

from ghidra.program.model.symbol import SourceType

SYMBOL_LINE = re.compile(
    r"^\s*(?P<name>\S+)\s*=\s*\.(?P<section>\w+):0x(?P<addr>[0-9A-Fa-f]+)\s*;"
    r"(?:\s*//\s*(?P<comment>.*))?$"
)

# Anchors verified independently with an RTTI walk. If these do not match, the
# symbol file is for a different build and must not be applied.
ANCHORS = {
    0x008C76E0: "??_7CameraMode@@6B@",
    0x008CBECC: "??_7CHand@@6B@",
    0x00900A54: "??_7HandStateCamera@@6B@",
    0x008DF800: "??_7GestureSystem@@6B@",
}


def parse(path):
    entries = []
    with open(path, "r", errors="replace") as handle:
        for lineno, line in enumerate(handle, 1):
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            match = SYMBOL_LINE.match(line)
            if not match:
                continue
            comment = match.group("comment") or ""
            kind = "function" if "type:function" in comment else "object"
            size = 0
            size_match = re.search(r"size:0x([0-9A-Fa-f]+)", comment)
            if size_match:
                size = int(size_match.group(1), 16)
            entries.append(
                {
                    "name": match.group("name"),
                    "section": match.group("section"),
                    "addr": int(match.group("addr"), 16),
                    "kind": kind,
                    "size": size,
                    "lineno": lineno,
                }
            )
    return entries


def check_anchors(by_addr):
    missing = []
    for addr, expected in ANCHORS.items():
        actual = by_addr.get(addr)
        if actual != expected:
            missing.append((addr, expected, actual))
    return missing


def run():
    path = askFile("bw1-decomp symbols.txt", "Import").getAbsolutePath()
    entries = parse(path)
    if not entries:
        print("No symbols parsed from %s" % path)
        return

    by_addr = {}
    for entry in entries:
        by_addr.setdefault(entry["addr"], entry["name"])

    bad = check_anchors(by_addr)
    if bad:
        print("ABORT: anchor mismatch. This symbol file is for a different build.")
        for addr, expected, actual in bad:
            print("  0x%08X expected %s, got %s" % (addr, expected, actual))
        return
    print("Anchor check passed (%d/%d)." % (len(ANCHORS), len(ANCHORS)))

    space = currentProgram.getAddressFactory().getDefaultAddressSpace()
    symbol_table = currentProgram.getSymbolTable()
    listing = currentProgram.getListing()

    labelled = 0
    functions = 0
    skipped = 0
    total = len(entries)

    monitor.initialize(total)
    monitor.setMessage("Applying %d symbols" % total)

    for index, entry in enumerate(entries):
        if monitor.isCancelled():
            break
        if index % 500 == 0:
            monitor.setProgress(index)

        address = space.getAddress(entry["addr"])
        if currentProgram.getMemory().getBlock(address) is None:
            skipped += 1
            continue

        try:
            symbol_table.createLabel(address, entry["name"], SourceType.IMPORTED)
            labelled += 1
        except Exception as exc:  # name clashes are not fatal
            skipped += 1
            continue

        if entry["kind"] == "function" and listing.getFunctionAt(address) is None:
            try:
                createFunction(address, None)
                functions += 1
            except Exception:
                pass

    print("")
    print("Parsed   : %d symbols" % total)
    print("Labelled : %d" % labelled)
    print("Functions: %d created" % functions)
    print("Skipped  : %d (outside memory or name clash)" % skipped)
    print("")
    print("Now run Analysis > One Shot > Demangler to turn the mangled names")
    print("into readable C++ signatures.")


run()
