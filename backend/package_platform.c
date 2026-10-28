/* PS5 AppInstUtil ABI documented by ps5-payload-dev/websrv's PKGInstall and
 * itsPLK/ps5-pkg-manager's install_service.h. Orbit owns the worker/state machine;
 * no third-party installer implementation is embedded. */
#include "packages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ORBIT_DESKTOP
#include <dlfcn.h>
#include <ps5/kernel.h>
#include <unistd.h>
extern int sceKernelLoadStartModule(const char *, size_t, const void *, unsigned, void *, int *);
typedef struct {
    const char *uri, *extra_uri, *scenario, *content_id, *name, *icon;
    uint64_t reserved[8];
} PackageMetadata;
typedef struct { char content_id[48]; int type, platform; } PackageInfo;
typedef struct { char languages[30][8], scenarios[64][3], contents[64][48], reserved[6480]; } PlayGo;
typedef struct { int32_t code, version; char description[512], type[9]; } InstallError;
typedef struct {
    char state[16], source[8]; uint32_t remaining;
    uint64_t received, initial_chunk, total; uint32_t promote;
    InstallError error; int32_t local_percent; bool copy_only;
    char reserved[256];
} InstallStatus;
static int (*initialize)(void), (*terminate)(void);
static int (*install_package)(const PackageMetadata *, PackageInfo *, PlayGo *);
static int (*install_status)(const char *, InstallStatus *);
static bool initialized;
static void *symbol(const char *name, uint32_t handle) {
    void *value = dlsym(RTLD_DEFAULT, name);
    return value ? value : (void *)kernel_dynlib_dlsym(getpid(), handle, name);
}
static int prepare(void) {
    if (initialized) return 0;
    /* Optional API: a missing symbol must not prevent Orbit from starting. */
    int module = sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx", 0, NULL, 0, NULL, NULL);
    uint32_t handle = 0;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) && module >= 0) handle = (uint32_t)module;
    initialize = symbol("sceAppInstUtilInitialize", handle);
    terminate = symbol("sceAppInstUtilTerminate", handle);
    install_package = symbol("sceAppInstUtilInstallByPackage", handle);
    install_status = symbol("sceAppInstUtilGetInstallStatus", handle);
    if (!initialize || !terminate || !install_package || !install_status) return -1;
    int code = initialize();
    if (!code) initialized = true;
    return code;
}
bool package_platform_available(void) { return true; }
int package_platform_submit(const char *path, const char *name, const char *content_id,
                            char result_id[49], char *error, size_t cap) {
    int code = prepare();
    if (code) {
        snprintf(error, cap, "The PS5 package installer is unavailable (0x%08x). Check your jailbreak and try again.", (unsigned)code);
        return code;
    }
    char uri[1600];
    /* AppInstUtil's service sees /data through the system's /user mount. */
    if (snprintf(uri, sizeof uri, "%s%s", !strncmp(path, "/data/", 6) ? "/user" : "", path) >= (int)sizeof uri) return -1;
    PackageMetadata meta = {0}; PackageInfo info = {0}; PlayGo playgo = {0};
    meta.uri = uri; meta.extra_uri = ""; meta.scenario = "";
    meta.content_id = content_id; meta.name = name; meta.icon = "";
    code = install_package(&meta, &info, &playgo);
    if (code) {
        snprintf(error, cap, "The PS5 could not install this package (0x%08x). Check free space, package compatibility and your jailbreak. The downloaded file was kept.", (unsigned)code);
        return code;
    }
    info.content_id[sizeof info.content_id - 1] = 0;
    copy_text(result_id, 49, *info.content_id ? info.content_id : content_id);
    return 0;
}
int package_platform_poll(const char *content_id, PackageProgress *result) {
    memset(result, 0, sizeof *result);
    int code = prepare();
    if (code) return code;
    InstallStatus value = {0};
    code = install_status(content_id, &value);
    if (code) return code;
    value.state[sizeof value.state - 1] = 0;
    value.error.description[sizeof value.error.description - 1] = 0;
    copy_text(result->state, sizeof result->state, value.state);
    copy_text(result->message, sizeof result->message, value.error.description);
    result->received = value.received; result->total = value.total; result->error = value.error.code;
    return 0;
}
void package_platform_close(void) { if (initialized) terminate(); initialized = false; }
#else
/* No installation emulation in normal desktop builds. Test-only deterministic
 * service responses exercise the real receipt/API/worker without a console. */
bool package_platform_available(void) {
#ifdef ORBIT_TEST
    return getenv("ORBIT_TEST_PACKAGE_INSTALL") != NULL;
#else
    return false;
#endif
}
int package_platform_submit(const char *path, const char *name, const char *content_id,
                            char result_id[49], char *error, size_t cap) {
    (void)path; (void)name;
#ifdef ORBIT_TEST
    const char *mode = getenv("ORBIT_TEST_PACKAGE_INSTALL");
    if (mode && strcmp(mode, "reject")) {
        copy_text(result_id, 49, !strcmp(mode, "no-id") ? "" : *content_id ? content_id : "TEST-PACKAGE"); return 0;
    }
#else
    (void)content_id; (void)result_id;
#endif
    copy_text(error, cap, "The PS5 package installer is unavailable. The downloaded file was kept."); return -1;
}
int package_platform_poll(const char *id, PackageProgress *result) {
    (void)id; memset(result, 0, sizeof *result);
#ifdef ORBIT_TEST
    const char *mode = getenv("ORBIT_TEST_PACKAGE_INSTALL");
    if (!mode || !strcmp(mode, "lost")) return -1;
    copy_text(result->state, sizeof result->state, !strcmp(mode, "hold") ? "installing" : !strcmp(mode, "error") ? "error" : "completed");
    result->total = 4096; result->received = !strcmp(mode, "hold") ? 2048 : 4096;
    if (!strcmp(mode, "error")) result->error = -2;
    return 0;
#else
    return -1;
#endif
}
void package_platform_close(void) {}
#endif
