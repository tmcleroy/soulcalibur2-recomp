// SPDX-License-Identifier: GPL-3.0-or-later
// Android: the game is a library (libsoulcalibur2.so) that its activity (android/, GameActivity.java) loads in a
// process of its own and calls the main of, with the launcher's arguments. What a program started elsewhere gets from
// the launcher, the activity gives as variables: RECOMP_FOLDER, the player's folder for the game (the current folder
// elsewhere), and RECOMP_LOG, the file its output goes to (logs/game.log). Without a log the output goes to the
// system's log (adb logcat -s soulcalibur2). Taken from cps2recomp's port/android.cpp.
#ifdef __ANDROID__
#include "sc2_android.h"
#include "sc2_settings.h"
#include "sc2_touch.h"
#include "host_call.h"
#include "audio_output.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <dlfcn.h>
#include <signal.h>
#include <unwind.h>
#include <sched.h>

#include <algorithm>
#include <vector>
#include <fcntl.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

// The standard output and error, a line at a time to the system's log
void toSystemLog()
{
    static int pipes[2];
    if (pipe(pipes) != 0) return;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipes[1], 1);
    dup2(pipes[1], 2);
    std::thread([] {
        std::string line;
        char buffer[512];
        ssize_t got;
        while ((got = read(pipes[0], buffer, sizeof buffer)) > 0) {
            for (ssize_t i = 0; i < got; i++) {
                if (buffer[i] == '\n') {
                    __android_log_write(ANDROID_LOG_INFO, "soulcalibur2", line.c_str());
                    line.clear();
                } else {
                    line += buffer[i];
                }
            }
        }
    }).detach();
}

} // namespace

// SDL's failed checks: written to the log (SDL has already said which) and passed over, not shown in a dialog, which
// would be made through Java from a guest fiber's stack and stop the game there (host_call.h)
static SDL_AssertState SDLCALL passAssertion(const SDL_AssertData *data, void *)
{
    std::fprintf(stderr, "SDL check failed (passed over): %s at %s:%d\n", data->condition, data->filename,
        data->linenum);
    return SDL_ASSERTION_IGNORE;
}

// A crash's signal and where it was, written to the game's log (the system's own report goes only to adb logcat),
// then the system's handling as before
struct CrashWalk { int count; };
static _Unwind_Reason_Code crashFrame(struct _Unwind_Context *context, void *argument)
{
    CrashWalk *walk = static_cast<CrashWalk *>(argument);
    if (walk->count++ >= 40) return _URC_END_OF_STACK;
    const uintptr_t pc = _Unwind_GetIP(context);
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(pc), &info) && info.dli_fname) {
        const char *file = std::strrchr(info.dli_fname, '/');
        dprintf(2, "  #%02d %s+0x%zx %s\n", walk->count - 1, file ? file + 1 : info.dli_fname,
            static_cast<size_t>(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)), info.dli_sname ? info.dli_sname : "");
    } else {
        dprintf(2, "  #%02d 0x%zx\n", walk->count - 1, static_cast<size_t>(pc));
    }
    return _URC_NO_REASON;
}

static void crashed(int signal, siginfo_t *info, void *)
{
    dprintf(2, "soulcalibur2: crashed, signal %d at address %p\n", signal, info ? info->si_addr : nullptr);
    CrashWalk walk{0};
    _Unwind_Backtrace(crashFrame, &walk);
    ::signal(signal, SIG_DFL);
    raise(signal);
}

// The game's thread on the phone's fast cores only (those whose top speed is above the slowest's: not the
// efficiency cores); the threads it starts later inherit it, the sound's among them
static void fastCores()
{
    const int count = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
    std::vector<long> top(count, 0);
    long slowest = 0;
    for (int cpu = 0; cpu < count; ++cpu) {
        char path[96];
        std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
        if (std::FILE *file = std::fopen(path, "r")) {
            if (std::fscanf(file, "%ld", &top[cpu]) != 1) top[cpu] = 0;
            std::fclose(file);
        }
        if (top[cpu] > 0 && (slowest == 0 || top[cpu] < slowest)) slowest = top[cpu];
    }
    cpu_set_t fast;
    CPU_ZERO(&fast);
    int chosen = 0;
    for (int cpu = 0; cpu < count; ++cpu) {
        if (top[cpu] > slowest) {
            CPU_SET(cpu, &fast);
            ++chosen;
        }
    }
    if (chosen == 0) return; // (all alike, or unknown)
    if (sched_setaffinity(0, sizeof fast, &fast) == 0)
        std::fprintf(stderr, "soulcalibur2: on the %d fast cores of %d\n", chosen, count);
}

