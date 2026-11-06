package org.jerusalem.engine;

import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Android SAF import and accessible controls; decoding and playback live in C++. */
public class MainActivity extends SDLActivity {
    private static final int PICK_TREE = 100;
    private static final long FILE_LIMIT = 64L * 1024 * 1024;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button importButton, browseButton;
    private File dataRoot;
    private static native byte[] nativeDescribe(String path);
    private static native void nativeOpen(String path);
    private static native void nativeCommand(int command);
    private static native String nativeStatus();

    @Override protected String[] getLibraries() { return new String[] {"SDL3", "main"}; }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) return;
        dataRoot = new File(getFilesDir(), "game-data");
        if (!dataRoot.isDirectory() && !dataRoot.mkdirs()) {
            showText("Storage error", "Cannot create private game-data directory.");
            return;
        }
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(0xEE18222B);
        panel.setPadding(12, 4, 12, 4);
        status = new TextView(this);
        status.setTextColor(0xFFFFFFFF);
        status.setText("Import original data, then choose an asset. This build is an inspector; gameplay is not implemented.");
        panel.addView(status);
        LinearLayout buttons = new LinearLayout(this);
        importButton = button("Import data", v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, PICK_TREE);
        });
        browseButton = button("Browse assets", v -> browse(dataRoot));
        buttons.addView(importButton); buttons.addView(browseButton);
        buttons.addView(button("Fit image", v -> nativeCommand(1)));
        buttons.addView(button("Zoom +", v -> nativeCommand(2)));
        buttons.addView(button("Zoom −", v -> nativeCommand(3)));
        buttons.addView(button("Stop audio", v -> nativeCommand(4)));
        buttons.addView(button("Status", v -> showText("Native status", nativeStatus())));
        android.widget.HorizontalScrollView row = new android.widget.HorizontalScrollView(this);
        row.addView(buttons); panel.addView(row);
        android.widget.RelativeLayout.LayoutParams layout = new android.widget.RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        layout.addRule(android.widget.RelativeLayout.ALIGN_PARENT_BOTTOM);
        mLayout.addView(panel, layout);
    }

    private Button button(String label, View.OnClickListener action) {
        Button button = new Button(this);
        button.setText(label); button.setAllCaps(false); button.setContentDescription(label);
        button.setMinHeight(48); button.setOnClickListener(action);
        return button;
    }

    private void showText(String title, String message) {
        if (isFinishing() || isDestroyed()) return;
        TextView text = new TextView(this);
        text.setText(message); text.setTextIsSelectable(true); text.setPadding(24, 16, 24, 16);
        ScrollView scroll = new ScrollView(this); scroll.addView(text);
        new AlertDialog.Builder(this).setTitle(title).setView(scroll).setPositiveButton("Close", null).show();
    }

    private void browse(File directory) {
        File[] children = directory.listFiles(f -> f.isDirectory() || f.isFile());
        if (children == null || children.length == 0) { showText("Assets", "No imported files in this folder."); return; }
        Arrays.sort(children, Comparator.comparing((File f) -> !f.isDirectory()).thenComparing(File::getName));
        String[] labels = new String[children.length + (directory.equals(dataRoot) ? 0 : 1)];
        int offset = labels.length - children.length;
        if (offset == 1) labels[0] = ".. (parent folder)";
        for (int i = 0; i < children.length; ++i)
            labels[i + offset] = (children[i].isDirectory() ? "Folder: " : "") + children[i].getName();
        new AlertDialog.Builder(this).setTitle("Imported assets").setItems(labels, (dialog, which) -> {
            if (offset == 1 && which == 0) { browse(directory.getParentFile()); return; }
            File selected = children[which - offset];
            if (selected.isDirectory()) { browse(selected); return; }
            worker.execute(() -> {
                try {
                    byte[] result = nativeDescribe(selected.getAbsolutePath());
                    String description = new String(result, Charset.forName("windows-1252"));
                    runOnUiThread(() -> {
                        nativeOpen(selected.getAbsolutePath());
                        status.setText(selected.getName() + ": " + description.split("\\r?\\n", 2)[0]);
                        showText(selected.getName(), description);
                    });
                } catch (Exception error) {
                    runOnUiThread(() -> showText("Inspection failed", error.toString()));
                }
            });
        }).setNegativeButton("Close", null).show();
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent result) {
        super.onActivityResult(requestCode, resultCode, result);
        if (requestCode != PICK_TREE || resultCode != RESULT_OK || result == null || result.getData() == null) return;
        Uri tree = result.getData();
        importButton.setEnabled(false); browseButton.setEnabled(false); status.setText("Importing original data…");
        worker.execute(() -> {
            String message;
            try {
                ImportState state = new ImportState();
                copyTree(tree, DocumentsContract.getTreeDocumentId(tree), dataRoot, state, 0);
                message = "Imported " + state.files + " files; skipped " + state.skipped +
                        ". Choose Browse assets. Files over 64 MiB and Windows executables are skipped.";
            } catch (Exception error) { message = "Import stopped: " + error.getMessage() + ". Earlier completed files remain available."; }
            final String finalMessage = message;
            runOnUiThread(() -> { if (!isDestroyed()) {
                status.setText(finalMessage); importButton.setEnabled(true); browseButton.setEnabled(true);
            }});
        });
    }

    private static class ImportState {
        int files, skipped, visited;
        long bytes;
        final Set<String> seen = new HashSet<>();
    }

    private void copyTree(Uri tree, String id, File destination, ImportState state, int depth) throws IOException {
        if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled");
        if (depth > 32 || !state.seen.add(id)) throw new IOException("Folder cycle or nesting limit reached");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE};
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list chosen folder");
            while (cursor.moveToNext()) {
                if (++state.visited > 10000) throw new IOException("10,000 entry import limit reached");
                String childId = cursor.getString(0), name = cursor.getString(1), mime = cursor.getString(2);
                if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") ||
                        name.contains("/") || name.contains("\\") || name.contains(":")) throw new IOException("Unsafe asset name");
                for (int i = 0; i < name.length(); ++i)
                    if (name.charAt(i) < 32) throw new IOException("Control character in asset name");
                File target = new File(destination, name);
                if (!target.getCanonicalPath().startsWith(dataRoot.getCanonicalPath() + File.separator))
                    throw new IOException("Asset path outside data directory");
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create asset folder");
                    copyTree(tree, childId, target, state, depth + 1);
                    continue;
                }
                String lower = name.toLowerCase(Locale.ROOT);
                if (lower.endsWith(".dll") || lower.endsWith(".exe") || lower.endsWith(".ex_") ||
                        lower.endsWith(".ex") || (!cursor.isNull(3) && cursor.getLong(3) > FILE_LIMIT)) { ++state.skipped; continue; }
                File temp = File.createTempFile("import-", ".part", destination);
                try {
                    Uri source = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                    long written = 0;
                    try (InputStream input = getContentResolver().openInputStream(source);
                         FileOutputStream output = new FileOutputStream(temp)) {
                        if (input == null) throw new IOException("Cannot open selected asset");
                        byte[] buffer = new byte[65536]; int n;
                        while ((n = input.read(buffer)) != -1) {
                            if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled");
                            written += n; state.bytes += n;
                            if (written > FILE_LIMIT || state.bytes > 2L * 1024 * 1024 * 1024)
                                throw new IOException("Asset or 2 GiB total import limit exceeded");
                            output.write(buffer, 0, n);
                        }
                        if (!cursor.isNull(3) && cursor.getLong(3) >= 0 && cursor.getLong(3) != written)
                            throw new IOException("Source asset size changed during import");
                        output.getFD().sync();
                    }
                    if (!temp.renameTo(target)) throw new IOException("Cannot commit imported asset");
                    ++state.files;
                } finally { if (temp.exists() && !temp.delete()) android.util.Log.w("Jerusalem", "Cannot remove partial import"); }
            }
        } catch (SecurityException error) { throw new IOException("Folder read access was denied", error); }
    }

    @Override protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }
}
