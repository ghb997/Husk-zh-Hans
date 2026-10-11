/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Geode, the mod loader for Geometry Dash, loaded into the game the way its Android launcher does it.
 *
 * On a phone, Geode's launcher (com.geode.launcher) starts Geometry Dash itself: it loads the game's libraries, then
 * Geode.android64.so, whose JNI_OnLoad hooks the game, reads the mods from a folder and puts its own menus into the game's.
 * Here the game is already being started by the cocos2d-x driver; this adds the launcher's part, after the game's libraries
 * and before the first frame:
 *
 *   - the launcher's libc++_shared.so, which Geode and its mods are built against (from the launcher APK);
 *   - filesDir/game_version.txt, the game's versionCode, which is how Geode knows which game it is in;
 *   - org.fmod.FMOD's static fields, which the launcher's FMOD.init sets;
 *   - com.geode.launcher.utils.GeodeUtils, the launcher's Java that Geode calls back into (folders, clipboard, ...);
 *   - stores into the game's code, which Geode's hooks are, carried out through the JIT region's other view
 *     (husk-tl-codewrite.c);
 *   - Geode.android64.so itself: loaded from its file, constructors run, then JNI_OnLoad.
 *
 * Mods are Geode's business from there: it unpacks them and dlopens their libraries by path, which the loader serves from
 * the file (husk-tl-ld.c).
 */
#define _DARWIN_C_SOURCE
#include "husk-tl-geode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "husk-tl-bionic.h"
#include "husk-tl-codewrite.h"
#include "husk-tl-jni.h"
#include "husk-tl-ld.h"
#include "husk-tl-internal.h"

int tl_hle_version_code(void);

static struct {
    char so[1024], launcher_apk[1024], data[1024];   /* so: Geode.android64.so, or the release zip it comes in */
    int version_code;
    bool configured;
} G;

static jvalue vl(void *p) { jvalue v; v.j = 0; v.l = p; return v; }
static jvalue vi(int i) { jvalue v; v.j = 0; v.i = i; return v; }
static jvalue vz(int z) { jvalue v; v.j = 0; v.z = z != 0; return v; }
static void Noop(tl_jcall *c) { (void)c; }
static void RetFalse(tl_jcall *c) { c->ret = vz(0); }
static void RetTrue(tl_jcall *c) { c->ret = vz(1); }

void tl_geode_configure(const char *geode_so, const char *launcher_apk, const char *data_dir, int version_code)
{
    snprintf(G.so, sizeof(G.so), "%s", geode_so ? geode_so : "");
    snprintf(G.launcher_apk, sizeof(G.launcher_apk), "%s", launcher_apk ? launcher_apk : "");
    snprintf(G.data, sizeof(G.data), "%s", data_dir ? data_dir : "");
    G.version_code = version_code;
    G.configured = G.so[0] != 0;
}

bool tl_geode_configured(void) { return G.configured; }

/* Where Geode keeps the game's mods, resources and saves: the launcher's "base directory" (its media folder on a phone). */
static void base_dir(char *out, size_t n) { snprintf(out, n, "%s/geode", G.data); }
/* The launcher's filesDir: game_version.txt, and where Geode unpacks mods. */
static void internal_dir(char *out, size_t n) { snprintf(out, n, "%s/files", G.data); }

static void mkdirs(const char *path)
{
    char p[1024];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}

/* ---------------------------------------------------- the launcher's Java */

static void GU_baseDirectory(tl_jcall *c) { char p[1024]; base_dir(p, sizeof(p)); c->ret = vl(tl_jni_new_string(p)); }
static void GU_internalDirectory(tl_jcall *c) { char p[1024]; internal_dir(p, sizeof(p)); c->ret = vl(tl_jni_new_string(p)); }
static void GU_launchArguments(tl_jcall *c) { c->ret = vl(tl_jni_new_string("")); }
static void GU_launcherVersion(tl_jcall *c) { c->ret = vi(18); }    /* launcher 1.8 */
static void GU_readClipboard(tl_jcall *c) { c->ret = vl(tl_jni_new_string("")); }
/* Capabilities are things the launcher promises to do for Geode (forward input through its own callbacks, report resizes).
 * Husk does none of them, so it promises none: Geode then does that work itself, inside the game. */
