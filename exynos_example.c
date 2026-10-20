/*
 * exynos_example.c - BEISPIEL eines durchreichenden Vulkan-ICD im Stil von ExynosTools.
 *
 * Idee: Diese Bibliothek wird von der App als "Vulkan-Treiber" geladen, laedt selbst den
 * echten Samsung-Treiber (Backend) und reicht alle Aufrufe durch. Einzelne Aufrufe werden
 * abgefangen. Hier nur eine Demo: vkGetPhysicalDeviceProperties haengt " [ExynosExample]"
 * an den GPU-Namen und schreibt einen Logcat-Eintrag (Tag: ExynosExample).
 *
 * v0.2 (GTA-Profil): misst zusaetzlich pro Sekunde FPS, Frametime und wie viel davon in
 * vkQueuePresentKHR verbracht wird (Tag ExynosExample). Viel Zeit im Present = GPU/VSync
 * begrenzt; wenig = CPU (Emulation) begrenzt.
 *
 * KEIN echter Treiber, KEINE BCn-Emulation, ungetestet auf Hardware.
 */
#include <dlfcn.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include <vulkan/vulkan.h>

#define TAG "ExynosExample"
static void dolog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) dolog(__VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

#include <stdarg.h>
static void dolog(const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", buf);
    static const char *paths[] = {"/sdcard/Download/exynosgta.log", "/storage/emulated/0/Download/exynosgta.log", NULL};
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "a");
        if (f) { fprintf(f, "%s\n", buf); fclose(f); break; }
    }
}

typedef PFN_vkVoidFunction (*PFN_icdGIPA)(VkInstance, const char *);
typedef VkResult (*PFN_icdNegotiate)(uint32_t *);

static void *g_backend;
static PFN_icdGIPA g_gipa;
static PFN_vkGetPhysicalDeviceProperties g_real_props;

static void *open_backend(void) {
    if (g_backend) return g_backend;
    const char *env = getenv("EXYNOSEX_BACKEND"); /* optionaler Pfad zum Samsung-Treiber */
    LOG("open_backend: EXYNOSEX_BACKEND=%s", env ? env : "(nicht gesetzt)");
    if (env) {
        g_backend = dlopen(env, RTLD_NOW | RTLD_LOCAL);
        if (g_backend) return g_backend;
        LOG("dlopen(%s) fehlgeschlagen: %s", env, dlerror());
    }

    glob_t g; /* sucht den Samsung-Treiber in /vendor, ohne einen Dateinamen zu raten */
    int gr = glob("/vendor/lib64/hw/vulkan.*.so", 0, NULL, &g);
    LOG("glob /vendor/lib64/hw/vulkan.*.so -> Ergebnis %d", gr);
    if (gr == 0) {
        for (size_t i = 0; i < g.gl_pathc && !g_backend; i++) {
            if (strstr(g.gl_pathv[i], "ranchu") || strstr(g.gl_pathv[i], "pastel")) continue;
            g_backend = dlopen(g.gl_pathv[i], RTLD_NOW | RTLD_LOCAL);
            if (g_backend) LOG("Backend geladen: %s", g.gl_pathv[i]);
            else LOG("dlopen(%s) fehlgeschlagen: %s", g.gl_pathv[i], dlerror());
        }
        globfree(&g);
    }
    if (!g_backend) LOG("Kein Backend gefunden - EXYNOSEX_BACKEND setzen");
    return g_backend;
}

static PFN_icdGIPA backend_gipa(void) {
    if (g_gipa) return g_gipa;
    void *b = open_backend();
    if (!b) return NULL;
    g_gipa = (PFN_icdGIPA)dlsym(b, "vk_icdGetInstanceProcAddr");
    if (!g_gipa) g_gipa = (PFN_icdGIPA)dlsym(b, "vkGetInstanceProcAddr");
    LOG("Backend-GIPA: %s", g_gipa ? "gefunden" : "NICHT gefunden");
    return g_gipa;
}

static void VKAPI_CALL my_GetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
    g_real_props(pd, p);
    size_t n = strlen(p->deviceName);
    const char *sfx = " [ExynosExample]";
    if (n + strlen(sfx) < VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) strcat(p->deviceName, sfx);
    LOG("GPU: %s, API %u.%u", p->deviceName, VK_VERSION_MAJOR(p->apiVersion), VK_VERSION_MINOR(p->apiVersion));
}


static PFN_vkQueuePresentKHR g_real_present;
typedef PFN_vkVoidFunction (*PFN_gdpa)(VkDevice, const char *);
static PFN_gdpa g_real_gdpa;

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static VkResult VKAPI_CALL my_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    static double last, win_start, in_present, worst;
    static unsigned frames;
    double t0 = now_ms();
    VkResult r = g_real_present(q, pi);
    double t1 = now_ms();
    if (last > 0 && t1 - last > worst) worst = t1 - last;
    last = t1; in_present += t1 - t0; frames++;
    if (win_start == 0) win_start = t0;
    if (t1 - win_start >= 1000.0) {
        double span = t1 - win_start;
        LOG("FPS %.1f | avg %.1f ms | worst %.1f ms | im Present %.0f%%",
            frames * 1000.0 / span, span / frames, worst, 100.0 * in_present / span);
        win_start = t1; frames = 0; in_present = 0; worst = 0;
    }
    return r;
}

static PFN_vkVoidFunction VKAPI_CALL my_GetDeviceProcAddr(VkDevice d, const char *name) {
    PFN_vkVoidFunction fn = g_real_gdpa(d, name);
    if (fn && !strcmp(name, "vkQueuePresentKHR")) {
        g_real_present = (PFN_vkQueuePresentKHR)fn;
        return (PFN_vkVoidFunction)my_QueuePresentKHR;
    }
    return fn;
}

EXPORT VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *v) {
    void *b = open_backend();
    PFN_icdNegotiate f = b ? (PFN_icdNegotiate)dlsym(b, "vk_icdNegotiateLoaderICDInterfaceVersion") : NULL;
    if (f) return f(v);
    if (*v > 5) *v = 5;
    return VK_SUCCESS;
}

EXPORT PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance inst, const char *name) {
    PFN_icdGIPA real = backend_gipa();
    if (!real) return NULL;
    PFN_vkVoidFunction fn = real(inst, name);
    if (fn && !strcmp(name, "vkGetPhysicalDeviceProperties")) {
        g_real_props = (PFN_vkGetPhysicalDeviceProperties)fn;
        return (PFN_vkVoidFunction)my_GetPhysicalDeviceProperties;
    }
    if (fn && !strcmp(name, "vkGetDeviceProcAddr")) {
        g_real_gdpa = (PFN_gdpa)fn;
        return (PFN_vkVoidFunction)my_GetDeviceProcAddr;
    }
    if (fn && !strcmp(name, "vkQueuePresentKHR")) {
        g_real_present = (PFN_vkQueuePresentKHR)fn;
        return (PFN_vkVoidFunction)my_QueuePresentKHR;
    }
    return fn;
}

EXPORT PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance inst, const char *name) {
    return vk_icdGetInstanceProcAddr(inst, name);
}
