package org.jerusalem.engine;

import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.graphics.Bitmap;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.ByteArrayOutputStream;
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
    private static final int PICK_NAVIGATION = 101;
    private static final long FILE_LIMIT = 64L * 1024 * 1024;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status, subtitles;
    private Button importButton, browseButton;
    private File dataRoot;
    private String selectedAudioPath, selectedMoviePath, pendingAudioPath;
    private String lastNativeStatus = "";
    private boolean importing, menuVisible=true, developerMode, menuArtLoading, menuArtLoaded, menuArtAttempted, engineReadySeen;
    private boolean subtitlesEnabled=true;
    private LinearLayout controlsPanel;
    private View developerRow, gameRow;
    private FrameLayout menuOverlay;
    private ImageView menuImage;
    private TextView menuStatus;
    private Button continueButton, saveGameButton;
    private String awaitedAction, previousResult="";
    private final java.util.ArrayList<Button> menuButtons = new java.util.ArrayList<>();
    private final Handler statusHandler = new Handler(Looper.getMainLooper());
    private final Runnable refreshStatus = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed() || mBrokenLibraries || status == null) return;
            if (!importing) {
                String caption = nativeSubtitle();
                if (!caption.contentEquals(subtitles.getText())) subtitles.setText(caption);
                int captionVisibility = caption.isEmpty() || !subtitlesEnabled ? View.GONE : View.VISIBLE;
                if (subtitles.getVisibility() != captionVisibility) subtitles.setVisibility(captionVisibility);
                String message = nativeStatus();
                if (!message.equals(lastNativeStatus)) {
                    if (!engineReadySeen && message.startsWith("Ready")) {engineReadySeen = true;menuArtAttempted = false;}
                    lastNativeStatus = message;
                    status.setText(message);
                }
            }
            if (menuOverlay != null) {
                int flags = nativeGameFlags();
                boolean saved = storyFile(false).isFile() || storyFile(true).isFile();
                if (awaitedAction != null) {
                    String reply = nativeGameResult();
                    if (!reply.equals(previousResult) && reply.contains(":" + awaitedAction + ":")) {
                        boolean success = reply.endsWith(":ok");
                        String action = awaitedAction; awaitedAction = null;
                        menuStatus.setText(nativeStatus());
                        if (success && !action.equals("save")) { developerMode = false; closeMenu(); }
                    }
                }
                // Compute the final state once. Enabling all controls and then
                // disabling Continue/Save on every poll prevents accessibility idle.
                boolean ready = !importing && awaitedAction == null;
                boolean active = (flags & 1) != 0 && (flags & 6) == 0;
                for (Button action : menuButtons) {
                    boolean enabled = ready && (action == continueButton ? active || saved : action == saveGameButton ? active : true);
                    if (action.isEnabled() != enabled) action.setEnabled(enabled);
                }
                if (menuVisible && !menuArtLoading && !menuArtLoaded && !menuArtAttempted && lastNativeStatus.startsWith("Ready")) loadMenuArt();
            }
            statusHandler.postDelayed(this, 250);
        }
    };
    private static native byte[] nativeDescribe(String path);
    private static native void nativeOpen(String path);
    private static native boolean nativeSupportsAudio(String path);
    private static native boolean nativeSupportsCinema(String path);
    private static native void nativeCommand(int command);
    private static native String nativeStatus();
    private static native String nativeInventory();
    private static native String nativeInventoryOptions();
    private static native void nativeCombine(int first, int second);
    private static native void nativeSelectItem(int item);
    private static native String nativeSubtitle();
    private static native byte[] nativeExtractA01(byte[] program);
    private static native byte[] nativeExtractStage(byte[] program);
    private static native void nativeMenuPaused(boolean paused);
    private static native void nativeDeveloperMode(boolean enabled);
    private static native int nativeGameFlags();
    private static native String nativeGameResult();
    private static native int[] nativeMenuArt();
    private static native void nativeViewportBottom(int pixels);
    private static native void nativeOptions(int volume, boolean markers);

    @Override protected String[] getLibraries() { return new String[] {"SDL3", "main"}; }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) return;
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        dataRoot = new File(getFilesDir(), "game-data");
        if (!dataRoot.isDirectory() && !dataRoot.mkdirs()) {
            showText("Storage error", "Cannot create private game-data directory.");
            return;
        }
        subtitlesEnabled = getPreferences(MODE_PRIVATE).getBoolean("subtitles", true);
        nativeOptions(getPreferences(MODE_PRIVATE).getInt("volume", 100), getPreferences(MODE_PRIVATE).getBoolean("markers", true));
        LinearLayout panel = new LinearLayout(this); controlsPanel = panel;
        panel.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> { if (view.getVisibility() == View.VISIBLE) nativeViewportBottom(bottom-top); });
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(0xEE18222B);
        panel.setPadding(12, 4, 12, 4);
        status = new TextView(this);
        status.setTextColor(0xFFFFFFFF);
        status.setText("Import original game data to begin.");
        subtitles = new TextView(this);subtitles.setTextSize(18);subtitles.setTextColor(0xFFFFFFFF);subtitles.setPadding(16, 8, 16, 8);subtitles.setVisibility(View.GONE);panel.addView(subtitles);
        panel.addView(status);
        LinearLayout buttons = new LinearLayout(this);
        importButton = button("Import data", v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, PICK_TREE);
        });
        browseButton = button("Browse assets", v -> browse(dataRoot));
        buttons.addView(button("Menu", v -> openMenu()));
        buttons.addView(importButton); buttons.addView(browseButton);
        buttons.addView(button("Play audio", v -> {
            if (selectedAudioPath == null) showText("Audio", "Choose a supported PCM WAV, WAC or WAS from Browse assets first.");
            else playWhenFocused(selectedAudioPath);
        }));
        buttons.addView(button("Replay movie", v -> {
            if (selectedMoviePath == null) showText("Cinema", "Choose a supported CI2 movie from Browse assets first.");
            else openWhenFocused(selectedMoviePath, "Starting movie…");
        }));
        buttons.addView(button("Preview animation", v -> nativeCommand(6)));
        Button navigation = button("A01 navigation", v -> {
            if (importing) return;
            if (new File(dataRoot, "a01-navigation.tsv").isFile()) nativeCommand(7);
            else chooseNavigationSource();
        });
        navigation.setOnLongClickListener(v -> { if (!importing) chooseNavigationSource(); return true; });
        buttons.addView(navigation);
        buttons.addView(button("Inventory", v -> showInventory()));
        buttons.addView(button("Start A01 story", v -> { if (!importing) nativeCommand(11); }));
        buttons.addView(button("Save checkpoint", v -> { if (!importing) nativeCommand(9); }));
        buttons.addView(button("Load checkpoint", v -> { if (!importing) nativeCommand(10); }));
        buttons.addView(button("Fit image", v -> nativeCommand(1)));
        buttons.addView(button("Flat / 360", v -> nativeCommand(5)));
        buttons.addView(button("Zoom +", v -> nativeCommand(2)));
        buttons.addView(button("Zoom −", v -> nativeCommand(3)));
        buttons.addView(button("Stop audio", v -> nativeCommand(4)));
        buttons.addView(button("Status", v -> showText("Native status", nativeStatus())));
        android.widget.HorizontalScrollView row = new android.widget.HorizontalScrollView(this);
        row.addView(buttons); panel.addView(row); developerRow = row;
        LinearLayout playButtons = new LinearLayout(this);
        playButtons.addView(button("Menu", v -> openMenu()));
        playButtons.addView(button("Inventory", v -> showInventory()));
        panel.addView(playButtons); gameRow = playButtons; gameRow.setVisibility(View.GONE);
        android.widget.RelativeLayout.LayoutParams layout = new android.widget.RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        layout.addRule(android.widget.RelativeLayout.ALIGN_PARENT_BOTTOM);
        mLayout.addView(panel, layout);
        createMenu();openMenu();
    }

    private File storyFile(boolean automatic) {
        return new File(getFilesDir(), automatic ? "native-saves/a01-auto.jrs" : "native-saves/a01-story.jrs");
    }

    private void createMenu() {
        menuOverlay = new FrameLayout(this);menuOverlay.setBackgroundColor(0xFF21150F);
        menuImage = new ImageView(this);menuImage.setScaleType(ImageView.ScaleType.FIT_CENTER);
        menuImage.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        menuOverlay.addView(menuImage, new FrameLayout.LayoutParams(-1, -1));
        // The original bitmap has baked English labels. Cover its right side with
        // accessible native controls so unavailable original options never look active.
        LinearLayout columns = new LinearLayout(this);
        View decoration = new View(this); columns.addView(decoration, new LinearLayout.LayoutParams(0, -1, 0.38f));
        ScrollView scroll = new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(0xFF21150F);
        LinearLayout entries = new LinearLayout(this);entries.setOrientation(LinearLayout.VERTICAL);entries.setPadding(16, 20, 16, 16);
        TextView title = new TextView(this);title.setText("JERUSALEM");title.setTextColor(0xFFFFE3AE);title.setTextSize(25);entries.addView(title);
        TextView subtitle = new TextView(this);subtitle.setText("The Three Roads to the Holy Land");subtitle.setTextColor(0xFFE7D2B5);entries.addView(subtitle);
        menuStatus = new TextView(this);menuStatus.setText("Import your original game data, then set up the game to begin.");menuStatus.setTextColor(0xFFFFFFFF);menuStatus.setPadding(0, 12, 0, 12);entries.addView(menuStatus);
        entries.addView(button("New Game", v -> launchGameAction(11, "new")));
        continueButton = button("Continue", v -> {
            int flags = nativeGameFlags();
            if ((flags & 1) != 0 && (flags & 6) == 0) { developerMode = false;closeMenu(); }
            else launchGameAction(14, "load");
        });continueButton.setEnabled(false);entries.addView(continueButton);
        entries.addView(button("Load Game", v -> chooseStorySave()));
        saveGameButton = button("Save Game", v -> launchGameAction(12, "save"));saveGameButton.setEnabled(false);entries.addView(saveGameButton);
        entries.addView(button("Settings", v -> showSettings()));
        entries.addView(button("Import game data", v -> { if (!importing && awaitedAction == null) importButton.performClick(); }));
        entries.addView(button("Set up game", v -> { if (!importing && awaitedAction == null) chooseNavigationSource(); }));
        entries.addView(button("Developer Tools", v -> { developerMode = true;closeMenu(); }));
        entries.addView(button("Quit", v -> finish()));
        for (int i = 0; i < entries.getChildCount(); ++i) if (entries.getChildAt(i) instanceof Button) menuButtons.add((Button) entries.getChildAt(i));
        scroll.addView(entries);columns.addView(scroll, new LinearLayout.LayoutParams(0, -1, 0.62f));
        menuOverlay.addView(columns, new FrameLayout.LayoutParams(-1, -1));
        android.widget.RelativeLayout.LayoutParams params = new android.widget.RelativeLayout.LayoutParams(-1, -1);
        mLayout.addView(menuOverlay, params);
    }

    private void openMenu() {
        if (menuOverlay == null) return;
        menuVisible = true;nativeMenuPaused(true);nativeCommand(15);
        if (awaitedAction == null) {
            int flags = nativeGameFlags();
            menuStatus.setText((flags & 6) != 0 ? nativeStatus() : (flags & 1) != 0 ? "Game paused. Continue resumes your current game." :
                storyFile(false).isFile() || storyFile(true).isFile() ? "Continue or load a saved game." : "Import your original data, then choose Set up game and New Game.");
        }
        controlsPanel.setVisibility(View.GONE);menuOverlay.setVisibility(View.VISIBLE);menuOverlay.bringToFront();
        if (!menuArtLoaded && !menuArtLoading) loadMenuArt();
    }

    private void closeMenu() {
        if (awaitedAction != null) return;
        menuVisible = false;menuOverlay.setVisibility(View.GONE);controlsPanel.setVisibility(View.VISIBLE);
        developerRow.setVisibility(developerMode ? View.VISIBLE : View.GONE);gameRow.setVisibility(developerMode ? View.GONE : View.VISIBLE);
        status.setMaxLines(developerMode ? 8 : 2);nativeDeveloperMode(developerMode);nativeMenuPaused(false);
    }

    private void launchGameAction(int command, String action) {
        if (importing || awaitedAction != null) return;
        previousResult = nativeGameResult();awaitedAction = action;
        menuStatus.setText(action.equals("save") ? "Saving game…" : "Loading game…");
        nativeCommand(command);
    }

    private void chooseStorySave() {
        if (importing || awaitedAction != null) return;
        java.util.ArrayList<String> labels = new java.util.ArrayList<>();java.util.ArrayList<Integer> commands = new java.util.ArrayList<>();
        if (storyFile(false).isFile()) {labels.add("Manual save");commands.add(13);}
        if (storyFile(true).isFile()) {labels.add("Autosave");commands.add(16);}
        if (labels.isEmpty()) {showText("Load Game", "No story saves yet. Start a New Game first.");return;}
        new AlertDialog.Builder(this).setTitle("Load Game").setItems(labels.toArray(new String[0]), (dialog, index) -> launchGameAction(commands.get(index), "load"))
                .setNegativeButton("Cancel", null).show();
    }

    private void loadMenuArt() {
        menuArtLoading = true;menuArtAttempted = true;
        worker.execute(() -> {
            int[] pixels = nativeMenuArt();
            runOnUiThread(() -> {
                menuArtLoading = false;if (isFinishing() || isDestroyed() || pixels == null || pixels.length < 3) return;
                int width = pixels[0], height = pixels[1];
                if (width <= 0 || height <= 0 || (long) width*height != pixels.length-2) return;
                menuImage.setImageBitmap(Bitmap.createBitmap(pixels, 2, width, width, height, Bitmap.Config.ARGB_8888));menuArtLoaded = true;
            });
        });
    }

    private void showSettings() {
        LinearLayout settings = new LinearLayout(this);settings.setOrientation(LinearLayout.VERTICAL);settings.setPadding(24, 16, 24, 16);
        TextView volumeLabel = new TextView(this);volumeLabel.setText("Audio volume");settings.addView(volumeLabel);
        SeekBar volume = new SeekBar(this);volume.setMax(100);volume.setProgress(getPreferences(MODE_PRIVATE).getInt("volume", 100));settings.addView(volume);
        CheckBox markers = new CheckBox(this);markers.setText("Show hotspot markers");markers.setChecked(getPreferences(MODE_PRIVATE).getBoolean("markers", true));settings.addView(markers);
        CheckBox captions = new CheckBox(this);captions.setText("Show subtitles");captions.setChecked(subtitlesEnabled);settings.addView(captions);
        new AlertDialog.Builder(this).setTitle("Settings").setView(settings).setPositiveButton("Apply", (dialog, which) -> {
            subtitlesEnabled = captions.isChecked();getPreferences(MODE_PRIVATE).edit().putInt("volume", volume.getProgress()).putBoolean("markers", markers.isChecked()).putBoolean("subtitles", subtitlesEnabled).apply();
            nativeOptions(volume.getProgress(), markers.isChecked());
        }).setNegativeButton("Cancel", null).show();
    }

    @Override public void onBackPressed() {
        if (menuOverlay == null) {super.onBackPressed();return;}
        if (menuVisible && (nativeGameFlags() & 1) != 0 && awaitedAction == null) closeMenu();else openMenu();
    }

    private Button button(String label, View.OnClickListener action) {
        Button button = new Button(this);
        button.setText(label); button.setAllCaps(false); button.setContentDescription(label);
        button.setMinHeight((int) (48 * getResources().getDisplayMetrics().density + 0.5f)); button.setOnClickListener(action);
        return button;
    }

    private void showInventory() {
        new AlertDialog.Builder(this).setTitle("Inventory").setMessage(nativeInventory())
                .setPositiveButton("Combine items", (dialog, which) -> chooseIngredients())
                .setNeutralButton("Use item", (dialog, which) -> chooseUseItem())
                .setNegativeButton("Close", null).show();
    }

    private void chooseUseItem() {
        if (importing) return;
        String raw = nativeInventoryOptions();
        if (raw.isEmpty()) { showText("Use item", "Collect an item first."); return; }
        String[] entries = raw.trim().split("\n");
        int[] ids = new int[entries.length];String[] labels = new String[entries.length];
        for (int i = 0; i < entries.length; ++i) {
            String[] fields = entries[i].split("\t", 2);if (fields.length != 2) return;
            ids[i] = Integer.parseInt(fields[0]);labels[i] = fields[1];
        }
        new AlertDialog.Builder(this).setTitle("Use item").setItems(labels, (dialog, index) -> nativeSelectItem(ids[index]))
                .setNegativeButton("Cancel", null).show();
    }

    private void chooseIngredients() {
        if (importing) return;
        String raw = nativeInventoryOptions();
        if (raw.isEmpty()) { showText("Combine items", "Collect at least two items first."); return; }
        String[] entries = raw.trim().split("\n");
        if (entries.length < 2) { showText("Combine items", "Collect at least two items first."); return; }
        int[] ids = new int[entries.length];
        String[] labels = new String[entries.length];
        for (int i = 0; i < entries.length; ++i) {
            String[] fields = entries[i].split("\t", 2);
            if (fields.length != 2) return;
            ids[i] = Integer.parseInt(fields[0]); labels[i] = fields[1];
        }
        new AlertDialog.Builder(this).setTitle("First ingredient").setItems(labels, (dialog, first) -> {
            String[] others = new String[labels.length - 1];
            int[] otherIds = new int[others.length];
            for (int i = 0, j = 0; i < labels.length; ++i) if (i != first) {
                others[j] = labels[i]; otherIds[j++] = ids[i];
            }
            new AlertDialog.Builder(this).setTitle("Combine " + labels[first] + " with…")
                    .setItems(others, (secondDialog, second) -> nativeCombine(ids[first], otherIds[second]))
                    .setNegativeButton("Cancel", null).show();
        }).setNegativeButton("Cancel", null).show();
    }

    private void showText(String title, String message) {
        if (isFinishing() || isDestroyed()) return;
        TextView text = new TextView(this);
        text.setText(message); text.setTextIsSelectable(true); text.setPadding(24, 16, 24, 16);
        ScrollView scroll = new ScrollView(this); scroll.addView(text);
        new AlertDialog.Builder(this).setTitle(title).setView(scroll).setPositiveButton("Close", null).show();
    }

    private void playWhenFocused(String path) {
        openWhenFocused(path, "Starting audio… Use the phone's media-volume controls if it is quiet.");
    }

    private void openWhenFocused(String path, String message) {
        pendingAudioPath = path;
        lastNativeStatus = "";
        status.setText(message);
        if (hasWindowFocus()) {
            pendingAudioPath = null;
            nativeOpen(path);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && !mBrokenLibraries && pendingAudioPath != null) {
            String path = pendingAudioPath;
            pendingAudioPath = null;
            nativeOpen(path);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        if (!mBrokenLibraries && status != null) {
            statusHandler.removeCallbacks(refreshStatus);
            statusHandler.post(refreshStatus);
        }
    }

    @Override protected void onPause() {
        if (!mBrokenLibraries) nativeCommand(15);
        statusHandler.removeCallbacks(refreshStatus);
        pendingAudioPath = null;
        super.onPause();
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
                    String description = new String(result, Charset.forName(selected.getName().toUpperCase(Locale.ROOT).endsWith(".DIA") ? "UTF-8" : "windows-1252"));
                    boolean playableAudio = nativeSupportsAudio(selected.getAbsolutePath());
                    boolean playableMovie = nativeSupportsCinema(selected.getAbsolutePath());
                    runOnUiThread(() -> {
                        if (isFinishing() || isDestroyed()) return;
                        pendingAudioPath = null;
                        selectedAudioPath = playableAudio ? selected.getAbsolutePath() : null;
                        selectedMoviePath = playableMovie ? selected.getAbsolutePath() : null;
                        if (playableMovie) {
                            openWhenFocused(selectedMoviePath, "Starting movie…");
                        } else if (playableAudio) {
                            // A modal report would pause SDL while audio is being started.
                            playWhenFocused(selectedAudioPath);
                        } else {
                            nativeOpen(selected.getAbsolutePath());
                            status.setText(selected.getName() + ": " + description.split("\\r?\\n", 2)[0]);
                            showText(selected.getName(), description);
                        }
                    });
                } catch (Exception error) {
                    runOnUiThread(() -> showText("Inspection failed", error.toString()));
                }
            });
        }).setNegativeButton("Close", null).show();
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent result) {
        super.onActivityResult(requestCode, resultCode, result);
        if (resultCode != RESULT_OK || result == null || result.getData() == null) return;
        if (requestCode == PICK_NAVIGATION) { importNavigation(result.getData()); return; }
        if (requestCode != PICK_TREE) return;
        Uri tree = result.getData();
        importing = true;
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
                importing = false;
                status.setText(finalMessage); importButton.setEnabled(true); browseButton.setEnabled(true);
                menuArtAttempted = false;if (menuVisible) { menuStatus.setText(finalMessage);loadMenuArt(); }
            }});
        });
    }

    private void chooseNavigationSource() {
        new AlertDialog.Builder(this).setTitle("Set up game")
                .setMessage("Choose Jerusalem.ex_ or Jerusalem.exe from your original disc. The app reads its A01 scene connections and ambient sound bindings without running or retaining the Windows program.")
                .setPositiveButton("Choose file", (dialog, which) -> {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.setType("*/*"); intent.addCategory(Intent.CATEGORY_OPENABLE);
                    startActivityForResult(intent, PICK_NAVIGATION);
                }).setNegativeButton("Cancel", null).show();
    }

    private void importNavigation(Uri source) {
        importing = true; importButton.setEnabled(false); browseButton.setEnabled(false);
        status.setText("Reading original A01 scene connections…");
        worker.execute(() -> {
            boolean success = false;
            String message;
            File temporary = null;
            try {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                try (InputStream input = getContentResolver().openInputStream(source)) {
                    if (input == null) throw new IOException("Cannot open selected program");
                    byte[] buffer = new byte[16384]; int read;
                    while ((read = input.read(buffer)) != -1) {
                        if (bytes.size() + read > 1024 * 1024)
                            throw new IOException("Choose Jerusalem.ex_ or Jerusalem.exe, not a disc image");
                        bytes.write(buffer, 0, read);
                    }
                }
                byte[] program = bytes.toByteArray();
                byte[] stage = nativeExtractStage(program);
                byte[] model = nativeExtractA01(program);
                if (model == null) throw new IOException("Native scene extraction returned no model");
                temporary = File.createTempFile("navigation-", ".part", dataRoot);
                try (FileOutputStream output = new FileOutputStream(temporary)) {
                    output.write(model); output.getFD().sync();
                }
                if (!temporary.renameTo(new File(dataRoot, "a01-navigation.tsv"))) throw new IOException("Cannot save scene connections");
                if (stage == null) throw new IOException("Native story extraction returned no model");
                temporary = File.createTempFile("stage-", ".part", dataRoot);
                try (FileOutputStream output = new FileOutputStream(temporary)) { output.write(stage); output.getFD().sync(); }
                if (!temporary.renameTo(new File(dataRoot, "a01-stage.tsv"))) throw new IOException("Cannot save story layouts");
                success = true; message = "Original A01 connections and story layouts are ready.";
            } catch (Exception error) { message = "A01 extraction failed: " + error.getMessage(); }
            finally { if (temporary != null && temporary.exists() && !temporary.delete())
                android.util.Log.w("Jerusalem", "Cannot remove partial navigation import"); }
            final boolean ready = success;
            final String resultMessage = message;
            runOnUiThread(() -> { if (!isDestroyed()) {
                importing = false; importButton.setEnabled(true); browseButton.setEnabled(true);
                status.setText(resultMessage);
                if (ready) { if (!menuVisible) nativeCommand(8);else menuStatus.setText("Game connections ready. Choose New Game."); }
                else showText("A01 navigation", resultMessage);
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
        statusHandler.removeCallbacks(refreshStatus);
        worker.shutdownNow();
        super.onDestroy();
    }
}
