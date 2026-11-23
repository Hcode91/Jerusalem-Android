// Private analysis export for explicit entry addresses, not a codec implementation.
// @category Jerusalem
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class ResourceTrace extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if (args.length<2) throw new IllegalArgumentException("OUTPUT_DIRECTORY ENTRY_HEX...");
        Path out=Path.of(args[0]); Files.createDirectories(out);
        List<Address> requested=new ArrayList<>();
        for (int i=1;i<args.length;++i) {
            if (args[i].startsWith("refs:")) {
                Address target=toAddr(Long.parseLong(args[i].substring(5),16));
                StringBuilder refs=new StringBuilder();
                for (var ref:getReferencesTo(target)) {
                    Function function=getFunctionContaining(ref.getFromAddress());
                    refs.append(ref.getFromAddress()).append('\t').append(ref.getReferenceType()).append('\t')
                        .append(function==null ? "<unidentified>" : function.getEntryPoint()).append('\n');
                    if (function!=null && !requested.contains(function.getEntryPoint())) requested.add(function.getEntryPoint());
                }
                Files.writeString(out.resolve(target+".data-references.tsv"),refs.toString(),StandardCharsets.UTF_8);
            } else if (args[i].startsWith("range:")) {
                String[] range=args[i].split(":");
                Address start=toAddr(Long.parseLong(range[1],16)), end=toAddr(Long.parseLong(range[2],16));
                FunctionIterator functions=currentProgram.getFunctionManager().getFunctions(start,true);
                while (functions.hasNext()) {
                    Function f=functions.next();
                    if (f.getEntryPoint().compareTo(end)>=0) break;
                    requested.add(f.getEntryPoint());
                }
            } else requested.add(toAddr(Long.parseLong(args[i],16)));
        }
        DecompInterface decompiler=new DecompInterface();
        StringBuilder manifest=new StringBuilder("entry\tname\tdecompiled\n");
        try {
            if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
            for (Address entry:requested) {
                monitor.checkCancelled();
                Function function=getFunctionAt(entry);
                if (function==null) function=getFunctionContaining(entry);
                if (function==null) { disassemble(entry); function=createFunction(entry,"resource_trace_"+entry); }
                if (function==null) throw new IllegalStateException("Unresolved boundary: "+entry);
                DecompileResults result=decompiler.decompileFunction(function,60,monitor);
                boolean ok=result.decompileCompleted() && result.getDecompiledFunction()!=null;
                manifest.append(entry).append('\t').append(function.getName()).append('\t').append(ok).append('\n');
                if (ok) Files.writeString(out.resolve(entry+".decompiled.c"),result.getDecompiledFunction().getC(),StandardCharsets.UTF_8);
                StringBuilder listing=new StringBuilder(), refs=new StringBuilder();
                InstructionIterator instructions=currentProgram.getListing().getInstructions(function.getBody(),true);
                while (instructions.hasNext()) {
                    Instruction instruction=instructions.next();
                    listing.append(instruction.getAddress()).append('\t').append(instruction).append('\n');
                }
                for (var reference:getReferencesTo(entry))
                    refs.append(reference.getFromAddress()).append('\t').append(reference.getReferenceType()).append('\n');
                Files.writeString(out.resolve(entry+".instructions.tsv"),listing.toString(),StandardCharsets.UTF_8);
                Files.writeString(out.resolve(entry+".callers.tsv"),refs.toString(),StandardCharsets.UTF_8);
            }
        } finally { decompiler.dispose(); }
        Files.writeString(out.resolve("manifest.tsv"),manifest.toString(),StandardCharsets.UTF_8);
        println("RESOURCE_TRACE complete: "+out);
    }
}
