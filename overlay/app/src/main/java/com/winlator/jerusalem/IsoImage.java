package com.winlator.jerusalem;

import java.io.Closeable;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Minimal read-only ISO 9660 reader (with Joliet long names) for CD images.
 * Plain Java, no Android dependencies, so it can be unit-tested on a desktop JVM.
 *
 * Accepts both "cooked" .iso files (2048-byte sectors) and raw .bin images
 * (2352-byte sectors, Mode 1 or Mode 2 Form 1), as well as 2336-byte Mode 2 images.
 * Only the first data track is read, which is where a PC game's files live.
 */
public final class IsoImage implements Closeable {
    public static final int SECTOR = 2048;

    public static final class Entry {
        public final String path;      // relative path using '/', e.g. "data/a01/sound/x.wac"
        public final boolean directory;
        public final long lba;
        public long size;

        Entry(String path, boolean directory, long lba, long size) {
            this.path = path;
            this.directory = directory;
            this.lba = lba;
            this.size = size;
        }

        @Override
        public String toString() {
            return (directory ? "[D] " : "    ") + path + " (" + size + ")";
        }
    }

    public interface ProgressListener {
        /** Return false to cancel. */
        boolean onProgress(long bytesDone);
    }

    private final FileChannel channel;
    private final int rawSectorSize;
    private final int dataOffset;
    private final long imageSize;
    private final byte[] sectorBuf;
    private String volumeId = "";
    private long rootLba;
    private long rootSize;
    private boolean joliet;

    private IsoImage(FileChannel channel, int rawSectorSize, int dataOffset) throws IOException {
        this.channel = channel;
        this.rawSectorSize = rawSectorSize;
        this.dataOffset = dataOffset;
        this.imageSize = channel.size();
        this.sectorBuf = new byte[rawSectorSize];
    }

    /** Detects the sector layout and parses the volume descriptors. */
    public static IsoImage open(FileChannel channel) throws IOException {
        int[][] layouts = {{2048, 0}, {2352, 16}, {2352, 24}, {2336, 8}};
        for (int[] layout : layouts) {
            IsoImage image = new IsoImage(channel, layout[0], layout[1]);
            if (image.hasDescriptorAt16()) {
                image.readVolumeDescriptors();
                return image;
            }
        }
        throw new IOException("Not an ISO 9660 CD image (no CD001 signature found)");
    }

    public String getVolumeId() {
        return volumeId;
    }

    public boolean isJoliet() {
        return joliet;
    }

    public int getRawSectorSize() {
        return rawSectorSize;
    }

    private boolean hasDescriptorAt16() throws IOException {
        long pos = 16L * rawSectorSize + dataOffset;
        if (pos + 6 > imageSize) return false;
        ByteBuffer b = ByteBuffer.allocate(6);
        readFully(pos, b);
        byte[] a = b.array();
        return a[1] == 'C' && a[2] == 'D' && a[3] == '0' && a[4] == '0' && a[5] == '1';
    }

    private void readFully(long pos, ByteBuffer dst) throws IOException {
        while (dst.hasRemaining()) {
            int n = channel.read(dst, pos);
            if (n < 0) throw new IOException("Unexpected end of image at " + pos);
            pos += n;
        }
    }

    /** Reads the 2048 user-data bytes of a logical sector into out[0..2047]. */
    public void readSector(long lba, byte[] out) throws IOException {
        long pos = lba * rawSectorSize;
        if (pos + dataOffset + SECTOR > imageSize) throw new IOException("Sector " + lba + " is past the end of the image");
        ByteBuffer b = ByteBuffer.wrap(sectorBuf, 0, rawSectorSize == SECTOR ? SECTOR : Math.min(rawSectorSize, (int) (imageSize - pos)));
        readFully(pos, b);
        System.arraycopy(sectorBuf, dataOffset, out, 0, SECTOR);
    }

    private void readVolumeDescriptors() throws IOException {
        byte[] s = new byte[SECTOR];
        long pvdRootLba = -1, pvdRootSize = 0;
        long jRootLba = -1, jRootSize = 0;

        for (long lba = 16; lba < 16 + 64; lba++) {
            readSector(lba, s);
            if (s[1] != 'C' || s[2] != 'D' || s[3] != '0' || s[4] != '0' || s[5] != '1') break;
            int type = s[0] & 0xff;
            if (type == 255) break;
            if (type == 1 && pvdRootLba < 0) {
                volumeId = new String(s, 40, 32, StandardCharsets.US_ASCII).trim();
                pvdRootLba = le32(s, 156 + 2);
                pvdRootSize = le32(s, 156 + 10);
            }
            else if (type == 2 && s[88] == '%' && s[89] == '/' && (s[90] == '@' || s[90] == 'C' || s[90] == 'E')) {
                jRootLba = le32(s, 156 + 2);
                jRootSize = le32(s, 156 + 10);
            }
        }

        if (jRootLba >= 0) {
            joliet = true;
            rootLba = jRootLba;
            rootSize = jRootSize;
        }
        else if (pvdRootLba >= 0) {
            rootLba = pvdRootLba;
            rootSize = pvdRootSize;
        }
        else throw new IOException("ISO image has no primary volume descriptor");
    }

