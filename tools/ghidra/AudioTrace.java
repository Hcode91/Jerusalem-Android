// Exports private audio-analysis evidence; does not execute original code.
// @category Jerusalem

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

public class AudioTrace extends GhidraScript {
    @Override public void run() throws Exception {
        if (!"b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687".equalsIgnoreCase(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("This trace targets the verified Jerusalem executable only");
        Path out=Path.of(getScriptArgs()[0]); Files.createDirectories(out);
        long[] addresses={0x411640,0x411840,0x411860,0x415410,0x415890,0x4a25e0,0x4a2d10,0x41df50,0x4a3380,0x4a34d0,0x4a3500,0x41cec0};
        StringBuilder manifest=new StringBuilder("entry\tname\tdecompiled\n");
        DecompInterface decompiler=new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
            for (long value:addresses) {
                monitor.checkCancelled(); Address entry=toAddr(value);
                Function function=getFunctionAt(entry);
                if (function==null) { disassemble(entry); function=createFunction(entry,"audio_trace_"+entry); }
                if (function==null) throw new IllegalStateException("Function boundary unresolved: "+entry);
                DecompileResults result=decompiler.decompileFunction(function,45,monitor);
                boolean ok=result.decompileCompleted() && result.getDecompiledFunction()!=null;
                manifest.append(entry).append('\t').append(function.getName()).append('\t').append(ok).append('\n');
                if (ok) Files.writeString(out.resolve(entry+".decompiled.c"),result.getDecompiledFunction().getC(),StandardCharsets.UTF_8);
                StringBuilder listing=new StringBuilder();
                InstructionIterator instructions=currentProgram.getListing().getInstructions(function.getBody(),true);
                while (instructions.hasNext()) {
                    Instruction instruction=instructions.next();
                    listing.append(instruction.getAddress()).append('\t').append(instruction).append('\n');
                }
                Files.writeString(out.resolve(entry+".instructions.tsv"),listing.toString(),StandardCharsets.UTF_8);
            }
        } finally { decompiler.dispose(); }
        Files.writeString(out.resolve("manifest.tsv"),manifest.toString(),StandardCharsets.UTF_8);
        println("AUDIO_TRACE complete: "+out);
    }
}
