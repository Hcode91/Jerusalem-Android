// Maps loader diagnostics to functions and saves private analysis output.
// @category Jerusalem

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.Address;
import ghidra.framework.model.DomainFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class LoaderMilestone extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected private output directory");
        Path out = Path.of(args[0]);
        Files.createDirectories(out);
        // Auto-analysis left these string-referenced code regions without functions.
        // Seed only this verified executable build and require its exact entry bytes.
        if ("b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687".equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            seedAudioHeader(0x004a2440L, "aSecComSouStereo_DecompressHeader_candidate");
            seedAudioHeader(0x004a2bb0L, "aSecComSouMono_DecompressHeader_candidate");
        }
        String program = currentProgram.getName().replaceAll("[^A-Za-z0-9._-]", "_");
        StringBuilder report = new StringBuilder();
        report.append("program\t").append(currentProgram.getName()).append('\n');
        report.append("sha256\t").append(currentProgram.getExecutableSHA256()).append('\n');
        report.append("image_base\t").append(currentProgram.getImageBase()).append('\n');
        report.append("language\t").append(currentProgram.getLanguageID()).append('\n');
        report.append("compiler\t").append(currentProgram.getCompilerSpec().getCompilerSpecID()).append('\n');
        report.append("functions\t").append(currentProgram.getFunctionManager().getFunctionCount()).append('\n');
        // Link already imported helper DLLs, independent of import order or filename case.
        for (String library : currentProgram.getExternalManager().getExternalLibraryNames()) {
            if (!library.equalsIgnoreCase("CinemaMovieM.dll") && !library.equalsIgnoreCase("mmxImage.dll")) continue;
            for (DomainFile file : currentProgram.getDomainFile().getParent().getFiles()) {
                if (file.getName().equalsIgnoreCase(library)) {
                    currentProgram.getExternalManager().setExternalPath(library, file.getPathname(), true);
                    report.append("helper_library\t").append(library).append('\t').append(file.getPathname()).append('\n');
                }
            }
        }
        report.append("string_address\txref\tfunction_entry\tfunction_name\tdiagnostic\n");

        String[] needles = {
            "aseccomsoumono::decompressheader", "aseccomsoustereo::decompressheader",
            "aseccomaqi::decompressnode", "aseccomaqi::decompresschannel",
            "aimagefilecinema::readheader", "aart::init - can not read art header",
            "aimagefiletgc::loadimage - readheader",
            "arotation::alloc", "arotation::loadsave", "aapplication::rotsetact",
            "caquatorimageheader::allocbuffer"
        };
        Map<String, Function> selected = new LinkedHashMap<>();
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext() && !monitor.isCancelled()) {
            Data item = data.next();
            Object value = item.getValue();
            if (!(value instanceof String)) continue;
            String text = (String)value;
            String lower = text.toLowerCase(Locale.ROOT);
            boolean match = false;
            for (String needle : needles) match |= lower.contains(needle);
            if (!match) continue;
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Function function = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                report.append(item.getAddress()).append('\t').append(ref.getFromAddress()).append('\t');
                if (function == null) report.append("<unidentified>\t<unidentified>");
                else {
                    report.append(function.getEntryPoint()).append('\t').append(function.getName());
                    selected.put(function.getEntryPoint().toString(), function);
                }
                report.append('\t').append(text.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ')).append('\n');
            }
        }
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
            for (Function function : selected.values()) {
                if (monitor.isCancelled()) break;
                DecompileResults result = decompiler.decompileFunction(function, 45, monitor);
                String filename = program + "." + function.getEntryPoint() + ".decompiled.c";
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    Files.writeString(out.resolve(filename), result.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
                    report.append("decompiled\t").append(function.getEntryPoint()).append('\t').append(filename).append('\n');
                } else report.append("decompile_failed\t").append(function.getEntryPoint()).append('\t').append(result.getErrorMessage()).append('\n');
            }
        } finally {
            decompiler.dispose();
        }
        Files.writeString(out.resolve(program + ".loaders.tsv"), report.toString(), StandardCharsets.UTF_8);
        println("JERUSALEM_MILESTONE program=" + program + " functions=" + currentProgram.getFunctionManager().getFunctionCount()
            + " loader_candidates=" + selected.size() + " private_report=" + out.resolve(program + ".loaders.tsv"));
    }

    private void seedAudioHeader(long offset, String name) throws Exception {
        Address address = toAddr(offset);
        byte[] expected = HexFormat.of().parseHex("83ec0c53558be956");
        byte[] actual = new byte[expected.length];
        currentProgram.getMemory().getBytes(address, actual);
        if (!Arrays.equals(expected, actual)) throw new IllegalStateException("Unexpected loader entry bytes at " + address);
        Function function = getFunctionAt(address);
        if (function == null) {
            disassemble(address);
            function = createFunction(address, name);
            if (function == null) throw new IllegalStateException("Cannot define loader at " + address);
        }
    }
}
