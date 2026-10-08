#!/usr/bin/env python3
"""Check the decorated names of the Windows symbol file against the Mac
symbol files of bw1-decomp.

The exe of the game has no symbols. A person or a tool wrote each decorated
Windows name in config/BW1W120/symbols.txt, and Ghidra makes the signature of
a function from that name. The Mac symbol files (config/BW1M*) hold the names
from the compiler, so their parameter lists are the ground truth.

For each Windows function, this script asks the demangler of Windows
(dbghelp, UnDecorateSymbolName) for the parameter list. It takes the class
names in order and compares them with the class names of the Mac symbol of
the same class and method.

The check is for one fault: a class that the name gives as a back-reference
digit ("AAV1@" and not "AAVLHPoint@@"), where the digit is the wrong one. The
name then reads as a different class, and nothing shows it. The script ends
with exit code 1 when it finds such a name that bwvr/ghidra/name-corrections.txt
does not correct. Put the right name in that file, with the proof.

Usage:
    python check_names.py                    # the names with a wrong digit
    python check_names.py --all              # also each other difference, to read
    python check_names.py --no-corrections   # the symbol file alone

Run it after a move to a newer commit of bw1-decomp. It needs Windows.
"""

import collections
import ctypes
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFIG = os.path.join(ROOT, "third_party", "bw1-decomp", "config")
SYMBOLS = os.path.join(CONFIG, "BW1W120", "symbols.txt")
CORRECTIONS = os.path.join(ROOT, "bwvr", "ghidra", "name-corrections.txt")

SYMBOL_LINE = re.compile(
    r"^(?P<name>\S+)\s*=\s*\.(?P<section>\w+):0x(?P<addr>[0-9A-Fa-f]+);"
    r"(?:\s*//\s*(?P<comment>.*))?$")

# A class, struct, union or enum that the name gives as a digit.
BACKREF = re.compile(r"(?:[VUT]|W4)\d@")

# A class in an undecorated parameter list: "class X", or the class of a
# pointer to a member function, "(__thiscall X::*)".
WIN_CLASS = re.compile(
    r"\b(?:class|struct|union|enum)\s+([A-Za-z_][\w:]*)|([A-Za-z_]\w*)::\*\)")


def read_functions(path):
    """Yield (address, name) for each function line of a symbols file."""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = SYMBOL_LINE.match(line.strip())
            if match is None or "type:function" not in (match.group("comment") or ""):
                continue
            yield int(match.group("addr"), 16), match.group("name")


def base(name):
    """Return a class name with no template arguments and no scope."""
    return name.split("<")[0].split("::")[-1].strip()


class Undecorator:
    """The demangler of Windows."""

    def __init__(self):
        self.function = ctypes.WinDLL("dbghelp").UnDecorateSymbolName
        self.function.argtypes = [ctypes.c_char_p, ctypes.c_char_p,
                                  ctypes.c_uint, ctypes.c_uint]
        self.function.restype = ctypes.c_uint

    def __call__(self, name):
        buffer = ctypes.create_string_buffer(8192)
        if not self.function(name.encode("latin-1"), buffer, 8192, 0):
            return None
        text = buffer.value.decode("latin-1")
        return None if text == name else text