    /** Lists every file and directory on the disc, parents before children. */
    public List<Entry> listAll() throws IOException {
        List<Entry> result = new ArrayList<>();
        Set<Long> visited = new HashSet<>();
        walk(rootLba, rootSize, "", result, visited, 0);
        return result;
    }

    private void walk(long lba, long size, String prefix, List<Entry> out, Set<Long> visited, int depth) throws IOException {
        if (depth > 32 || !visited.add(lba)) return;
        byte[] s = new byte[SECTOR];
        long sectors = (size + SECTOR - 1) / SECTOR;
        List<Entry> subdirs = new ArrayList<>();
        Entry last = null;

        for (long i = 0; i < sectors; i++) {
            readSector(lba + i, s);
            int off = 0;
            while (off < SECTOR) {
                int len = s[off] & 0xff;
                if (len == 0) break; // rest of this sector is padding
                if (off + len > SECTOR || len < 34) break;

                int flags = s[off + 25] & 0xff;
                int nameLen = s[off + 32] & 0xff;
                if (33 + nameLen > len) break;
                long extent = le32(s, off + 2);
                long dataLen = le32(s, off + 10);

                boolean special = nameLen == 1 && (s[off + 33] == 0 || s[off + 33] == 1);
                if (!special) {
                    String name = decodeName(s, off + 33, nameLen);
                    boolean dir = (flags & 0x02) != 0;
                    String path = prefix + name;

                    if (!dir && last != null && !last.directory && last.path.equals(path)) {
                        // continuation of a multi-extent file
                        last.size += dataLen;
                    }
                    else if (!name.isEmpty()) {
                        Entry e = new Entry(path, dir, extent, dataLen);
                        out.add(e);
                        if (dir) subdirs.add(e);
                        last = e;
                    }
                }
                off += len;
            }
        }

        for (Entry d : subdirs) walk(d.lba, d.size, d.path + "/", out, visited, depth + 1);
    }

    private String decodeName(byte[] s, int off, int len) {
        String name;
        if (joliet) name = new String(s, off, len & ~1, StandardCharsets.UTF_16BE);
        else name = new String(s, off, len, StandardCharsets.ISO_8859_1);

        int semi = name.indexOf(';');
        if (semi >= 0) name = name.substring(0, semi);
        if (name.endsWith(".")) name = name.substring(0, name.length() - 1);
        // Never allow path tricks out of the destination folder
        name = name.replace('/', '_').replace('\\', '_');
        if (name.equals("..") || name.equals(".")) name = "_";
        return name;
    }

    /** Streams one file's contents. */
    public void extract(Entry entry, OutputStream out, ProgressListener listener) throws IOException {
        if (entry.directory) throw new IOException(entry.path + " is a directory");
        byte[] s = new byte[SECTOR];
        long remaining = entry.size;
        long lba = entry.lba;
        while (remaining > 0) {
            readSector(lba++, s);
            int n = (int) Math.min(SECTOR, remaining);
            out.write(s, 0, n);
            remaining -= n;
            if (listener != null && !listener.onProgress(n)) throw new IOException("Cancelled");
        }
    }

    public static long totalSize(List<Entry> entries) {
        long total = 0;
        for (Entry e : entries) if (!e.directory) total += e.size;
        return total;
    }

    private static long le32(byte[] b, int off) {
        return (b[off] & 0xffL) | ((b[off + 1] & 0xffL) << 8) | ((b[off + 2] & 0xffL) << 16) | ((b[off + 3] & 0xffL) << 24);
    }

    @Override
    public void close() throws IOException {
        channel.close();
    }

    /** Case-insensitive lookup helper. */
    public static Entry find(List<Entry> entries, String path) {
        String p = path.toLowerCase(Locale.ENGLISH);
        for (Entry e : entries) if (e.path.toLowerCase(Locale.ENGLISH).equals(p)) return e;
        return null;
    }
}