static void GU_capability(tl_jcall *c)
{
    const char *what = tl_jni_string(c->args[0].l);
    tl_log_line("geode: asked whether the launcher supports \"%s\": no", what ? what : "?");
    c->ret = vz(0);
}
static void GU_openWebview(tl_jcall *c)
{
    const char *url = tl_jni_string(c->args[0].l);
    tl_log_line("geode: asked to open %s (not shown)", url ? url : "?");
}
static void GU_screenInsets(tl_jcall *c)
{
    jobj *a = tl_jni_new_prim_array('I', 4);
    memset(a->arr.data, 0, 4 * sizeof(int32_t));
    c->ret = vl(a);
}

#define GU "com/geode/launcher/utils/GeodeUtils"
#define M(n, s, f) { GU, n, s, f }
static const tl_jhle k_geode[] = {
    M("reportPlatformCapability", "(Ljava/lang/String;)Z", GU_capability),
    M("getBaseDirectory", "()Ljava/lang/String;", GU_baseDirectory),
    M("getInternalDirectory", "()Ljava/lang/String;", GU_internalDirectory),
    M("getLaunchArguments", "()Ljava/lang/String;", GU_launchArguments),
    M("getLauncherVersion", "()I", GU_launcherVersion),
    M("writeClipboard", "(Ljava/lang/String;)V", Noop),
    M("readClipboard", "()Ljava/lang/String;", GU_readClipboard),
    M("openWebview", "(Ljava/lang/String;)V", GU_openWebview),
    M("openFolder", "(Ljava/lang/String;)Z", RetFalse),
    M("selectFile", "(Ljava/lang/String;)Z", RetFalse),
    M("selectFiles", "(Ljava/lang/String;)Z", RetFalse),
    M("selectFolder", "(Ljava/lang/String;)Z", RetFalse),
    M("createFile", "(Ljava/lang/String;)Z", RetFalse),
    M("restartGame", "()V", Noop),
    M("getPermissionStatus", "(Ljava/lang/String;)Z", RetTrue),
    M("requestPermission", "(Ljava/lang/String;)V", Noop),
    M("getScreenInsets", "()[I", GU_screenInsets),
    { NULL, NULL, NULL, NULL }
};
#undef M

/* ------------------------------------------------------------------ loading */

/* Write one zip entry to a file, unless a file of that size is already there. */
static bool extract(tl_zip *z, const tl_zip_entry *e, const char *dest)
{
    struct stat st;
    if (stat(dest, &st) == 0 && (uint64_t)st.st_size == e->usize) return true;
    const uint8_t *data; size_t len; bool owned; char err[160];
    if (!tl_zip_data(z, e, (size_t)1 << 30, &data, &len, &owned, err, sizeof(err))) { tl_log_line("geode: %s", err); return false; }
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s.part", dest);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(data, 1, len, f) == len;
    if (f) fclose(f);
    if (owned) free((void *)data);
    return ok && rename(tmp, dest) == 0;
}

/*
 * A Geode release zip (geode-vX-android64.zip) holds the library and the loader's own resources (its menus, fonts, sounds).
 * The launcher puts the resources in <base>/game/geode/resources/geode.loader/; both are unpacked from the zip here, the
 * first time and whenever the release changes.
 */
