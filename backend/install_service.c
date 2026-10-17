#include "install_service.h"
#include "orbit.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define LOADER_PORT 9021
#define HELPER_CONNECT_MS 25000
#define HELPER_READY_MS 30000
#define INSTALL_CALL_MS 120000
#define STATUS_CALL_MS 10000

#if defined(ORBIT_EMBED_INSTALL_HELPER) && !defined(ORBIT_DESKTOP)
extern const unsigned char install_helper_elf[], install_helper_elf_end[];
#define HELPER_AVAILABLE 1
#else
#define HELPER_AVAILABLE 0
#endif

bool install_service_available(void) {
    return HELPER_AVAILABLE;
}

static int canceled(void) {
    return orbit_is_stopping();
}

#if HELPER_AVAILABLE
static bool write_token(char *token, size_t cap) {
    unsigned char raw[ORBIT_INST_TOKEN_LEN / 2];
    int urandom = open("/dev/urandom", O_RDONLY);
    if (urandom < 0 || read(urandom, raw, sizeof raw) != (ssize_t)sizeof raw) {
        if (urandom >= 0) close(urandom);
        return false;
    }
    close(urandom);
    if (cap < ORBIT_INST_TOKEN_LEN + 1) return false;
    for (size_t i = 0; i < sizeof raw; i++) snprintf(token + 2 * i, 3, "%02x", raw[i]);
    int fd = open(ORBIT_INST_TOKEN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = write(fd, token, ORBIT_INST_TOKEN_LEN) == ORBIT_INST_TOKEN_LEN;
    close(fd);
    return ok;
}

/* Hands the helper program to the ELF loader. Returns the open connection so the loader's
 * own output can be read while we wait, or -1. */
static int send_to_loader(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(LOADER_PORT)};
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&peer, sizeof peer) != 0) {
        close(fd);
        return -1;
    }
    size_t size = (size_t)(install_helper_elf_end - install_helper_elf);
    if (orbit_inst_xfer(fd, (void *)install_helper_elf, size, 1, 20000, canceled) != 0) {
        close(fd);
        return -1;
    }
    /* The loader starts the program when it reads the end of the file. */
    shutdown(fd, SHUT_WR);
    return fd;
}
#endif

int install_session_open(install_session *session) {
    session->fd = -1;
    session->pid = -1;
    session->content_id[0] = 0;
#if !HELPER_AVAILABLE
    return ORBIT_INST_E_UNAVAILABLE;
#else
    char token[ORBIT_INST_TOKEN_LEN + 1] = {0};
    if (!write_token(token, sizeof token)) return ORBIT_INST_E_UNAVAILABLE;
    /* A previous helper's log is of no use; start a fresh one. */
    unlink(ORBIT_INST_LOG_FILE);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return ORBIT_INST_E_UNAVAILABLE;
    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in self = {.sin_family = AF_INET, .sin_port = htons(ORBIT_INST_PORT)};
    self.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&self, sizeof self) != 0 || listen(listener, 1) != 0) {
        close(listener);
        return ORBIT_INST_E_UNAVAILABLE;
    }
    int loader = send_to_loader();
    if (loader < 0) {
        close(listener);
        return ORBIT_INST_E_UNAVAILABLE;
    }

    int client = -1, result = ORBIT_INST_E_TIMEOUT;
    int64_t deadline = orbit_inst_now_ms() + HELPER_CONNECT_MS;
    while (orbit_inst_now_ms() < deadline) {
        if (canceled()) {
            result = ORBIT_INST_E_CANCELED;
            break;
        }
        struct pollfd p[2] = {{.fd = listener, .events = POLLIN}, {.fd = loader, .events = POLLIN}};
        int r = poll(p, loader >= 0 ? 2 : 1, 100);
        if (r <= 0) continue;
        if (p[0].revents & POLLIN) {
            client = accept(listener, NULL, NULL);
            if (client >= 0) break;
        }
        if (loader >= 0 && (p[1].revents & (POLLIN | POLLHUP))) {
            char sink[256];
            if (read(loader, sink, sizeof sink) <= 0) {
                close(loader);
                loader = -1;
            }
        }
    }
    if (loader >= 0) close(loader);
    close(listener);
    if (client < 0) return result;

    orbit_inst_response hello;
    int r = orbit_inst_xfer(client, &hello, sizeof hello, 0, HELPER_READY_MS, canceled);
    hello.token[sizeof hello.token - 1] = 0;
    if (r == 0 && (hello.magic != ORBIT_INST_MAGIC || hello.version != ORBIT_INST_VERSION ||
                   hello.op != ORBIT_INST_HELLO || strncmp(hello.token, token, ORBIT_INST_TOKEN_LEN) != 0))
        r = ORBIT_INST_E_UNAVAILABLE;
    unlink(ORBIT_INST_TOKEN_FILE);
    session->fd = client;
    /* Only a helper that proved itself may be signalled later. */
    if (r == 0) session->pid = (pid_t)hello.pid;
    if (r == 0 && hello.result != 0) r = hello.result;
    if (r != 0) {
        install_session_close(session);
        return r;
    }
    return 0;
