/* Installs a finished .pkg download on the console, when the setting asks for it.
 * The download itself is already complete and checked; whatever happens here, the file stays. */
#include "orbit.h"
#include "install_service.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define POLL_SECONDS 2
#define NO_STATUS_SECONDS 15   /* "none" is expected for a moment after the call */
#define STALL_SECONDS 600

/* One install at a time: the helper listens on a fixed local port. */
static pthread_mutex_t one_install = PTHREAD_MUTEX_INITIALIZER;

static bool is_pkg(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".pkg");
}

static Job *find_locked(const char *id) {
    for (size_t i = 0; i < orbit.job_count; i++)
        if (orbit.jobs[i].id[0] && !strcmp(orbit.jobs[i].id, id)) return &orbit.jobs[i];
    return NULL;
}

static void set_locked(const char *id, const char *state, unsigned progress, const char *error, int code) {
    Job *j = find_locked(id);
    if (!j) return;
    copy_text(j->install_state, sizeof j->install_state, state);
    j->install_progress = progress;
    copy_text(j->install_error, sizeof j->install_error, error);
    j->install_code = code;
    pthread_cond_broadcast(&orbit.changed);
}

static void set(const char *id, const char *state, unsigned progress, const char *error, int code) {
    pthread_mutex_lock(&orbit.mutex);
    set_locked(id, state, progress, error, code);
    pthread_mutex_unlock(&orbit.mutex);
}

typedef struct {
    char id[24], path[700], name[128];
} Request;

static void run(const Request *q) {
    install_session session = INSTALL_SESSION_INIT;
    int r = install_session_open(&session);
    if (orbit_is_stopping()) {
        set(q->id, "", 0, "", 0);
        install_session_close(&session);
        return;
    }
    if (r != 0) {
        printf("[ORBIT] install: helper did not start (%d)\n", r);
        set(q->id, "failed", 0, "Orbit could not start the installer. Make sure the ELF loader is running.", r);
        install_session_close(&session);
        return;
    }
    set(q->id, "installing", 0, "", 0);
    r = install_session_install(&session, q->path, q->name);
    if (r != 0) {
        printf("[ORBIT] install: the console refused %s (%d)\n", q->path, r);
        set(q->id, "failed", 0, "The console would not install this package.", r);
        install_session_close(&session);
        return;
    }
    printf("[ORBIT] install: started %s as %s\n", q->path, session.content_id);
    time_t began = time(NULL), moved = began;
    uint64_t last = 0;
    for (;;) {
        sleep(POLL_SECONDS);
        if (orbit_is_stopping()) {
            set(q->id, "", 0, "", 0);
            break;
        }
        install_status st;
        r = install_session_status(&session, &st);
        time_t now = time(NULL);
        if (r != 0 || st.result != 0) {
            printf("[ORBIT] install: status failed (%d/%d)\n", r, st.result);
            set(q->id, "failed", 0, "The installation stopped before it finished.", r ? r : st.result);
            break;
        }
        printf("[ORBIT] install: %s %llu/%llu promote %u error %d\n", st.state,
               (unsigned long long)st.downloaded, (unsigned long long)st.total, st.progress, st.error_code);
        if (st.error_code != 0 || !strcmp(st.state, "error")) {
            set(q->id, "failed", 0, "The installation stopped before it finished.", st.error_code);
            break;
        }
        if (!strcmp(st.state, "playable") || !strcmp(st.state, "completed")) {
            set(q->id, "installed", 100, "", 0);
            break;
        }
        if (!strcmp(st.state, "none") && now - began < NO_STATUS_SECONDS) continue;
        if (!strcmp(st.state, "none")) {
            set(q->id, "failed", 0, "The installation stopped before it finished.", 0);
            break;
        }
        unsigned percent = st.total ? (unsigned)(st.downloaded * 100 / st.total) : st.progress;
        if (percent > 100) percent = 100;
        uint64_t mark = st.downloaded + st.progress;
        if (mark != last) {
            last = mark;
            moved = now;
        } else if (now - moved > STALL_SECONDS) {
            set(q->id, "failed", 0, "The installation did not make progress.", 0);
            break;
        }
        set(q->id, "installing", percent, "", 0);
    }
    install_session_close(&session);
}

static void *worker(void *arg) {
    Request *q = arg;
    pthread_mutex_lock(&one_install);
    run(q);
    pthread_mutex_unlock(&one_install);
    free(q);
    return NULL;
}

void install_job_completed_locked(Job *j) {
    j->install_state[0] = 0;
    j->install_error[0] = 0;
    j->install_code = 0;
    j->install_progress = 0;
    const char *file = j->output[0] ? j->output : j->filename;
    if (!orbit.install_pkgs || !install_service_available() || !is_pkg(file)) return;
    Request *q = calloc(1, sizeof *q);
    if (!q) return;
    copy_text(q->id, sizeof q->id, j->id);
    snprintf(q->path, sizeof q->path, "%s/homebrew/%s", j->root, file);
    copy_text(q->name, sizeof q->name, j->release.title[0] ? j->release.title : file);
    copy_text(j->install_state, sizeof j->install_state, "waiting");
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, worker, q) != 0) {
        j->install_state[0] = 0;
        free(q);
    }
    pthread_attr_destroy(&attr);
}
