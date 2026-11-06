package com.winlator.jerusalem;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import androidx.documentfile.provider.DocumentFile;

import com.winlator.core.FileUtils;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Copies a Jerusalem game disc into app storage, from either a CD image
 * (.iso, or raw .bin) or an already-extracted folder.
 *
 * Each disc ends up in files/jerusalem/discN, where N comes from the disc's own
 * data/cd.ini. The launcher maps disc 1 to D: and disc 2 (if any) to E:,
 * both flagged as CD-ROM drives so the game's CD check finds them.
 */
public final class GameImporter {
    public interface Listener {
        void onProgress(int percent);
    }

    public static final String MARKER = "data/cd.ini";
    private static final long SPARE_SPACE = 64L * 1024 * 1024;

    private final Context context;
    private final Listener listener;
    private final AtomicBoolean cancelled = new AtomicBoolean(false);
    private long total;
    private long done;
    private int lastPercent = -1;

    public GameImporter(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    public void cancel() {
        cancelled.set(true);
    }

    public static File getGameDir(Context context) {
        return new File(context.getFilesDir(), "jerusalem");
    }

    public static File getDiscDir(Context context, int disc) {
        return new File(getGameDir(context), "disc" + disc);
    }

    public static File getExecutable(Context context) {
        return new File(getDiscDir(context, 1), "Jerusalem.exe");
    }

    public static boolean isDiscImported(Context context, int disc) {
        File dir = getDiscDir(context, disc);
        return new File(dir, ".imported").isFile();
    }

    private File stagingDir() {
        return new File(getGameDir(context), "import-tmp");
    }

    // ------------------------------------------------------------------ images

    /** Imports from an .iso/.bin. Returns the disc number. */
    public int importImage(Uri uri) throws IOException {
        ParcelFileDescriptor pfd = context.getContentResolver().openFileDescriptor(uri, "r");
        if (pfd == null) throw new IOException("Could not open the selected file");

        try (FileInputStream fis = new ParcelFileDescriptor.AutoCloseInputStream(pfd);
             FileChannel channel = fis.getChannel();
             IsoImage iso = IsoImage.open(channel)) {

            List<IsoImage.Entry> entries = iso.listAll();
            IsoImage.Entry marker = IsoImage.find(entries, MARKER);
            if (marker == null) throw new IOException("This disc image doesn't look like Jerusalem (no data/cd.ini).");

            ByteArrayOutputStream cdIni = new ByteArrayOutputStream();
            iso.extract(marker, cdIni, null);
            int disc = parseDiscNumber(cdIni.toByteArray());

            total = IsoImage.totalSize(entries);
            checkFreeSpace();

            File staging = stagingDir();
            FileUtils.delete(staging);
            if (!staging.mkdirs()) throw new IOException("Could not create " + staging);

            for (IsoImage.Entry e : entries) {
                File dst = new File(staging, e.path);
                if (e.directory) {
                    dst.mkdirs();
                    continue;
                }
                File parent = dst.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream out = new BufferedOutputStream(new FileOutputStream(dst), 1 << 16)) {
                    iso.extract(e, out, this::addProgress);
                }
            }

            finish(staging, disc, iso.getVolumeId());
            return disc;
        }
        catch (IOException e) {
            FileUtils.delete(stagingDir());
            throw e;
        }
    }

    // ----------------------------------------------------------------- folders