#endif
}

static int ask(install_session *session, orbit_inst_request *request, orbit_inst_response *reply,
               unsigned op, int timeout_ms) {
    if (session->fd < 0) return ORBIT_INST_E_DISCONNECTED;
    request->magic = ORBIT_INST_MAGIC;
    request->version = ORBIT_INST_VERSION;
    request->op = op;
    int r = orbit_inst_xfer(session->fd, request, sizeof *request, 1, 10000, canceled);
    if (r == 0) r = orbit_inst_xfer(session->fd, reply, sizeof *reply, 0, timeout_ms, canceled);
    if (r == 0 && (reply->magic != ORBIT_INST_MAGIC || reply->version != ORBIT_INST_VERSION || reply->op != op))
        r = ORBIT_INST_E_DISCONNECTED;
    return r;
}

int install_session_install(install_session *session, const char *path, const char *name) {
    struct stat st;
    if (!path || path[0] != '/' || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -ENOENT;
    orbit_inst_request request = {0};
    orbit_inst_response reply = {0};
    if (snprintf(request.path, sizeof request.path, "%s", path) >= (int)sizeof request.path) return -ENAMETOOLONG;
    snprintf(request.name, sizeof request.name, "%s", name ? name : "");
    int r = ask(session, &request, &reply, ORBIT_INST_INSTALL, INSTALL_CALL_MS);
    if (r != 0) return r;
    if (reply.result == 0) {
        reply.content_id[sizeof reply.content_id - 1] = 0;
        copy_text(session->content_id, sizeof session->content_id, reply.content_id);
    }
    return reply.result;
}

int install_session_status(install_session *session, install_status *status) {
    orbit_inst_request request = {0};
    orbit_inst_response reply = {0};
    memset(status, 0, sizeof *status);
    copy_text(request.content_id, sizeof request.content_id, session->content_id);
    int r = ask(session, &request, &reply, ORBIT_INST_STATUS, STATUS_CALL_MS);
    if (r != 0) return r;
    reply.state[sizeof reply.state - 1] = 0;
    reply.description[sizeof reply.description - 1] = 0;
    status->result = reply.result;
    memcpy(status->state, reply.state, sizeof status->state);
    status->downloaded = reply.downloaded;
    status->total = reply.total;
    status->progress = reply.progress;
    status->remain_seconds = reply.remain_seconds;
    status->error_code = reply.error_code;
    memcpy(status->description, reply.description, sizeof status->description);
    return 0;
}

void install_session_close(install_session *session) {
    if (session->fd >= 0) {
        orbit_inst_request request = {0};
        orbit_inst_response reply = {0};
        request.magic = ORBIT_INST_MAGIC;
        request.version = ORBIT_INST_VERSION;
        request.op = ORBIT_INST_CLOSE;
        if (orbit_inst_xfer(session->fd, &request, sizeof request, 1, 2000, NULL) == 0)
            orbit_inst_xfer(session->fd, &reply, sizeof reply, 0, 3000, NULL);
        close(session->fd);
        session->fd = -1;
    }
    if (session->pid > 1) {
        for (int i = 0; i < 20 && kill(session->pid, 0) == 0; i++) usleep(100 * 1000);
        if (kill(session->pid, 0) == 0) kill(session->pid, SIGKILL);
    }
    session->pid = -1;
}
