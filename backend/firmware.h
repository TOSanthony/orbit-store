#ifndef ORBIT_FIRMWARE_H
#define ORBIT_FIRMWARE_H
#include "orbit.h"
#include <stddef.h>
#include <stdint.h>
bool firmware_known(uint32_t raw);
/* "12.60", or empty when the firmware is unknown. */
void firmware_text(uint32_t raw, char *out, size_t cap);
/* The newest firmware PS5 fake packages run on, as text. */
const char *firmware_package_limit(void);
/* The oldest firmware major version a filename names, or -1. */
int firmware_required(const char *name);
/* Adds requiredFirmware, maxFirmware and firmwareIssue to a catalogue entry for this console. */
void firmware_describe(cJSON *entry, uint32_t raw);
#endif
