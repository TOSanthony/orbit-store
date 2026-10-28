/* Console notifications, and the events the owner chose to hear about: downloads and
 * installations that finish or fail, and Orbit updates. They follow the PS5's system
 * language; their translations come from i18n/ through build/generated/messages.h
 * (tools/embed-messages.py reads the MESSAGE() texts). */
#include "orbit.h"
#include "messages.h"
#include "ntfy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ORBIT_DESKTOP
#include <dlfcn.h>
extern int sceKernelSendNotificationRequest(int, const void *, size_t, int);
#endif
#define MESSAGE(text) text

int notify_console(const char *text) {
#ifdef ORBIT_TEST
    const char *log = getenv("ORBIT_TEST_NOTIFICATIONS");
    FILE *f = log ? fopen(log, "a") : NULL;
    if (f) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
#endif
#ifndef ORBIT_DESKTOP
    unsigned char notification[3120] = {0};
    snprintf((char *)notification + 45, sizeof notification - 45, "%s", text);
    return sceKernelSendNotificationRequest(0, notification, sizeof notification, 0);
#else
    (void)text;
    return 0;
#endif
}

/* SCE_SYSTEM_SERVICE_PARAM_ID_LANG values, mapped as the TV app maps them (i18n.cpp). */
static const char *language_code(int system) {
    switch (system) {
    case 2: case 22: return "fr";
    case 3: case 20: return "es";
    case 4: return "de";
    case 5: return "it";
    case 6: return "nl";
    case 7: case 17: return "pt-BR";
    case 8: return "ru";
    case 16: return "pl";
    case 19: return "tr";
    case 24: return "hu";
    default: return "en";
    }
}

#if !defined(ORBIT_DESKTOP)
static int (*param_get_int)(int, int *);
static pthread_once_t param_once = PTHREAD_ONCE_INIT;
/* Looked up on the first notification, never on Orbit's startup path; the module stays
 * loaded for the process, as for the browser session. */
static void param_lookup(void) {
    void *module = dlopen("libSceSystemService.sprx", RTLD_NOW | RTLD_LOCAL);
    if (module) param_get_int = (int (*)(int, int *))dlsym(module, "sceSystemServiceParamGetInt");
}
#endif

static const char *system_language(void) {
#ifdef ORBIT_TEST
    const char *value = getenv("ORBIT_TEST_SYSTEM_LANGUAGE");
    return language_code(value ? atoi(value) : 1);
#elif !defined(ORBIT_DESKTOP)
    pthread_once(&param_once, param_lookup);
    int value = 1;
    if (!param_get_int || param_get_int(1, &value)) value = 1;
    return language_code(value);
#else
    return language_code(1);
#endif
}

static const char *translate(const char *english, const char *language) {
    size_t lang = 0;
    while (lang < ORBIT_MESSAGE_LANGUAGES && strcmp(orbit_message_languages[lang], language)) lang++;
    if (lang == ORBIT_MESSAGE_LANGUAGES) return english;
    for (size_t i = 0; i < sizeof orbit_messages / sizeof orbit_messages[0]; i++)
        if (!strcmp(orbit_messages[i].english, english) && orbit_messages[i].text[lang])
            return orbit_messages[i].text[lang];
    return english;
}

void notify_restart_failed(void) {
    const char *english = MESSAGE("Orbit stopped but could not start again. Run it from your payload manager.");
    notify_console(translate(english, system_language()));
}
static const char *const event_names[] = {"downloadFinished", "downloadFailed", "installFinished",
                                          "installFailed", "updateAvailable"};
const char *notify_event_name(unsigned index) {
    return index < sizeof event_names / sizeof event_names[0] ? event_names[index] : NULL;
}
bool notify_wanted_locked(unsigned event) {
    return (orbit.notify_events & event) && (orbit.download_notifications || ntfy_enabled());
}
bool notify_wanted(unsigned event) {
    pthread_mutex_lock(&orbit.mutex);
    bool wanted = notify_wanted_locked(event);
    pthread_mutex_unlock(&orbit.mutex);
    return wanted;
}
/* The translated text with {name} filled in. */
static void fill(char *out, size_t cap, const char *english, const char *name, const char *value) {
    const char *text = translate(english, system_language());
    char hole[24];
    snprintf(hole, sizeof hole, "{%s}", name);
    const char *at = strstr(text, hole);
    if (at)
        snprintf(out, cap, "%.*s%s%s", (int)(at - text), text, value, at + strlen(hole));
    else
        copy_text(out, cap, text);
}
/* On the console when its setting is on, and through ntfy when that is set up, if the
 * owner chose this event. */
static void deliver(unsigned event, const char *message, NtfyKind kind) {
    pthread_mutex_lock(&orbit.mutex);
    bool chosen = orbit.notify_events & event, console = orbit.download_notifications;
    pthread_mutex_unlock(&orbit.mutex);
    if (!chosen) return;
    if (console) notify_console(message);
    ntfy_send(message, kind);
}
void notify_download(const char *title, bool finished) {
    char message[512];
    fill(message, sizeof message, finished ? MESSAGE("Download finished: {title}") : MESSAGE("Download failed: {title}"),
         "title", title);
    deliver(finished ? NOTIFY_DOWNLOAD_FINISHED : NOTIFY_DOWNLOAD_FAILED, message, finished ? NTFY_DONE : NTFY_FAILED);
}
void notify_install(const char *title, bool installed, int cleanup) {
    const char *english = !installed ? MESSAGE("Installation failed: {title}")
                          : cleanup == 1 ? MESSAGE("Installed: {title}. The downloaded package was deleted.")
                          : cleanup == 2 ? MESSAGE("Installed: {title}. The downloaded package was kept.")
                                         : MESSAGE("Installed: {title}");
    char message[512];
    fill(message, sizeof message, english, "title", title);
    deliver(installed ? NOTIFY_INSTALL_FINISHED : NOTIFY_INSTALL_FAILED, message, installed ? NTFY_DONE : NTFY_FAILED);
}
void notify_update(const char *version) {
    /* Once per release, remembered across restarts; nothing is recorded while nobody would hear it. */
    pthread_mutex_lock(&orbit.mutex);
    bool fresh = notify_wanted_locked(NOTIFY_UPDATE_AVAILABLE) && *version && strcmp(version, orbit.notified_update);
    if (fresh) {
        char previous[sizeof orbit.notified_update];
        copy_text(previous, sizeof previous, orbit.notified_update);
        copy_text(orbit.notified_update, sizeof orbit.notified_update, version);
        /* Unsaved, it would be announced again after every restart. */
        if (state_save_locked()) {
            copy_text(orbit.notified_update, sizeof orbit.notified_update, previous);
            fresh = false;
        }
    }
    pthread_mutex_unlock(&orbit.mutex);
    if (!fresh) return;
    char message[512];
    fill(message, sizeof message, MESSAGE("Orbit {version} is available. Open Orbit to update."), "version", version);
    deliver(NOTIFY_UPDATE_AVAILABLE, message, NTFY_UPDATE);
}
const char *notify_ntfy_test(void) {
    return translate(MESSAGE("Orbit Store can send you notifications here."), system_language());
}
