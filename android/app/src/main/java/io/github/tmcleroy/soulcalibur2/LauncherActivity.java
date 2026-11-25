// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tommy McLeroy. Taken from cps2recomp's android/.
package io.github.tmcleroy.soulcalibur2;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

// The launcher (libsoulcalibur2-launcher.so: recomp-common's launcher). What a launcher does elsewhere by
// starting a program, it asks this activity for, from its own thread (recomp-common/launcher/src/game_process.cpp):
// startGame() starts a game's activity in the next process slot, and gameResult() says how it ended; importFiles()
// asks the player for a disc image with the system's picker and copies it where the game can open it, and
// importResult() says where; installPackage() hands a new version's APK, downloaded and checked, to Android's
// installer (recomp-common/launcher/src/update.cpp).
public class LauncherActivity extends SDLActivity {
    static final int STILL_RUNNING = Integer.MIN_VALUE;
    private static final Class<?>[] SLOTS = {
        GameActivity.Slot0.class, GameActivity.Slot1.class, GameActivity.Slot2.class, GameActivity.Slot3.class};
    private static final int REQUEST_GAME = 1;
    private static final int REQUEST_IMPORT = 2;
    private static final int REQUEST_INSTALLS = 3;

    private int nextSlot = 0;
    private volatile int result = 0;
    private volatile String imported = "";
    private volatile long importCopiedBytes = 0;
    private volatile long importTotalBytes = 0;
    private volatile String importErrorMessage = null;
    private String importFolder;
    private volatile String pendingInstall;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "soulcalibur2-launcher"};
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        unpackGame();
    }

    // A development build's own disc image and guests (tools/release/android.sh GAME_DISC: assets/game/), copied into
    // the launcher's folder once, away from the activity's thread (they are gigabytes), and the launcher set to them;
    // then the launcher starts again to find them
    private void unpackGame() {
        final String[] files;
        try {
            files = getAssets().list("game");
        } catch (Exception e) {
            return;
        }
        if (files == null || files.length == 0) {
            return;
        }
        final File home = new File(getExternalFilesDir(null), "Soulcalibur II");
        final File done = new File(home, "imported/.packed-" + packedVersion());
        if (done.exists()) {
            return;
        }
        android.widget.Toast.makeText(this, "Unpacking the game files packed in this build (a minute or two); "
            + "the launcher starts again when done", android.widget.Toast.LENGTH_LONG).show();
        new Thread(() -> {
            try {
                File imported = new File(home, "imported");
                File guest = new File(home, "guest");
                imported.mkdirs();
                guest.mkdirs();
                for (String name : files) {
                    File to = new File(name.equals("disc.iso") ? imported : guest, name);
                    File partial = new File(to.getPath() + ".part");
                    try (InputStream in = getAssets().open("game/" + name); OutputStream out = new FileOutputStream(partial)) {
                        byte[] buffer = new byte[1 << 20];
                        for (int got; (got = in.read(buffer)) > 0;) {
                            out.write(buffer, 0, got);
                        }
                    }
                    to.delete();
                    partial.renameTo(to);
                }
                // (launcher.ini: the game's disc, in its [game] section; the rest of the file kept)
                File ini = new File(home, "launcher.ini");
                String text = ini.exists() ? new String(java.nio.file.Files.readAllBytes(ini.toPath()),
                    java.nio.charset.StandardCharsets.UTF_8) : "";
                String disc = "disc=" + new File(imported, "disc.iso").getPath();
                StringBuilder out = new StringBuilder();
                boolean inGame = false, written = false;
                for (String line : text.split("\n", -1)) {
                    if (line.startsWith("[")) {
                        if (inGame && !written) {
                            out.append(disc).append('\n');
                            written = true;
                        }
                        inGame = line.trim().equals("[game]");
                    } else if (inGame && line.startsWith("disc=")) {
                        if (!written) {
                            out.append(disc).append('\n');
                            written = true;
                        }
                        continue;
                    }
                    out.append(line).append('\n');
                }
                if (!written) {
                    out.append(inGame ? "" : "[game]\n").append(disc).append('\n');
                }
                java.nio.file.Files.write(ini.toPath(), out.toString().replaceAll("\n+$", "\n")
                    .getBytes(java.nio.charset.StandardCharsets.UTF_8));
                done.createNewFile();
                Log.v("soulcalibur2", "unpacked the packed game files");
                // (the launcher again, in a new process: it reads launcher.ini when it starts)
                Intent again = getPackageManager().getLaunchIntentForPackage(getPackageName());
                again.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(again);
                System.exit(0);
            } catch (Exception e) {
                Log.w("soulcalibur2", "the packed game files: " + e);
            }
        }).start();
    }

    // This install's time, so a new build's files are unpacked again
    private long packedVersion() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        } catch (Exception e) {
            return 0;
        }
    }

    // Starts the game whose library is lib<library>.so, with these arguments, in its folder, its output to log
    public boolean startGame(String library, String[] arguments, String folder, String log) {
        final Intent intent = new Intent(this, SLOTS[nextSlot]);
        nextSlot = (nextSlot + 1) % SLOTS.length;
        intent.putExtra(GameActivity.EXTRA_LIBRARY, library);
        intent.putExtra(GameActivity.EXTRA_ARGUMENTS, arguments);
        intent.putExtra(GameActivity.EXTRA_FOLDER, folder);
        intent.putExtra(GameActivity.EXTRA_LOG, log);
        result = STILL_RUNNING;
        runOnUiThread(() -> startActivityForResult(intent, REQUEST_GAME));
        return true;
    }

    // An HTTP request, from curl's arguments (recomp-common/launcher/src/run_program.cpp): Curl.java
    public String curl(String[] arguments) {
        return Curl.run(arguments);
    }

    // A new version's APK (in the app's cache: UpdateProvider) opened with Android's installer, which asks the player
    // and installs it over this app: 0. Android lets an app open the installer only once the player allows it to
    // install apps, so the first time Android's Settings for that open instead, and the installer after them: 1.
    // -1: neither could be opened.
    public int installPackage(String path) {
        final File apk = new File(path);
        if (UpdateProvider.uri(this, apk) == null || !apk.isFile()) {
            return -1;
        }
        if (!getPackageManager().canRequestPackageInstalls()) {
            pendingInstall = path;
            final Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()));
            runOnUiThread(() -> {
                try {
                    startActivityForResult(settings, REQUEST_INSTALLS);
                } catch (Exception e) {
                    Log.w("soulcalibur2", "no Settings for installing apps: " + e);
                }
            });
            return 1;
        }
        return openInstaller(apk) ? 0 : -1;
    }

    private boolean openInstaller(File apk) {
        final Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(UpdateProvider.uri(this, apk), UpdateProvider.MIME);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        if (intent.resolveActivity(getPackageManager()) == null) {
            Log.w("soulcalibur2", "no installer for " + apk);
            return false;
        }
        runOnUiThread(() -> {
            try {
                startActivity(intent);
            } catch (Exception e) {
                Log.w("soulcalibur2", "the installer: " + e);
            }
        });
        return true;
    }

    // How the game ended: its main()'s value (-1: some other way), or STILL_RUNNING
    public int gameResult() {
        return result;
    }

    // The system's picker for files (several if `many`), each chosen copied into `folder` by its own name
    public void importFiles(String folder, boolean many) {
        importFolder = folder;
        imported = null;
        importCopiedBytes = 0;
        importTotalBytes = 0;
        importErrorMessage = null;
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, many);
        runOnUiThread(() -> {
            try {
                startActivityForResult(intent, REQUEST_IMPORT);
            } catch (Exception e) {
                Log.w("soulcalibur2", "could not open picker: " + e);
                importErrorMessage = e.getMessage() != null ? e.getMessage() : e.toString();
                imported = "";
            }
        });
    }

    // The copies' paths, one a line ("" for none), or null while the player chooses and they are copied
    public String importResult() {
        return imported;
    }

    public long importCopied() {
        return importCopiedBytes;
    }

    public long importTotal() {
        return importTotalBytes;
    }

    public String importError() {
        return importErrorMessage;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_INSTALLS) {
            // (back from Settings: the installer, if the player allowed it; otherwise the update card's Install asks again)
            String path = pendingInstall;
            pendingInstall = null;
            if (path != null && getPackageManager().canRequestPackageInstalls()) {
                openInstaller(new File(path));
            }
        } else if (requestCode == REQUEST_GAME) {
            result = resultCode >= RESULT_FIRST_USER ? resultCode - RESULT_FIRST_USER : -1;
        } else if (requestCode == REQUEST_IMPORT) {
            final List<Uri> chosen = new ArrayList<>();
            if (resultCode == RESULT_OK && data != null) {
                if (data.getClipData() != null) {
                    for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                        chosen.add(data.getClipData().getItemAt(i).getUri());
                    }
                } else if (data.getData() != null) {
                    chosen.add(data.getData());
                }
            }
            // (a disc image is gigabytes: copied away from the activity's thread)
            new Thread(() -> {
                StringBuilder paths = new StringBuilder();
                for (Uri uri : chosen) {
                    String path = copy(uri, importFolder);
                    if (path != null) {
                        paths.append(path).append('\n');
                    }
                }
                imported = paths.toString();
            }).start();
        }
    }

    // A document copied into a folder by its own name; its path there, or null
    private String copy(Uri uri, String folder) {
        String name = null;
        long total = 0;
        try (Cursor cursor = getContentResolver().query(uri,
                new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
                if (!cursor.isNull(1)) {
                    total = cursor.getLong(1);
                }
            }
        } catch (Exception e) {
            Log.w("soulcalibur2", "no name for " + uri + ": " + e);
        }
        importCopiedBytes = 0;
        importTotalBytes = total > 0 ? total : 0;
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment();
        }
        name = new File(name).getName();  // (never a path)
        File to = new File(folder, name);
        File partial = new File(folder, name + ".part");
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(partial)) {
            if (in == null) {
                throw new java.io.IOException("could not open " + name);
            }
            byte[] buffer = new byte[1 << 20];
            for (int got; (got = in.read(buffer)) > 0;) {
                out.write(buffer, 0, got);
                importCopiedBytes += got;
            }
            // (a document whose stream ends before its size: a cloud file or card read that stopped early)
            if (total > 0 && importCopiedBytes != total) {
                throw new java.io.IOException("only " + importCopiedBytes + " of " + name + "'s " + total
                        + " bytes could be read");
            }
        } catch (Exception e) {
            Log.w("soulcalibur2", "could not copy " + uri + ": " + e);
            if (importErrorMessage == null) {
                importErrorMessage = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            partial.delete();
            return null;
        }
        to.delete();
        if (!partial.renameTo(to)) {
            if (importErrorMessage == null) {
                importErrorMessage = "could not move " + name + " into " + folder;
            }
            partial.delete();
            return null;
        }
        return to.getPath();
    }
}
