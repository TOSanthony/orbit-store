/* The package-install helper. The Orbit service sends this program to the console's ELF
 * loader; it runs as its own process, connects back to the service, and makes the system
 * calls that install a package. A fresh helper serves each install and ends with it. */
#include "install_proto.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The system's own layouts (libSceAppInstUtil). */
typedef struct {
    const char *uri, *ex_uri, *playgo_scenario_id, *content_id, *content_name, *icon_url;
} pkg_metadata_t;
typedef struct {
    char content_id[48];
    int type, platform;
} pkg_info_t;
typedef struct {
    char languages[30][8];
    char playgo_scenario_ids[64][3];
    char content_ids[64][48];
    unsigned char unknown[6480];
} playgo_info_t;
typedef struct {
    int32_t error_code, version;
    char description[512];
    char type[9];
} install_error_t;
typedef struct {
    char status[16];
    char src_type[8];
    uint32_t remain_time;
    uint64_t downloaded_size, initial_chunk_size, total_size;
    uint32_t promote_progress;
    install_error_t error_info;
    int32_t local_copy_percent;
    bool is_copy_only;
} install_status_t;

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilTerminate(void);
extern int sceAppInstUtilInstallByPackage(const pkg_metadata_t *, pkg_info_t *, playgo_info_t *);
extern int sceAppInstUtilGetInstallStatus(const char *, install_status_t *);
extern int sceNetInit(void);
extern int sceNetCtlInit(void);
extern int sceUserServiceInitialize(int *priority);

static FILE *log_file;
static void say(const char *format, ...) {
    if (!log_file) return;
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fflush(log_file);
}

static void copy(char *to, size_t cap, const char *from) {
    snprintf(to, cap, "%s", from);
}

static int connect_back(void) {
    for (int attempt = 0; attempt < 50; attempt++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(ORBIT_INST_PORT)};
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&peer, sizeof peer) == 0) return fd;
        close(fd);
        usleep(200 * 1000);
    }
    return -1;
}

static void send_response(int fd, orbit_inst_response *r, unsigned op) {
    r->magic = ORBIT_INST_MAGIC;
    r->version = ORBIT_INST_VERSION;
    r->op = op;
    if (orbit_inst_xfer(fd, r, sizeof *r, 1, 10000, NULL) != 0) {
        say("could not answer op %u\n", op);
        _exit(1);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    log_file = fopen(ORBIT_INST_LOG_FILE, "a");
    say("helper %d starting\n", (int)getpid());

    char token[ORBIT_INST_TOKEN_LEN + 1] = {0};
    FILE *tf = fopen(ORBIT_INST_TOKEN_FILE, "r");
    if (tf) {
        size_t n = fread(token, 1, ORBIT_INST_TOKEN_LEN, tf);
        token[n] = 0;
        fclose(tf);
    }
    int priority = 256;
    sceNetCtlInit();
    sceUserServiceInitialize(&priority);
    sceNetInit();

    int fd = connect_back();
    if (fd < 0) {
        say("could not reach the service\n");
        return 1;
    }
    errno = 0;
    int init = sceAppInstUtilInitialize();
    say("sceAppInstUtilInitialize -> 0x%08x errno %d\n", (unsigned)init, errno);
    orbit_inst_response out = {0};
    out.result = init;
    out.native_errno = errno;
    out.pid = (int32_t)getpid();
    memcpy(out.token, token, sizeof out.token);
    send_response(fd, &out, ORBIT_INST_HELLO);
    if (init != 0) return 1;

    /* The system may keep reading these after the call returns, so they live as long as we do. */
    static pkg_metadata_t metadata;
    static pkg_info_t info;
    static playgo_info_t playgo;
    static orbit_inst_request request;
    static char path[1024], name[256];
    bool installing = false;

    for (;;) {
        int r = orbit_inst_xfer(fd, &request, sizeof request, 0, 300000, NULL);
        if (r != 0 || request.magic != ORBIT_INST_MAGIC || request.version != ORBIT_INST_VERSION) break;
        memset(&out, 0, sizeof out);
        if (request.op == ORBIT_INST_CLOSE) {
            send_response(fd, &out, ORBIT_INST_CLOSE);
            break;
        }
        if (request.op == ORBIT_INST_INSTALL) {
            if (installing || !memchr(request.path, 0, sizeof request.path) ||
                !memchr(request.name, 0, sizeof request.name)) {
                out.result = -1;
            } else {
                copy(path, sizeof path, request.path);
                copy(name, sizeof name, request.name);
                memset(&metadata, 0, sizeof metadata);
                memset(&info, 0, sizeof info);
                memset(&playgo, 0, sizeof playgo);
                metadata.uri = path;
                metadata.ex_uri = "";
                metadata.playgo_scenario_id = "";
                metadata.content_id = "";
                metadata.content_name = name;
                metadata.icon_url = "";
                errno = 0;
                out.result = sceAppInstUtilInstallByPackage(&metadata, &info, &playgo);
                out.native_errno = errno;
                copy(out.content_id, sizeof out.content_id, info.content_id);
                installing = out.result == 0;
                say("install %s -> 0x%08x errno %d content %s\n", path, (unsigned)out.result,
                    out.native_errno, info.content_id);
            }
            send_response(fd, &out, ORBIT_INST_INSTALL);
        } else if (request.op == ORBIT_INST_STATUS) {
            install_status_t status;
            memset(&status, 0, sizeof status);
            char id[48];
            copy(id, sizeof id, request.content_id[0] ? request.content_id : info.content_id);
            out.result = sceAppInstUtilGetInstallStatus(id, &status);
            /* Every word and number the system reports, whenever any of them changes, so a
             * log shows what each stage of an install is called. */
            static char last[256];
            char now[256];
            snprintf(now, sizeof now, "result=0x%08x status=%s src=%s downloaded=%llu total=%llu promote=%u remain=%u copy=%d copy_only=%d error=%d",
                     (unsigned)out.result, status.status, status.src_type,
                     (unsigned long long)status.downloaded_size, (unsigned long long)status.total_size,
                     status.promote_progress, status.remain_time, status.local_copy_percent,
                     (int)status.is_copy_only, status.error_info.error_code);
            if (strcmp(now, last)) {
                say("status %s\n", now);
                copy(last, sizeof last, now);
            }
            copy(out.state, sizeof out.state, status.status);
            out.downloaded = status.downloaded_size;
            out.total = status.total_size;
            out.progress = status.promote_progress;
            out.remain_seconds = status.remain_time;
            out.error_code = status.error_info.error_code;
            copy(out.description, sizeof out.description, status.error_info.description);
            send_response(fd, &out, ORBIT_INST_STATUS);
        } else {
            break;
        }
    }
    sceAppInstUtilTerminate();
    close(fd);
    say("helper done\n");
    return 0;
}
