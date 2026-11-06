// Private analysis export for explicit entry addresses, not a codec implementation.
// @category Jerusalem
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

public class ResourceTrace extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if (args.length<2) throw new IllegalArgumentException("OUTPUT_DIRECTORY ENTRY_HEX...");
        Path out=Path.of(args[0]); Files.createDirectories(out);
        DecompInterface decompiler=new DecompInterface();
        StringBuilder manifest=new StringBuilder("entry\tname\tdecompiled\n");
        try {
            if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
            for (int i=1;i<args.length;++i) {
                monitor.checkCancelled(); Address entry=toAddr(Long.parseLong(args[i],16));
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