    /** Imports from a folder picked with the system file picker. Returns the disc number. */
    public int importFolder(Uri treeUri) throws IOException {
        DocumentFile picked = DocumentFile.fromTreeUri(context, treeUri);
        if (picked == null || !picked.isDirectory()) throw new IOException("Could not open the selected folder");

        // The game files may sit one or two levels below the folder the user picked.
        DocumentFile root = findGameRoot(picked, 0);
        if (root == null) throw new IOException("Couldn't find the game files (data/cd.ini) in that folder.");

        List<DocumentFile> files = new ArrayList<>();
        List<String> paths = new ArrayList<>();
        total = 0;
        collect(root, "", files, paths);
        checkFreeSpace();

        File staging = stagingDir();
        FileUtils.delete(staging);
        if (!staging.mkdirs()) throw new IOException("Could not create " + staging);

        ContentResolver resolver = context.getContentResolver();
        byte[] buf = new byte[1 << 16];
        try {
            for (int i = 0; i < files.size(); i++) {
                File dst = new File(staging, paths.get(i));
                File parent = dst.getParentFile();
                if (parent != null) parent.mkdirs();
                try (InputStream in = resolver.openInputStream(files.get(i).getUri());
                     OutputStream out = new BufferedOutputStream(new FileOutputStream(dst), 1 << 16)) {
                    if (in == null) throw new IOException("Could not read " + paths.get(i));
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                        if (!addProgress(n)) throw new IOException("Cancelled");
                    }
                }
            }

            File marker = findCaseInsensitive(staging, MARKER);
            int disc = parseDiscNumber(marker != null ? FileUtils.read(marker) : null);
            finish(staging, disc, "");
            return disc;
        }
        catch (IOException e) {
            FileUtils.delete(staging);
            throw e;
        }
    }

    private static DocumentFile findGameRoot(DocumentFile dir, int depth) {
        DocumentFile data = findChild(dir, "data");
        if (data != null && data.isDirectory() && findChild(data, "cd.ini") != null) return dir;
        if (depth >= 2) return null;
        for (DocumentFile child : dir.listFiles()) {
            if (child.isDirectory()) {
                DocumentFile found = findGameRoot(child, depth + 1);
                if (found != null) return found;
            }
        }
        return null;
    }

    private static DocumentFile findChild(DocumentFile dir, String name) {
        for (DocumentFile child : dir.listFiles()) {
            String n = child.getName();
            if (n != null && n.equalsIgnoreCase(name)) return child;
        }
        return null;
    }

    private void collect(DocumentFile dir, String prefix, List<DocumentFile> files, List<String> paths) {
        for (DocumentFile child : dir.listFiles()) {
            String name = child.getName();
            if (name == null || name.equals(".") || name.equals("..") || name.contains("/")) continue;
            if (child.isDirectory()) {
                collect(child, prefix + name + "/", files, paths);
            }
            else {
                // Skip disc images that may sit next to the extracted files
                String lower = name.toLowerCase(Locale.ENGLISH);
                if (prefix.isEmpty() && (lower.endsWith(".iso") || lower.endsWith(".bin") || lower.endsWith(".cue"))) continue;
                files.add(child);
                paths.add(prefix + name);
                total += Math.max(0, child.length());
            }
        }
    }

    // ------------------------------------------------------------------ common

    private void finish(File staging, int disc, String volumeId) throws IOException {
        if (disc == 1) {
            File exe = findCaseInsensitive(staging, "Jerusalem.exe");
            if (exe == null) {
                File packed = findCaseInsensitive(staging, "Jerusalem.ex_");
                if (packed == null) throw new IOException("Disc 1 is missing Jerusalem.ex_ (the game program).");
                // On the CD the program is stored as Jerusalem.ex_; the installer just copies it as .exe
                if (!FileUtils.copy(packed, new File(staging, "Jerusalem.exe"))) throw new IOException("Could not create Jerusalem.exe");
            }
        }

        // Wine reads a drive's volume label from this file
        if (volumeId != null && !volumeId.isEmpty()) {
            FileUtils.writeString(new File(staging, ".windows-label"), volumeId);
        }
        FileUtils.writeString(new File(staging, ".imported"), "disc=" + disc + "\n");

        File dst = getDiscDir(context, disc);
        FileUtils.delete(dst);
        if (!staging.renameTo(dst)) throw new IOException("Could not move the imported files into place");
        report(100);
    }

    private static int parseDiscNumber(byte[] cdIni) {
        if (cdIni == null) return 1;
        String s = new String(cdIni, StandardCharsets.ISO_8859_1).trim();
        int end = 0;
        while (end < s.length() && Character.isDigit(s.charAt(end))) end++;
        try {
            int n = Integer.parseInt(s.substring(0, end));
            return n >= 1 && n <= 4 ? n : 1;
        }
        catch (NumberFormatException e) {
            return 1;
        }
    }

    static File findCaseInsensitive(File base, String relPath) {
        File cur = base;
        for (String part : relPath.split("/")) {
            File[] children = cur.listFiles();
            File match = null;
            if (children != null) for (File c : children) if (c.getName().equalsIgnoreCase(part)) { match = c; break; }
            if (match == null) return null;
            cur = match;
        }
        return cur;
    }

    private void checkFreeSpace() throws IOException {
        File dir = getGameDir(context);
        dir.mkdirs();
        long free = dir.getUsableSpace();
        if (free < total + SPARE_SPACE) {
            throw new IOException(String.format(Locale.ENGLISH,
                "Not enough free space: the game needs %d MB, but only %d MB is free.",
                (total + SPARE_SPACE) >> 20, free >> 20));
        }
    }

    private boolean addProgress(long n) {
        done += n;
        report(total > 0 ? (int) Math.min(99, done * 100 / total) : 0);
        return !cancelled.get();
    }

    private void report(int percent) {
        if (percent != lastPercent) {
            lastPercent = percent;
            if (listener != null) listener.onProgress(percent);
        }
    }
}
