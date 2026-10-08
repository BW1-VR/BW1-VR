// Writes the decompiled C of a list of functions to one file.
//
// The two arguments are the list file and the output file. The list file has
// one address in each line, in hex. A line that starts with "#" is a comment.
// An address inside a function gives that function. tools/re.ps1 calls this
// script.
//
// @category BlackAndWhiteVR

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class Decompile extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      println("Decompile: STOPPED. Give the list file and the output file.");
      return;
    }
    List<String> lines = Files.readAllLines(Paths.get(args[0]));
    DecompInterface decompiler = new DecompInterface();
    decompiler.setOptions(new DecompileOptions());
    decompiler.openProgram(currentProgram);
    int done = 0, failed = 0;
    try (PrintWriter out = new PrintWriter(
        Files.newBufferedWriter(Paths.get(args[1]), StandardCharsets.UTF_8))) {
      for (String line : lines) {
        String text = line.trim();
        if (text.isEmpty() || text.startsWith("#")) continue;
        Address address;
        try {
          address = toAddr(Long.parseLong(text.replaceFirst("^0[xX]", ""), 16));
        } catch (NumberFormatException e) {
          out.println("// not an address: " + text);
          failed++;
          continue;
        }
        Function f = getFunctionAt(address);
        if (f == null) f = getFunctionContaining(address);
        if (f == null) {
          disassemble(address);
          f = createFunction(address, null);
        }
        if (f == null) {
          out.println("// no function at " + text);
          failed++;
          continue;
        }
        DecompileResults result = decompiler.decompileFunction(f, 180, monitor);
        out.println("// ==== " + f.getName(true) + " @ " + f.getEntryPoint()
            + " (asked " + text + ") ====");
        out.println("// " + f.getPrototypeString(true, true));
        if (f.getSignatureSource() == SourceType.IMPORTED) {
          out.println("// The parameter types come from the decorated name, not from the code.");
        }
        // The decorated name of the symbol file is a second label at the
        // entry. Print it, so that a search for it finds this function.
        for (Symbol label : currentProgram.getSymbolTable().getSymbols(f.getEntryPoint())) {
          if (!label.isPrimary()) out.println("// label: " + label.getName(true));
        }
        if (result.decompileCompleted()) {
          out.println(result.getDecompiledFunction().getC());
          done++;
        } else {
          out.println("// failed: " + result.getErrorMessage());
          failed++;
        }
        out.flush();
      }
    }
    decompiler.dispose();
    println("Decompile: functions " + done + ", fails " + failed);
  }
}
