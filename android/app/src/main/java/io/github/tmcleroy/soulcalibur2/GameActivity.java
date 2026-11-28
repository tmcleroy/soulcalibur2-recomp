// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tommy McLeroy. Taken from cps2recomp's android/.
package io.github.tmcleroy.soulcalibur2;

import android.content.Intent;
import android.content.pm.PackageInfo;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

// The game: its library (libsoulcalibur2.so), loaded in a process of its own, whose main() SDL calls on its own thread with the
// arguments the launcher gave (LauncherActivity.startGame). What main() returns is the activity's result, which the
// launcher reads as a program's exit code. The process
// ends with the game: the library keeps the game's state in globals, which a second start in it would find used.
public class GameActivity extends SDLActivity {
    static final String EXTRA_LIBRARY = "library";   // the game's id: lib<id>.so
    static final String EXTRA_ARGUMENTS = "arguments";
    static final String EXTRA_FOLDER = "folder";     // the player's folder for the game: its current folder
    static final String EXTRA_LOG = "log";           // where its output goes (logs/game.log)

    // One activity for each process (AndroidManifest.xml)
    public static class Slot0 extends GameActivity {}
    public static class Slot1 extends GameActivity {}
    public static class Slot2 extends GameActivity {}
    public static class Slot3 extends GameActivity {}

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // (runtime/sc2_android.cpp reads these before anything else)
        Intent intent = getIntent();
        try {
            Os.setenv("RECOMP_FOLDER", text(intent.getStringExtra(EXTRA_FOLDER)), true);
            Os.setenv("RECOMP_LOG", text(intent.getStringExtra(EXTRA_LOG)), true);
            Os.setenv("SC2_SHADERS", unpackShaders().getPath(), true);
            testVariables();
        } catch (ErrnoException e) {
            Log.w("soulcalibur2", "setenv: " + e);
        }
        setResult(RESULT_FIRST_USER);
        super.onCreate(savedInstanceState);
    }

    // For tests: NAME=VALUE lines of env.txt in the app's folder on the shared storage (adb push), set for the game
    // as a computer's shell would (RECOMP_QUICK_MATCH, RECOMP_D3D_FRAME_DUMP and the like), and ARGS=..., more of
    // the program's arguments (--skip-intro). Nothing without the file.
    private String[] testArguments = new String[0];

    private void testVariables() throws ErrnoException {
        File file = new File(getExternalFilesDir(null), "env.txt");
        if (!file.isFile()) {
            return;
        }
        try (java.io.BufferedReader lines = new java.io.BufferedReader(new java.io.FileReader(file))) {
            for (String line; (line = lines.readLine()) != null;) {
                int equals = line.indexOf('=');
                if (line.startsWith("ARGS=")) {
                    testArguments = line.substring(5).trim().split("\\s+");
                } else if (equals > 0 && !line.startsWith("#")) {
                    Os.setenv(line.substring(0, equals).trim(), line.substring(equals + 1).trim(), true);
                }
            }
        } catch (java.io.IOException e) {
            Log.w("soulcalibur2", "env.txt: " + e);
        }
    }

    // The Shader feature's presets (runtime/sc2_settings.cpp, ShaderPreset), read as files: the app's assets/shaders/
    // unpacked into its own folder, once for each version of the app
    private File unpackShaders() {
        File shaders = new File(getFilesDir(), "shaders");
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            File done = new File(shaders, ".unpacked-" + info.lastUpdateTime);
            if (!done.exists()) {
                deleteAll(shaders);
                unpack("shaders", shaders);
                done.createNewFile();
            }
        } catch (Exception e) {
            Log.w("soulcalibur2", "shaders: " + e);
        }
        return shaders;
    }

    private void unpack(String asset, File to) throws Exception {
        String[] inside = getAssets().list(asset);
        if (inside != null && inside.length > 0) {
            to.mkdirs();
            for (String name : inside) {
                unpack(asset + "/" + name, new File(to, name));
            }
            return;
        }
        try (InputStream in = getAssets().open(asset); OutputStream out = new FileOutputStream(to)) {
            byte[] buffer = new byte[1 << 16];
            for (int got; (got = in.read(buffer)) > 0;) {
                out.write(buffer, 0, got);
            }
        }
    }

    private static void deleteAll(File file) {
        File[] inside = file.listFiles();
        if (inside != null) {
            for (File one : inside) {
                deleteAll(one);
            }
        }
        file.delete();
    }

    private static String text(String s) {
        return s == null ? "" : s;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", text(getIntent().getStringExtra(EXTRA_LIBRARY))};
    }

    @Override
    protected String[] getArguments() {
        String[] arguments = getIntent().getStringArrayExtra(EXTRA_ARGUMENTS);
        if (arguments == null) {
            arguments = new String[0];
        }
        String[] all = java.util.Arrays.copyOf(arguments, arguments.length + testArguments.length);
        System.arraycopy(testArguments, 0, all, arguments.length, testArguments.length);
        return all;
    }

    // An HTTP request, from curl's arguments (recomp-common/launcher/src/run_program.cpp): Curl.java
    public String curl(String[] arguments) {
        return Curl.run(arguments);
    }

    // The game started over in the next process slot with these arguments (online play: a host whose match begins
    // starts afresh, as the program does elsewhere with execv: runtime/runner.cpp startAgain). The new one answers the
    // launcher in this one's place.
    public boolean startAgain(String[] arguments) {
        Intent intent = getIntent();
        final Intent again = new Intent(this, next());
        again.putExtra(EXTRA_LIBRARY, intent.getStringExtra(EXTRA_LIBRARY));
        again.putExtra(EXTRA_ARGUMENTS, arguments);
        again.putExtra(EXTRA_FOLDER, intent.getStringExtra(EXTRA_FOLDER));
        again.putExtra(EXTRA_LOG, intent.getStringExtra(EXTRA_LOG));
        again.addFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT);
        // (here, on the game's thread, not later on the activity's: the caller ends the process once this returns)
        try {
            startActivity(again);
        } catch (Exception e) {
            Log.w("soulcalibur2", "could not start again: " + e);
            return false;
        }
        finish();
        return true;
    }

    // The slot after this one (the launcher takes them in turn too)
    private Class<?> next() {
        Class<?>[] slots = {Slot0.class, Slot1.class, Slot2.class, Slot3.class};
        for (int i = 0; i < slots.length; i++) {
            if (slots[i] == getClass()) {
                return slots[(i + 1) % slots.length];
            }
        }
        return Slot0.class;
    }

    public void finishGame(int code) {
        setResult(RESULT_FIRST_USER + code);
        finish();
    }

    @Override
    protected void main() {
        String library = getMainSharedObject();
        int code = SDLActivity.nativeRunMain(library, getMainFunction(), getArguments());
        Log.v("soulcalibur2", getIntent().getStringExtra(EXTRA_LIBRARY) + " ended with " + code);
        // (RESULT_FIRST_USER and up: a process that ended some other way, a crash, gives RESULT_CANCELED)
        setResult(RESULT_FIRST_USER + code);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (isFinishing()) {
            System.exit(0);
        }
    }
}
