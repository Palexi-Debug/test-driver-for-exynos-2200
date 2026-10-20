/*
 * exynos_example.c - v0.5 (HAL-Passthrough fuer Samsung Xclipse, GTA-V-Profil)
 *
 * Erkenntnis aus dem Vergleich mit MdiEx/ExynosTools: Samsungs Vulkan-Treiber ist ein
 * Android-HAL-Modul. Er exportiert "HMI" (hw_module_t) und vk_icdGetInstanceProcAddrSG,
 * NICHT vk_icdGetInstanceProcAddr. Custom-Treiber-Loader (AdrenoTools) erwarten ebenfalls
 * ein HAL-Modul (Symbol "HMI"). v0.1-0.3 exportierten kein HMI -> keine Vulkan-Geraete.
 *
 * Diese Version exportiert HMI, laedt das echte Samsung-HAL als Backend, oeffnet dessen
 * Vulkan-Geraet ("vk0") und reicht alles durch. Abgefangen: vkGetPhysicalDeviceProperties
 * (Namenssuffix) und vkQueuePresentKHR (FPS/Frametime-Log).
 * Log: Logcat-Tag ExynosExample und Dateien (u.a. /sdcard/Download/exynosgta.log).
 * Ungetestet auf Hardware.
 */
#include <dlfcn.h>
#include <glob.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <android/log.h>
#include <vulkan/vulkan.h>

#define TAG "ExynosExample"
#define EXPORT __attribute__((visibility("default")))

static void dolog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) dolog(__VA_ARGS__)

static void dolog(const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", buf);
    static const char *paths[] = {
        "/sdcard/Download/exynosgta.log",
        "/storage/emulated/0/Download/exynosgta.log",
        "/sdcard/Android/data/app.gamenative/files/exynosgta.log",
        "/data/data/app.gamenative/files/exynosgta.log",
        "/data/user/0/app.gamenative/files/exynosgta.log",
        NULL};
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "a");
        if (f) { fprintf(f, "%s\n", buf); fclose(f); }
    }
}

__attribute__((constructor)) static void on_load(void) {
    LOG("== vulkan.exynosgta.so GELADEN (pid %d) ==", (int)getpid());
}

/* ---- Minimale Android-HAL-Strukturen (Layout wie hardware/hardware.h + hwvulkan.h, 64 Bit) ---- */
struct hw_module_t;
struct hw_device_t {
    uint32_t tag, version;
    struct hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *);
};
struct hw_module_methods_t {
    int (*open)(const struct hw_module_t *, const char *id, struct hw_device_t **dev);
};
struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    struct hw_module_methods_t *methods;
    void *dso;
    uintptr_t reserved[32 - 7];
};
typedef struct {
    struct hw_device_t common;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
} hwvulkan_device_t;

static void *g_backend;
static hwvulkan_device_t *g_real_dev;
static hwvulkan_device_t g_my_dev;

static void *open_backend(void) {
    if (g_backend) return g_backend;
    const char *env = getenv("EXYNOSEX_BACKEND");
    LOG("open_backend: EXYNOSEX_BACKEND=%s", env ? env : "(nicht gesetzt)");
    if (env) {
        g_backend = dlopen(env, RTLD_NOW | RTLD_LOCAL);
        if (g_backend) return g_backend;
        LOG("dlopen(%s) fehlgeschlagen: %s", env, dlerror());
    }
    glob_t g;
    int gr = glob("/vendor/lib64/hw/vulkan.*.so", 0, NULL, &g);
    LOG("glob /vendor/lib64/hw/vulkan.*.so -> %d", gr);
    if (gr == 0) {
        for (size_t i = 0; i < g.gl_pathc && !g_backend; i++) {
            const char *p = g.gl_pathv[i];
            if (strstr(p, "ranchu") || strstr(p, "pastel") || strstr(p, "exynosgta")) continue;
            g_backend = dlopen(p, RTLD_NOW | RTLD_LOCAL);
            if (g_backend) LOG("Backend geladen: %s", p);
            else LOG("dlopen(%s) fehlgeschlagen: %s", p, dlerror());
        }
        globfree(&g);
    }
    return g_backend;
}

/* ---- abgefangene Aufrufe ---- */
static PFN_vkGetPhysicalDeviceProperties g_real_props;
static PFN_vkQueuePresentKHR g_real_present;
typedef PFN_vkVoidFunction (*PFN_gdpa)(VkDevice, const char *);
static PFN_gdpa g_real_gdpa;

static void VKAPI_CALL my_GetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
    g_real_props(pd, p);
    const char *sfx = " [ExynosGTA]";
    if (strlen(p->deviceName) + strlen(sfx) < VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) strcat(p->deviceName, sfx);
    LOG("GPU: %s, API %u.%u", p->deviceName, VK_VERSION_MAJOR(p->apiVersion), VK_VERSION_MINOR(p->apiVersion));
}

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

static PFN_vkVoidFunction VKAPI_CALL my_GetInstanceProcAddr(VkInstance inst, const char *name) {
    PFN_vkVoidFunction fn = g_real_dev->GetInstanceProcAddr(inst, name);
    if (!fn) return fn;
    if (!strcmp(name, "vkGetPhysicalDeviceProperties")) {
        g_real_props = (PFN_vkGetPhysicalDeviceProperties)fn;
        return (PFN_vkVoidFunction)my_GetPhysicalDeviceProperties;
    }
    if (!strcmp(name, "vkGetDeviceProcAddr")) {
        g_real_gdpa = (PFN_gdpa)fn;
        return (PFN_vkVoidFunction)my_GetDeviceProcAddr;
    }
    if (!strcmp(name, "vkQueuePresentKHR")) {
        g_real_present = (PFN_vkQueuePresentKHR)fn;
        return (PFN_vkVoidFunction)my_QueuePresentKHR;
    }
    return fn;
}

extern struct hw_module_t HMI;
static int my_close(struct hw_device_t *d) { (void)d; return 0; }

static int my_open(const struct hw_module_t *m, const char *id, struct hw_device_t **out) {
    (void)m;
    LOG("HAL open(id=%s)", id ? id : "(null)");
    void *b = open_backend();
    if (!b) { LOG("FEHLER: kein Samsung-Backend gefunden"); return -1; }
    struct hw_module_t *bm = (struct hw_module_t *)dlsym(b, "HMI");
    if (!bm || !bm->methods || !bm->methods->open) { LOG("FEHLER: Backend hat kein HMI/open"); return -1; }
    struct hw_device_t *rd = NULL;
    int r = bm->methods->open(bm, id, &rd);
    LOG("Backend open -> %d, device=%p", r, (void *)rd);
    if (r != 0 || !rd) return r ? r : -1;
    g_real_dev = (hwvulkan_device_t *)rd;
    g_my_dev = *g_real_dev;                       /* Felder (Version, Tag, ...) uebernehmen */
    g_my_dev.common.module = &HMI;
    g_my_dev.common.close = my_close;
    g_my_dev.GetInstanceProcAddr = my_GetInstanceProcAddr;
    *out = &g_my_dev.common;
    return 0;
}

static struct hw_module_methods_t g_methods = { .open = my_open };

EXPORT struct hw_module_t HMI = {
    .tag = ('H' << 24) | ('W' << 16) | ('M' << 8) | 'T',
    .module_api_version = 1,
    .hal_api_version = 0,
    .id = "vulkan",
    .name = "ExynosGTA Vulkan passthrough",
    .author = "example",
    .methods = &g_methods,
};