// (Not the fastest core alone by default: a phone caps that one first when it warms, below the next ones, and the
// game was then held on it. On the fast cores the system moves it to whichever is quickest at the time.
// SC2_FASTEST_CORE=1, for measuring: from the first frame the game's thread on the fastest core alone.)
static void fastestCore()
{
    const int count = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
    long fastest = 0;
    std::vector<long> top(count, 0);
    for (int cpu = 0; cpu < count; ++cpu) {
        char path[96];
        std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
        if (std::FILE *file = std::fopen(path, "r")) {
            if (std::fscanf(file, "%ld", &top[cpu]) != 1) top[cpu] = 0;
            std::fclose(file);
        }
        fastest = std::max(fastest, top[cpu]);
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    int chosen = 0;
    for (int cpu = 0; cpu < count; ++cpu) {
        if (fastest > 0 && top[cpu] == fastest) {
            CPU_SET(cpu, &set);
            ++chosen;
        }
    }
    if (chosen > 0 && chosen < count && sched_setaffinity(0, sizeof set, &set) == 0)
        std::fprintf(stderr, "soulcalibur2: the game's thread on the fastest core (SC2_FASTEST_CORE)\n");
}

// The cores' speed caps now (a warm phone lowers them), for the speed line
static std::string coreCaps()
{
    std::string caps;
    const int count = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
    long previous = -1;
    for (int cpu = 0; cpu < count; ++cpu) {
        char path[96];
        std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        long cap = 0;
        if (std::FILE *file = std::fopen(path, "r")) {
            if (std::fscanf(file, "%ld", &cap) != 1) cap = 0;
            std::fclose(file);
        }
        if (cap == previous) continue; // (a cluster's cores share one)
        previous = cap;
        char one[32];
        std::snprintf(one, sizeof one, "%scpu%d %.2f", caps.empty() ? "" : ", ", cpu, cap / 1e6);
        caps += one;
    }
    return caps + " GHz";
}

static void reportCrashes()
{
    static char stack[64 * 1024];
    stack_t alternate{};
    alternate.ss_sp = stack;
    alternate.ss_size = sizeof stack;
    sigaltstack(&alternate, nullptr);
    struct sigaction action{};
    action.sa_sigaction = crashed;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(signal, &action, nullptr);
}

static void sc2_android_on_exit()
{
    std::fflush(stdout);
    std::fflush(stderr);
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return;
    jclass activity_class = env->GetObjectClass(activity);
    if (activity_class) {
        jmethodID finish_game = env->GetMethodID(activity_class, "finishGame", "(I)V");
        if (finish_game) {
            env->CallVoidMethod(activity, finish_game, 0);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(activity_class);
    }
    env->DeleteLocalRef(activity);
}

void sc2_android_start()
{
    SDL_SetAssertionHandler(passAssertion, nullptr);
    // Landscape either way up, as the launcher (the manifest's SDL_ENV values don't reach the game's process), and
    // the back button for the game (it opens the menu) instead of closing it
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // SDL's systems that reach Java start here, on the activity's own thread's stack (host_call.h has the rest),
    // and the touch screen's controls with them
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC))
        __android_log_print(ANDROID_LOG_WARN, "soulcalibur2", "SDL: %s", SDL_GetError());
    sc2_touch_init();
    if (const char *folder = getenv("RECOMP_FOLDER"); folder && *folder) {
        if (chdir(folder) != 0) __android_log_print(ANDROID_LOG_WARN, "soulcalibur2", "no folder %s", folder);
    }
    // (not freopen: the standard output's descriptor is the system's, and fdsan stops a process that closes it)
    const char *log = getenv("RECOMP_LOG");
    const int file = log && *log ? open(log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
    if (file >= 0) {
        dup2(file, 1);
        dup2(file, 2);
        close(file);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IOLBF, 0);
    } else {
        toSystemLog();
    }
    std::atexit(sc2_android_on_exit);
    reportCrashes();
    fastCores();
    recomp_host_pump_start();
}

// The game's speed in its log every ten seconds: frames begun and the longest between two (a phone's own speed,
// for reports)
extern "C" void sc2_android_frame(void)
{
    static uint64_t window_start = 0, last = 0, longest = 0;
    static unsigned long long gaps_before = 0;
    static bool first_frame = true;
    static unsigned frames = 0;
    const uint64_t now = SDL_GetTicksNS();
    if (last != 0) longest = std::max(longest, now - last);
    last = now;
    ++frames;
    if (window_start == 0) window_start = now;
    if (first_frame) {
        first_frame = false;
        if (const char *pin = std::getenv("SC2_FASTEST_CORE"); pin != nullptr && *pin == '1') fastestCore();
    }
    if (now - window_start >= 10000000000ull) {
        const unsigned long long gaps = recomp_audio_output_underruns();
        std::fprintf(stderr, "soulcalibur2 speed: %.1f frames a second, longest frame %.1f ms, sound gaps %llu "
            "(caps %s)\n", frames * 1e9 / double(now - window_start), longest / 1e6, gaps - gaps_before,
            coreCaps().c_str());
        gaps_before = gaps;
        window_start = now;
        frames = 0;
        longest = 0;
    }
}

bool sc2_android_start_again(const std::vector<std::string> &args)
{
    // GameActivity.startAgain(String[]): the game in a new process with these arguments, its result the launcher's
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID start_again = activity_class ? env->GetMethodID(activity_class, "startAgain", "([Ljava/lang/String;)Z") : nullptr;
    if (!start_again) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (activity_class) env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
        return false;
    }
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray arguments = string_class ? env->NewObjectArray(static_cast<jsize>(args.size()), string_class, nullptr) : nullptr;
    if (!arguments) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (string_class) env->DeleteLocalRef(string_class);
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
        return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
        jstring arg = env->NewStringUTF(args[i].c_str());
        if (!arg) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(arguments);
            env->DeleteLocalRef(string_class);
            env->DeleteLocalRef(activity_class);
            env->DeleteLocalRef(activity);
            return false;
        }
        env->SetObjectArrayElement(arguments, static_cast<jsize>(i), arg);
        env->DeleteLocalRef(arg);
    }
    const bool started = env->CallBooleanMethod(activity, start_again, arguments) != JNI_FALSE;
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(arguments);
    env->DeleteLocalRef(string_class);
    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
    return started;
}

// For the launcher, which has no game program to run with --list-features: it opens the game's library and asks it
// (recomp-common/launcher/src/game_process.cpp). The text is the caller's to free.
extern "C" __attribute__((visibility("default"))) char *RecompListFeatures()
{
    return strdup(recomp::FeatureListText(sc2::Features()).c_str());
}
#endif
