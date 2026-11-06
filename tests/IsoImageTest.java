import com.winlator.jerusalem.IsoImage;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.util.List;
import java.util.Locale;

/** Plain-JVM test: java IsoImageTest DIR (DIR made by make_test_iso.py). */
public class IsoImageTest {
    public static void main(String[] args) throws Exception {
        File dir = new File(args[0]);
        List<String> manifest = Files.readAllLines(new File(dir, "manifest.txt").toPath());
        int failures = 0;

        String[][] images = {{"joliet.iso", "2048", "y"}, {"plain.iso", "2048", "n"}, {"mode1.bin", "2352", "y"}, {"mode2.bin", "2352", "y"}};
        for (String[] img : images) {
            try (FileChannel ch = FileChannel.open(new File(dir, img[0]).toPath(), StandardOpenOption.READ);
                 IsoImage iso = IsoImage.open(ch)) {
                boolean joliet = img[2].equals("y");
                check(iso.getRawSectorSize() == Integer.parseInt(img[1]), img[0] + ": sector size " + iso.getRawSectorSize());
                check(iso.isJoliet() == joliet, img[0] + ": joliet=" + iso.isJoliet());
                check(iso.getVolumeId().equals("JERUSALEM"), img[0] + ": volume id '" + iso.getVolumeId() + "'");

                List<IsoImage.Entry> entries = iso.listAll();
                int fileCount = 0;
                for (IsoImage.Entry e : entries) if (!e.directory) fileCount++;
                check(fileCount == manifest.size(), img[0] + ": " + fileCount + " files, expected " + manifest.size());

                for (String line : manifest) {
                    String[] f = line.split("\t");
                    IsoImage.Entry e = IsoImage.find(entries, f[0]);
                    if (e == null) { System.out.println("FAIL " + img[0] + ": missing " + f[0]); failures++; continue; }
                    if (joliet && !e.path.equals(f[0])) { System.out.println("FAIL " + img[0] + ": case " + e.path); failures++; }
                    if (!joliet && !e.path.equals(f[0].toUpperCase(Locale.ENGLISH))) { System.out.println("FAIL " + img[0] + ": name " + e.path); failures++; }
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    iso.extract(e, out, null);
                    String sha = hex(MessageDigest.getInstance("SHA-1").digest(out.toByteArray()));
                    if (out.size() != Long.parseLong(f[1]) || !sha.equals(f[2])) {
                        System.out.println("FAIL " + img[0] + ": content of " + f[0]);
                        failures++;
                    }
                }
                System.out.println("ok   " + img[0] + " (" + entries.size() + " entries)");
            }
            catch (AssertionError ae) {
                System.out.println("FAIL " + ae.getMessage());
                failures++;
            }
        }

        File junk = new File(dir, "junk.bin");
        Files.write(junk.toPath(), new byte[100000]);
        try (FileChannel ch = FileChannel.open(junk.toPath(), StandardOpenOption.READ)) {
            IsoImage.open(ch);
            System.out.println("FAIL junk accepted");
            failures++;
        }
        catch (java.io.IOException expected) {
            System.out.println("ok   junk rejected");
        }

        if (failures > 0) {
            System.out.println(failures + " failure(s)");
            System.exit(1);
        }
        System.out.println("All ISO tests passed");
    }

    private static void check(boolean cond, String msg) {
        if (!cond) throw new AssertionError(msg);
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(String.format("%02x", x));
        return sb.toString();
    }
}
