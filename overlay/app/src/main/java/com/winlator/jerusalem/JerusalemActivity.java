package com.winlator.jerusalem;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.preference.PreferenceManager;

import com.winlator.MainActivity;
import com.winlator.R;
import com.winlator.SettingsFragment;
import com.winlator.XServerDisplayActivity;
import com.winlator.container.Container;
import com.winlator.container.ContainerManager;
import com.winlator.core.AppUtils;
import com.winlator.core.DefaultVersion;
import com.winlator.core.DownloadProgressDialog;
import com.winlator.core.FileUtils;
import com.winlator.core.GPUInformation;
import com.winlator.core.PreloaderDialog;
import com.winlator.core.TarCompressorUtils;
import com.winlator.core.WineRegistryEditor;
import com.winlator.xenvironment.ImageFs;
import com.winlator.xenvironment.ImageFsInstaller;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.Locale;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Single-game front end: installs Wine on first run, imports the game discs,
 * prepares one dedicated Winlator container and launches Jerusalem in it.
 */
public class JerusalemActivity extends AppCompatActivity {
    private static final int REQUEST_IMPORT_IMAGE = 101;
    private static final int REQUEST_IMPORT_FOLDER = 102;

    private static final String PREF_CONTAINER_ID = "jerusalem_container_id";
    private static final String PREF_RENDERER = "jerusalem_renderer";
    private static final String PREF_SCREEN_SIZE = "jerusalem_screen_size";

    private static final String[] RENDERER_IDS = {"cnc-ddraw", "wined3d"};
    private static final String[] RENDERER_NAMES = {"CNC DDraw (recommended)", "WineD3D"};
    private static final String[] SCREEN_SIZES = {"640x480", "800x600", "1024x768"};
    private static final String DEFAULT_SCREEN_SIZE = "800x600";

