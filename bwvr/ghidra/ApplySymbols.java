// Gives the names of the public project openblack/bw1-decomp to the open
// program: config/BW1W120/symbols.txt, about 26,000 functions and 12,000
// data names.
//
// Run it before the analysis. tools/re.ps1 does this when it makes the
// project. The names are decorated MSVC names, and the analysis of Ghidra
// then makes a class, a signature and a calling convention from each one.
//
// The runblack.exe of the unofficial patch v1.42 has the code of v1.20 at the
// same addresses, so the names of BW1W120 fit it. See docs/address-map.md.
//
// The one argument is the path of symbols.txt. With no argument (in the
// Script Manager of Ghidra) the script asks for the file.
//
// @category BlackAndWhiteVR

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;
import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class ApplySymbols extends GhidraScript {
  private static final Pattern LINE = Pattern.compile(
      "^(\\S+)\\s*=\\s*\\.(\\w+):0x([0-9A-Fa-f]+)\\s*;(?:\\s*//\\s*(.*))?$");

  // Four vtables that an RTTI walk of the exe found. A symbol file that does
  // not have them at these addresses is for a different build.
  private static final long[] ANCHOR_ADDRESS = {
      0x008C76E0L, 0x008CBECCL, 0x00900A54L, 0x008DF800L};
  private static final String[] ANCHOR_NAME = {
      "??_7CameraMode@@6B@", "??_7CHand@@6B@", "??_7HandStateCamera@@6B@",
      "??_7GestureSystem@@6B@"};

  // The symbol file has the names of these two addresses swapped. The vtable
  // and the call sites prove it: docs/hand.md, "Two symbol names are
  // swapped". tools/disasm.py has the same pair.
  private static final long[][] SWAPPED = {{0x005B3C70L, 0x005B5E70L}};

  private static class Entry {
    String name;
    long address;
    boolean function;
  }

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String path = args.length > 0 ? args[0]
        : askFile("symbols.txt of bw1-decomp (config/BW1W120)", "Apply").getAbsolutePath();

    List<Entry> entries = new ArrayList<>();
    Map<Long, Entry> first = new HashMap<>();
    try (BufferedReader reader = new BufferedReader(
        new InputStreamReader(new FileInputStream(path), StandardCharsets.UTF_8))) {
      String line;
      while ((line = reader.readLine()) != null) {
        Matcher m = LINE.matcher(line.trim());
        if (!m.matches()) continue;
        String comment = m.group(4) == null ? "" : m.group(4);
        Entry e = new Entry();
        e.name = m.group(1);
        e.address = Long.parseLong(m.group(3), 16);
        e.function = m.group(2).equals("text") && comment.contains("type:function");
        entries.add(e);
        first.putIfAbsent(e.address, e);
      }
    }

    for (int i = 0; i < ANCHOR_ADDRESS.length; i++) {
      Entry e = first.get(ANCHOR_ADDRESS[i]);
      if (e == null || !e.name.equals(ANCHOR_NAME[i])) {
        println("ApplySymbols: STOPPED. The symbol file is for a different build: "
            + ANCHOR_NAME[i] + " is not at " + Long.toHexString(ANCHOR_ADDRESS[i]) + " in " + path);
        return;
      }
    }

    for (long[] pair : SWAPPED) {
      Entry a = first.get(pair[0]);
      Entry b = first.get(pair[1]);
      if (a == null || b == null) continue;
      String name = a.name;
      a.name = b.name;
      b.name = name;
    }

    SymbolTable table = currentProgram.getSymbolTable();
    int functions = 0, labels = 0, fails = 0;
    monitor.initialize(entries.size());
    monitor.setMessage("Applying " + entries.size() + " names");
    for (Entry e : entries) {
      if (monitor.isCancelled()) break;
      monitor.incrementProgress(1);
      Address address = toAddr(e.address);
      try {
        if (currentProgram.getMemory().getBlock(address) == null) {
          fails++;
          continue;
        }
        if (e.function) {
          disassemble(address);
          Function f = getFunctionAt(address);
          // No name at the creation: createFunction gives a name the source
          // "user defined", and the analysis does not demangle such a name.
          if (f == null) f = createFunction(address, null);
          if (f != null) {
            f.setName(e.name, SourceType.IMPORTED);
            functions++;
            continue;
          }
        }
        table.createLabel(address, e.name, SourceType.IMPORTED);
        labels++;
      } catch (Exception ex) {
        fails++;
      }
    }
    println("ApplySymbols: functions " + functions + ", labels " + labels + ", fails " + fails);
  }
}
