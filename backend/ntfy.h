#ifndef ORBIT_NTFY_H
#define ORBIT_NTFY_H
#include "orbit.h"
/* ntfy push notifications: a POST to the server and topic the owner chose when a
 * download or installation finishes or fails, or an update is out, so a phone or
 * computer subscribed to that topic hears about it. The settings, and the optional username and password, are kept
 * in ntfy.json (0600), never in state.json, the API's replies or diagnostics. */
void ntfy_load(void);
/* Stops the sender, abandoning a request in flight. Call after the download workers stop. */
void ntfy_stop(void);
bool ntfy_enabled(void);
/* {enabled, server, topic, username, hasPassword}: never the password. */
cJSON *ntfy_status(void);
/* {enabled, server, topic, auth, username, password}. Without auth the username and
 * password are forgotten; an empty password with auth keeps the saved one. */
int ntfy_configure(const cJSON *input, char *error, size_t cap);
/* Sends a test notification with the saved settings and waits for the server's reply.
 * Never call it under orbit.mutex. */
int ntfy_test(const char *message, char *error, size_t cap);
/* How a notification looks in the ntfy app: a tick, a warning at high priority, or an arrow. */
typedef enum { NTFY_DONE, NTFY_FAILED, NTFY_UPDATE } NtfyKind;
/* Queues a notification (already in the console's language). Never blocks on the network. */
void ntfy_send(const char *message, NtfyKind kind);
#endif