    private SharedPreferences preferences;
    private TextView statusView;
    private Button playButton;
    private boolean systemReady = false;
    private boolean busy = false;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        preferences = PreferenceManager.getDefaultSharedPreferences(this);
        buildUi();
        ensureSystem();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshStatus();
    }

    // ---------------------------------------------------------------------- UI

    private void buildUi() {
        int pad = dp(24);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        column.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(R.string.jerusalem_title);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        column.addView(title, matchWrap(0));

        statusView = new TextView(this);
        statusView.setGravity(Gravity.CENTER);
        statusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        column.addView(statusView, matchWrap(dp(16)));

        playButton = addButton(column, R.string.jerusalem_play, v -> play());
        playButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        addButton(column, R.string.jerusalem_import_image, v -> pickImage());
        addButton(column, R.string.jerusalem_import_folder, v -> pickFolder());
        addButton(column, R.string.jerusalem_settings, v -> showSettings());

        TextView hint = new TextView(this);
        hint.setText(R.string.jerusalem_controls_hint);
        hint.setGravity(Gravity.CENTER);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        column.addView(hint, matchWrap(dp(24)));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(column, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
    }

    private Button addButton(LinearLayout parent, int textRes, android.view.View.OnClickListener onClick) {
        Button button = new Button(this);
        button.setText(textRes);
        button.setAllCaps(false);
        button.setOnClickListener(onClick);
        parent.addView(button, matchWrap(dp(12)));
        return button;
    }

    private LinearLayout.LayoutParams matchWrap(int topMargin) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = topMargin;
        return lp;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void refreshStatus() {
        if (statusView == null) return;
        boolean disc1 = GameImporter.isDiscImported(this, 1);
        boolean disc2 = GameImporter.isDiscImported(this, 2);
        if (disc1 && disc2) statusView.setText(R.string.jerusalem_status_ready_two);
        else if (disc1) statusView.setText(R.string.jerusalem_status_ready);
        else if (disc2) statusView.setText(getString(R.string.jerusalem_status_need_disc1, 2));
        else statusView.setText(R.string.jerusalem_status_no_game);
        playButton.setEnabled(disc1 && systemReady && !busy);
    }

    private void showError(String title, String message) {
        new AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, null)
            .show();
    }

    // ------------------------------------------------------- first-run set-up

    private void ensureSystem() {
        ImageFs imageFs = ImageFs.find(this);
        if (!imageFs.isValid() || imageFs.getVersion() < ImageFsInstaller.LATEST_VERSION) installImageFs(imageFs);
        else ensureContainer();
    }

    /** Same job as Winlator's ImageFsInstaller, but usable from this activity. */
    private void installImageFs(ImageFs imageFs) {
        busy = true;
        AppUtils.keepScreenOn(this);
        SettingsFragment.resetBox86_64Version(this);
        final File rootDir = imageFs.getRootDir();
        final DownloadProgressDialog dialog = new DownloadProgressDialog(this);
        dialog.show(R.string.installing_system_files);

        Executors.newSingleThreadExecutor().execute(() -> {
            // Keep containers (home/) across reinstalls, wipe everything else
            if (rootDir.isDirectory()) {
                File[] files = rootDir.listFiles();
                if (files != null) for (File f : files) if (!f.getName().equals("home")) FileUtils.delete(f);
            }
            else rootDir.mkdirs();

            final byte compressionRatio = 22;
            final long contentLength = (long) (FileUtils.getSize(this, "imagefs.txz") * (100.0f / compressionRatio));
            AtomicLong totalSize = new AtomicLong();
            boolean success = TarCompressorUtils.extract(TarCompressorUtils.Type.XZ, this, "imagefs.txz", rootDir, (file, size) -> {
                if (size > 0) {
                    int progress = (int) Math.min(100, (totalSize.addAndGet(size) * 100f) / contentLength);
                    runOnUiThread(() -> dialog.setProgress(progress));
                }
                return file;
            });
            if (success) imageFs.createImgVersionFile(ImageFsInstaller.LATEST_VERSION);

            runOnUiThread(() -> {
                dialog.close();
                busy = false;
                if (success) ensureContainer();
                else showError(getString(R.string.jerusalem_app_name), getString(R.string.unable_to_install_system_files));
            });
        });
    }

    private Container findContainer(ContainerManager manager) {
        int id = preferences.getInt(PREF_CONTAINER_ID, 0);
        return id > 0 ? manager.getContainerById(id) : null;
    }

    private void ensureContainer() {
        ContainerManager manager = new ContainerManager(this);
        if (findContainer(manager) != null) {
            systemReady = true;
            refreshStatus();
            return;
        }

        JSONObject data = new JSONObject();
        try {
            data.put("name", "Jerusalem");
            data.put("screenSize", preferences.getString(PREF_SCREEN_SIZE, DEFAULT_SCREEN_SIZE));
            data.put("graphicsDriver", defaultGraphicsDriver());
            data.put("dxwrapper", preferences.getString(PREF_RENDERER, RENDERER_IDS[0]));
            data.put("audioDriver", Container.DEFAULT_AUDIO_DRIVER);
            data.put("wincomponents", Container.DEFAULT_WINCOMPONENTS);
            data.put("drives", buildDrives());
            data.put("showFPS", false);
            data.put("wow64Mode", true);
        }
        catch (JSONException e) {
            return;
        }

        busy = true;
        PreloaderDialog preloader = new PreloaderDialog(this);
        preloader.show(R.string.jerusalem_preparing);
        manager.createContainerAsync(data, container -> {
            preloader.close();
            busy = false;
            if (container == null) {
                showError(getString(R.string.jerusalem_app_name), "Could not create the Windows environment.");
                return;
            }
            preferences.edit().putInt(PREF_CONTAINER_ID, container.id).apply();
            systemReady = true;
            refreshStatus();
        });
    }

    private String defaultGraphicsDriver() {
        String renderer = GPUInformation.getRenderer(this);
        boolean adreno = renderer != null && renderer.toLowerCase(Locale.ENGLISH).contains("adreno");
        return adreno ? "turnip" : "virgl";
    }

    // ------------------------------------------------------------------ import

    private void pickImage() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_IMPORT_IMAGE);
    }

    private void pickFolder() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_IMPORT_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) return;
        if (requestCode == REQUEST_IMPORT_IMAGE) runImport(data.getData(), false);
        else if (requestCode == REQUEST_IMPORT_FOLDER) runImport(data.getData(), true);
    }

    private void runImport(Uri uri, boolean folder) {
        busy = true;
        refreshStatus();
        AppUtils.keepScreenOn(this);
        final DownloadProgressDialog dialog = new DownloadProgressDialog(this);
        final GameImporter importer = new GameImporter(this, percent -> runOnUiThread(() -> dialog.setProgress(percent)));
        dialog.show(R.string.jerusalem_importing, importer::cancel);

        Executors.newSingleThreadExecutor().execute(() -> {
            String error = null;
            int disc = 0;
            try {
                disc = folder ? importer.importFolder(uri) : importer.importImage(uri);
            }
            catch (Exception e) {
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String finalError = error;
            final int finalDisc = disc;
            runOnUiThread(() -> {
                dialog.close();
                busy = false;
                refreshStatus();
                if (finalError != null) showError(getString(R.string.jerusalem_import_failed), finalError);
                else AppUtils.showToast(this, getString(R.string.jerusalem_import_done, finalDisc));
            });
        });
    }

    // -------------------------------------------------------------------- play

    private String buildDrives() {
        String drives = "D:" + GameImporter.getDiscDir(this, 1).getPath();
        if (GameImporter.isDiscImported(this, 2)) drives += "E:" + GameImporter.getDiscDir(this, 2).getPath();
        return drives;
    }

    private void play() {
        ContainerManager manager = new ContainerManager(this);
        Container container = findContainer(manager);
        if (container == null) {
            systemReady = false;
            ensureContainer();
            return;
        }
        if (!GameImporter.getExecutable(this).isFile()) {
            refreshStatus();
            return;
        }

        String renderer = preferences.getString(PREF_RENDERER, RENDERER_IDS[0]);
        String screenSize = preferences.getString(PREF_SCREEN_SIZE, DEFAULT_SCREEN_SIZE);
        container.setDrives(buildDrives());
        container.setDXWrapper(renderer);
        container.setScreenSize(screenSize);
        container.saveData();

        markDrivesAsCdrom(container);
        prepareCncDdrawConfig(container);
        File shortcut = writeShortcut(renderer, screenSize);

        Intent intent = new Intent(this, XServerDisplayActivity.class);
        intent.putExtra("container_id", container.id);
        intent.putExtra("shortcut_path", shortcut.getPath());
        startActivity(intent);
    }

    /** The game looks for its disc on a CD-ROM drive (GetDriveType), so tell Wine D:/E: are CD drives. */
    private void markDrivesAsCdrom(Container container) {
        File systemReg = new File(container.getRootDir(), ".wine/system.reg");
        if (!systemReg.isFile()) return; // first boot: Wine creates it; set on the next launch
        try (WineRegistryEditor editor = new WineRegistryEditor(systemReg)) {
            editor.setCreateKeyIfNotExist(true);
            editor.setStringValue("Software\\Wine\\Drives", "d:", "cdrom");
            if (GameImporter.isDiscImported(this, 2)) editor.setStringValue("Software\\Wine\\Drives", "e:", "cdrom");
        }
    }

    /** Pre-seeds cnc-ddraw's config with aspect-ratio-correct scaling. */
    private void prepareCncDdrawConfig(Container container) {
        File config = new File(container.getRootDir(), ".wine/drive_c/ProgramData/cnc-ddraw/ddraw.ini");
        if (config.isFile()) return;
        String assetDir = "dxwrapper/cnc-ddraw-" + DefaultVersion.CNC_DDRAW;
        FileUtils.copy(this, assetDir + "/ddraw.ini", config);
        String ini = FileUtils.readString(config);
        if (ini != null) FileUtils.writeString(config, ini.replace("maintas=false", "maintas=true"));
    }

    private File writeShortcut(String renderer, String screenSize) {
        File file = new File(GameImporter.getGameDir(this), "Jerusalem.desktop");
        // Four backslashes: Winlator's .desktop parser unescapes them down to one.
        String content = "[Desktop Entry]\n" +
            "Name=Jerusalem\n" +
            "Exec=env WINEPREFIX=\"" + ImageFs.WINEPREFIX + "\" wine D:\\\\\\\\Jerusalem.exe\n" +
            "Type=Application\n" +
            "StartupNotify=true\n" +
            "StartupWMClass=jerusalem.exe\n" +
            "\n[Extra Data]\n" +
            "forceFullscreen=1\n" +
            "dxwrapper=" + renderer + "\n" +
            "screenSize=" + screenSize + "\n";
        FileUtils.writeString(file, content);
        return file;
    }

    // ---------------------------------------------------------------- settings

    private void showSettings() {
        String[] items = {
            getString(R.string.jerusalem_renderer) + ": " + rendererName(preferences.getString(PREF_RENDERER, RENDERER_IDS[0])),
            getString(R.string.jerusalem_screen_size) + ": " + preferences.getString(PREF_SCREEN_SIZE, DEFAULT_SCREEN_SIZE),
            getString(R.string.jerusalem_advanced),
            getString(R.string.jerusalem_remove_game)
        };
        new AlertDialog.Builder(this)
            .setTitle(R.string.jerusalem_settings)
            .setItems(items, (d, which) -> {
                switch (which) {
                    case 0: chooseRenderer(); break;
                    case 1: chooseScreenSize(); break;
                    case 2: startActivity(new Intent(this, MainActivity.class)); break;
                    case 3: confirmRemoveGame(); break;
                }
            })
            .show();
    }

    private String rendererName(String id) {
        for (int i = 0; i < RENDERER_IDS.length; i++) if (RENDERER_IDS[i].equals(id)) return RENDERER_NAMES[i];
        return id;
    }

    private void chooseRenderer() {
        String current = preferences.getString(PREF_RENDERER, RENDERER_IDS[0]);
        int checked = current.equals(RENDERER_IDS[1]) ? 1 : 0;
        new AlertDialog.Builder(this)
            .setTitle(R.string.jerusalem_renderer)
            .setSingleChoiceItems(RENDERER_NAMES, checked, (d, which) -> {
                preferences.edit().putString(PREF_RENDERER, RENDERER_IDS[which]).apply();
                d.dismiss();
            })
            .show();
    }

    private void chooseScreenSize() {
        String current = preferences.getString(PREF_SCREEN_SIZE, DEFAULT_SCREEN_SIZE);
        int checked = 1;
        for (int i = 0; i < SCREEN_SIZES.length; i++) if (SCREEN_SIZES[i].equals(current)) checked = i;
        new AlertDialog.Builder(this)
            .setTitle(R.string.jerusalem_screen_size)
            .setSingleChoiceItems(SCREEN_SIZES, checked, (d, which) -> {
                preferences.edit().putString(PREF_SCREEN_SIZE, SCREEN_SIZES[which]).apply();
                d.dismiss();
            })
            .show();
    }

    private void confirmRemoveGame() {
        new AlertDialog.Builder(this)
            .setTitle(R.string.jerusalem_remove_game)
            .setMessage(R.string.jerusalem_remove_confirm)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok, (d, w) -> {
                FileUtils.delete(GameImporter.getGameDir(this));
                refreshStatus();
            })
            .show();
    }
}
