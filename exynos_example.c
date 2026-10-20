/*
 * exynos_example.c - BEISPIEL eines durchreichenden Vulkan-ICD im Stil von ExynosTools.
 *
 * Idee: Diese Bibliothek wird von der App als "Vulkan-Treiber" geladen, laedt selbst den
 * echten Samsung-Treiber (Backend) und reicht alle Aufrufe durch. Einzelne Aufrufe werden
 * abgefangen. Hier nur eine Demo: vkGetPhysicalDeviceProperties haengt " [ExynosExample]"
 * an den GPU-Namen und schreibt einen Logcat-Eintrag (Tag: ExynosExample).
 *
 * KEIN echter Treiber, KEINE BCn-Emulation, ungetestet auf Hardware.
 */
#include <dlfcn.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <vulkan/vulkan.h>

#define TAG "ExynosExample"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

typedef PFN_vkVoidFunction (*PFN_icdGIPA)(VkInstance, const char *);
typedef VkResult (*PFN_icdNegotiate)(uint32_t *);

static void *g_backend;
static PFN_icdGIPA g_gipa;
static PFN_vkGetPhysicalDeviceProperties g_real_props;

static void *open_backend(void) {
    if (g_backend) return g_backend;
    const char *env = getenv("EXYNOSEX_BACKEND"); /* optionaler Pfad zum Samsung-Treiber */
    if (env && (g_backend = dlopen(env, RTLD_NOW | RTLD_LOCAL))) return g_backend;

    glob_t g; /* sucht den Samsung-Treiber in /vendor, ohne einen Dateinamen zu raten */
    if (glob("/vendor/lib64/hw/vulkan.*.so", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && !g_backend; i++) {
            if (strstr(g.gl_pathv[i], "ranchu") || strstr(g.gl_pathv[i], "pastel")) continue;
            g_backend = dlopen(g.gl_pathv[i], RTLD_NOW | RTLD_LOCAL);
            if (g_backend) LOG("Backend: %s", g.gl_pathv[i]);
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
    return g_gipa;
}

static void VKAPI_CALL my_GetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
    g_real_props(pd, p);
    size_t n = strlen(p->deviceName);
    const char *sfx = " [ExynosExample]";
    if (n + strlen(sfx) < VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) strcat(p->deviceName, sfx);
    LOG("GPU: %s, API %u.%u", p->deviceName, VK_VERSION_MAJOR(p->apiVersion), VK_VERSION_MINOR(p->apiVersion));
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
    return fn;
}

EXPORT PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance inst, const char *name) {
    return vk_icdGetInstanceProcAddr(inst, name);
}
