#include "browser_platform.h"
#ifndef ORBIT_DESKTOP
#include <dlfcn.h>
#include <string.h>
#include <pthread.h>
#ifndef ORBIT_BROWSER_PROBE
#include "orbit.h"
#define STAGE(phase, status, result) diagnostics_stage(phase, status, result)
#else
#define STAGE(phase, status, result) ((void)(result))
#endif

static void *system_service, *user_service;
static pthread_mutex_t service_lock = PTHREAD_MUTEX_INITIALIZER;
static int load_services(void) {
    if (!system_service)
        system_service = dlopen("libSceSystemService.sprx", RTLD_NOW | RTLD_LOCAL);
    if (!system_service) return -1;
    if (!user_service)
        user_service = dlopen("libSceUserService.sprx", RTLD_NOW | RTLD_LOCAL);
    return user_service ? 0 : -2;
}
static int foreground_user(void) {
    if (load_services()) return -1;
    int (*initialize)(void *) = (int (*)(void *))dlsym(user_service, "sceUserServiceInitialize");
    int (*terminate)(void) = (int (*)(void))dlsym(user_service, "sceUserServiceTerminate");
    int (*foreground)(int *) = (int (*)(int *))dlsym(user_service, "sceUserServiceGetForegroundUser");
    if (!initialize || !terminate || !foreground) return -1;
    int initialized = initialize(NULL), user = -1;
    if (foreground(&user)) user = -1;
    if (!initialized) (void)terminate();
    /* Never fall back to a different initial/logged-in user. */
    return user;
}
int probe_browser_user(void) {
    pthread_mutex_lock(&service_lock);
    int user = foreground_user();
    pthread_mutex_unlock(&service_lock);
    return user;
}

/* Only the explicit browser-session worker calls this. Keep loaded handles for
 * the process lifetime: opening the browser is asynchronous. No native service
 * import, lookup or initialization belongs on Orbit's normal startup path. */
static int open_browser(const char *url) {
    if (!url || !*url) return -1;
    STAGE("browser-modules", "started", 0);
    int loaded = load_services();
    if (loaded) {
        STAGE("browser-modules", "error", loaded);
        return loaded;
    }
    int (*initialize)(void *) = (int (*)(void *))dlsym(user_service, "sceUserServiceInitialize");
    int (*terminate)(void) = (int (*)(void))dlsym(user_service, "sceUserServiceTerminate");
    int (*launch)(const char *, void *) =
        (int (*)(const char *, void *))dlsym(system_service, "sceSystemServiceLaunchWebBrowser");
    if (!initialize || !terminate || !launch) {
        STAGE("browser-modules", "error", -3);
        return -3;
    }
    STAGE("browser-modules", "ok", 0);
    STAGE("browser-user-service", "started", 0);
    int result = initialize(NULL);
    STAGE("browser-user-service", result ? "error" : "ok", result);
    if (result) return result;
    STAGE("browser-launch", "started", 0);
    result = launch(url, NULL);
    STAGE("browser-launch", result ? "error" : "ok", result);
    STAGE("browser-user-cleanup", "started", 0);
    int cleanup = terminate();
    STAGE("browser-user-cleanup", cleanup ? "error" : "ok", cleanup);
    return result;
}
int probe_browser_open(const char *url) {
    pthread_mutex_lock(&service_lock);
    int result = open_browser(url);
    pthread_mutex_unlock(&service_lock);
    return result;
}

#else
int probe_browser_open(const char *url) { (void)url; return -1; }
int probe_browser_user(void) { return -1; }
#endif