static bool unpack_release(const char *zip_path, char *so_out, size_t n)
{
    tl_zip z; char err[160];
    if (!tl_zip_open(&z, zip_path, err, sizeof(err))) { tl_log_line("geode: %s", err); return false; }
    char bin[1024], res[1024], dest[1400];
    snprintf(bin, sizeof(bin), "%s/geode-bin", G.data);
    mkdirs(bin);
    base_dir(res, sizeof(res));
    strncat(res, "/game/geode/resources/geode.loader", sizeof(res) - strlen(res) - 1);
    mkdirs(res);
    bool have_so = false;
    for (size_t i = 0; i < z.count; i++) {
        const tl_zip_entry *e = &z.entries[i];
        size_t len = strlen(e->name);
        if (!len || e->name[len - 1] == '/' || strstr(e->name, "..")) continue;
        if (!strcmp(e->name, "Geode.android64.so")) {
            snprintf(so_out, n, "%s/Geode.android64.so", bin);
            have_so = extract(&z, e, so_out);
        } else if (!strncmp(e->name, "resources/", 10) && !strchr(e->name + 10, '/')) {
            snprintf(dest, sizeof(dest), "%s/%s", res, e->name + 10);
            extract(&z, e, dest);
        }
    }
    tl_zip_close(&z);
    if (!have_so) tl_log_line("geode: %s has no Geode.android64.so", zip_path);

    /* Since Geode 5 the loader's resources (its sprite sheets, fonts, sounds) are a release file of their own, resources.zip,
     * which the app downloads beside the release as geode-resources.zip. Without them Geode stops on its loading screen at
     * "Downloading Geode Resources" and tries GitHub itself. Its entries are flat: they go where the launcher puts them. */
    char rzip[1100];
    snprintf(rzip, sizeof(rzip), "%s", zip_path);
    char *slash = strrchr(rzip, '/');
    snprintf(slash ? slash + 1 : rzip, sizeof(rzip) - (size_t)(slash ? slash + 1 - rzip : 0), "geode-resources.zip");
    struct stat rs;
    if (stat(rzip, &rs) == 0 && tl_zip_open(&z, rzip, err, sizeof(err))) {
        int n = 0;
        for (size_t i = 0; i < z.count; i++) {
            const tl_zip_entry *e = &z.entries[i];
            size_t len = strlen(e->name);
            if (!len || e->name[len - 1] == '/' || strchr(e->name, '/') || strstr(e->name, "..")) continue;
            snprintf(dest, sizeof(dest), "%s/%s", res, e->name);
            if (extract(&z, e, dest)) n++;
        }
        tl_zip_close(&z);
        tl_log_line("geode: %d loader resource file(s) from %s", n, rzip);
    }
    return have_so;
}

bool tl_geode_load(jobj *activity)
{
    if (!G.configured) return true;
    char base[1024], internal[1024], path[1100];
    base_dir(base, sizeof(base));
    internal_dir(internal, sizeof(internal));
    snprintf(path, sizeof(path), "%s/game/geode/mods", base);
    mkdirs(path);
    mkdirs(internal);
    if (!G.version_code) G.version_code = tl_hle_version_code();      /* the game's own, from its manifest */
    size_t sl = strlen(G.so);
    if (sl > 4 && !strcmp(G.so + sl - 4, ".zip")) {
        char so[1100];
        if (!unpack_release(G.so, so, sizeof(so))) return false;
        snprintf(G.so, sizeof(G.so), "%s", so);
    }

    /* The game's versionCode, which is all Geode goes on to know which game it is in. */
    snprintf(path, sizeof(path), "%s/game_version.txt", internal);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d", G.version_code); fclose(f); }

    tl_jni_declare(GU, "java/lang/Object");
    tl_jni_register_hle(k_geode);

    /* FMOD.init(context): the launcher's first step, and where Geode's networking finds an Android context. */
    tl_jni_declare("org/fmod/FMOD", "java/lang/Object");
    tl_jni_set_static_field("org/fmod/FMOD", "gContext", "Landroid/content/Context;", vl(activity));
    tl_jni_set_static_field("org/fmod/FMOD", "INSTANCE", "Lorg/fmod/FMOD;", vl(tl_jni_new_object(tl_jni_class("org/fmod/FMOD"))));

    if (G.launcher_apk[0] && !tl_ld_add_apk(G.launcher_apk)) tl_log_line("geode: cannot read the launcher APK %s", G.launcher_apk);
    /* Without the launcher's C++ runtime Geode would load with hundreds of its imports missing and fall over at the first; the
     * game is better off starting without it. */
    if (!tl_ld_load("libc++_shared.so")) {
        tl_log_line("geode: no 64-bit libc++_shared.so in %s; starting the game without Geode", G.launcher_apk[0] ? G.launcher_apk : "(no launcher APK)");
        return false;
    }

    tl_codewrite_enable();
    tl_log_line("geode: loading %s (game versionCode %d)", G.so, G.version_code);
    tl_lib *L = tl_ld_load(G.so);
    if (!L || !tl_ld_init(L)) { tl_log_line("geode: %s could not be loaded", G.so); return false; }
    typedef int (*onload_fn)(void *vm, void *reserved);
    onload_fn onload = (onload_fn)tl_ld_sym(L, "JNI_OnLoad");
    if (!onload) { tl_log_line("geode: no JNI_OnLoad"); return false; }
    int v = onload(tl_jni_vm(), NULL);
    if (tl_jni_pending()) tl_log_line("geode: JNI_OnLoad left a Java exception pending");
    tl_log_line("geode: JNI_OnLoad -> %#x; %ld store(s) into code so far", v, tl_codewrite_count());
    return true;
}
