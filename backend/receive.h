#ifndef ORBIT_RECEIVE_H
#define ORBIT_RECEIVE_H
#include "orbit.h"
#define ORBIT_RECEIVE_CHUNK (8U * 1024U * 1024U)
#define ORBIT_RECEIVE_WINDOW 4U
int receive_start(void);
void receive_stop(void);
bool receive_available(void);
/* Caller has authenticated the normal Orbit bearer token. */
cJSON *receive_status(const char *owner);
int receive_command(const char *action, const cJSON *input, const char *owner, char *error,
                    size_t cap);
/* Folder transfers: action is "open", "commit", "cancel" or "abandon". */
int receive_bundle(const char *action, const cJSON *input, const char *owner, char *error, size_t cap);
int receive_chunk(const char *id, const char *offset, const char *sha256, unsigned char **data,
                  size_t size, const char *owner, char *error, size_t cap);
#endif
