#ifndef ORBIT_ZERO_COMPANION_H
#define ORBIT_ZERO_COMPANION_H
#include "orbit.h"
cJSON *zero_companion_status(bool secret);
int zero_companion_update(const cJSON *input, const char *peer, bool local, char *error, size_t cap);
#endif
