#ifndef ORBIT_ZERO_PROBE_H
#define ORBIT_ZERO_PROBE_H
#include "orbit.h"
int zero_probe_start(void);
void zero_probe_stop(void);
int zero_probe_request(const char *release_id, char *error, size_t cap);
cJSON *zero_probe_status(void);
#endif