def windows_parts(text):
    """Return (class, method, result type, [classes of the parameters])."""
    start = text.find("(")
    if start < 0:
        return None
    depth, end = 0, -1
    for index in range(start, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                end = index
                break
    head = text[:start].split()
    if end < 0 or not head:
        return None
    pieces = [p for p in re.sub(r"<[^<>]*>", "", head[-1]).split("::") if p]
    if not pieces:
        return None
    owner = pieces[-2] if len(pieces) > 1 else None
    classes = [base(m.group(1) or m.group(2))
               for m in WIN_CLASS.finditer(text[start + 1:end])]
    return owner, pieces[-1], " ".join(head[:-1]), classes


def read_length_name(text, index):
    """Read a CodeWarrior name: its length in digits, then the name."""
    end = index
    while end < len(text) and text[end].isdigit():
        end += 1
    count = int(text[index:end])
    return text[end:end + count], end + count


def mac_classes(params):
    """Return the class names of a CodeWarrior parameter list, in order."""
    out, index = [], 0
    while index < len(params):
        char = params[index]
        if char.isdigit():
            name, index = read_length_name(params, index)
            out.append(base(name))
        elif char == "Q" and params[index + 1:index + 2].isdigit():
            count = int(params[index + 1])
            index += 2
            name = ""
            for _ in range(count):
                if not params[index:index + 1].isdigit():
                    break
                name, index = read_length_name(params, index)
            out.append(base(name))
        elif char == "A":
            # An array: "A", the count, "_". The count is not a name length.
            end = index + 1
            while end < len(params) and params[end].isdigit():
                end += 1
            index = end + 1 if params[end:end + 1] == "_" else end
        else:
            index += 1
    return out


def mac_parts(name):
    """Return (class, method, [classes of the parameters]) or None."""
    name = name.lstrip(".")
    for match in re.finditer(r"__", name):
        if match.start() == 0:
            continue
        method, rest = name[:match.start()], name[match.end():]
        owner = None
        try:
            if rest[:1].isdigit():
                owner, index = read_length_name(rest, 0)
                rest = rest[index:]
            elif rest[:1] == "Q" and rest[1:2].isdigit():
                index = 2
                for _ in range(int(rest[1])):
                    owner, index = read_length_name(rest, index)
                rest = rest[index:]
        except ValueError:
            continue
        if rest.startswith("CF"):
            rest = rest[1:]
        if rest.startswith("F"):
            return (base(owner) if owner else None), method, mac_classes(rest[1:])
    return None


def main():
    show_all = "--all" in sys.argv[1:]
    if not os.path.exists(SYMBOLS):
        sys.exit("No symbol file at %s\nGet the names with: "
                 ".\\bwvr\\tools\\re.ps1 names" % SYMBOLS)
    mac_files = sorted(glob.glob(os.path.join(CONFIG, "BW1M*", "symbols.txt")))
    if not mac_files:
        sys.exit("No Mac symbol file in %s" % CONFIG)

    mac = collections.defaultdict(list)
    for path in mac_files:
        for _, name in read_functions(path):
            parts = mac_parts(name)
            if parts and parts[2] not in mac[parts[:2]]:
                mac[parts[:2]].append(parts[2])

    names = collections.OrderedDict()
    for addr, name in read_functions(SYMBOLS):
        names.setdefault(addr, name)
    corrected = set()
    if "--no-corrections" not in sys.argv[1:]:
        for addr, name in read_functions(CORRECTIONS):
            names[addr] = name
            corrected.add(addr)

    undecorate = Undecorator()
    counts = collections.Counter()
    wrong_digit, unreadable_digit, unchecked_digit, other = [], [], [], []
    for addr, name in names.items():
        if not name.startswith("?"):
            continue
        counts["decorated names"] += 1
        digit = bool(BACKREF.search(name))
        text = undecorate(name)
        if text is None:
            counts["Windows cannot read the name"] += 1
            if digit:
                unreadable_digit.append((addr, name, "", [], []))
            continue
        parts = windows_parts(text)
        if parts is None or name.startswith("??"):
            counts["not compared (a special name, or no parameter list)"] += 1
            continue
        owner, method, result, classes = parts
        candidates = mac.get((owner, method))
        if not candidates:
            counts["no Mac symbol with this name"] += 1
            if digit:
                counts["no Mac symbol, and a class as a digit"] += 1
                unchecked_digit.append((addr, name, text, classes, []))
            continue
        if classes in candidates:
            counts["the same classes as the Mac symbol"] += 1
            continue
        # A struct that comes back by value. The Windows name gives the
        # hidden pointer to the result as the first parameter, and that is
        # what the code does.
        if (classes and classes[1:] in candidates
                and re.search(r"\b%s\s*\*" % re.escape(classes[0]), result)):
            counts["the same, with the hidden pointer to the result"] += 1
            continue
        row = (addr, name, text, classes, candidates)
        if digit:
            wrong_digit.append(row)
        else:
            other.append(row)

    counts["corrections in name-corrections.txt"] = len(corrected)
    counts["DIFFERENT, with a class as a digit"] = len(wrong_digit)
    counts["different, with each class by name (read with --all)"] = len(other)
    for key, value in counts.items():
        print("%6d  %s" % (value, key))

    def show(title, rows):
        if not rows:
            return
        print("\n%s" % title)
        for addr, name, text, classes, candidates in rows:
            print("0x%08X %s" % (addr, name))
            if text:
                print("    Windows reads: %s" % text)
                print("    its classes:   %s" % ", ".join(classes))
            for candidate in candidates[:3]:
                print("    the Mac symbol: %s" % ", ".join(candidate))

    show("A class as a digit, and the classes are not those of the Mac symbol:",
         wrong_digit)
    show("A class as a digit, and Windows cannot read the name:", unreadable_digit)
    if show_all:
        show("A class as a digit, and no Mac symbol to compare with. Read "
             "what Windows makes of each:", unchecked_digit)
        show("Each class by name, and the classes are not those of the Mac "
             "symbol. Many are a choice of the symbol file (an int for an "
             "enum, a type of the Windows build):", other)

    if wrong_digit or unreadable_digit:
        print("\nFAIL: put the right name of each in bwvr/ghidra/name-corrections.txt")
        sys.exit(1)
    print("\nOK: no name has a wrong back-reference digit.")


if __name__ == "__main__":
    main()
