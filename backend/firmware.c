/* The console's firmware, and what it means for a download. Nothing here hides or blocks a
 * download: clients label the options that won't run on this console and list them last. */
#include "firmware.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* PS5 fake packages run on 11.60 or lower; above it they install but don't start. */
#define PS5_PACKAGE_LIMIT 0x11600000u
#define PS5_PACKAGE_LIMIT_TEXT "11.60"

/* The SDK reports 12.60 as 0x12600000: two BCD digits each for the major and minor version. */
static bool bcd_byte(unsigned value) { return (value >> 4) < 10 && (value & 15) < 10; }
static unsigned from_bcd(unsigned value) { return (value >> 4) * 10 + (value & 15); }

bool firmware_known(uint32_t raw) {
    unsigned major = raw >> 24, minor = (raw >> 16) & 0xff;
    return major && bcd_byte(major) && bcd_byte(minor);
}
void firmware_text(uint32_t raw, char *out, size_t cap) {
    if (!cap) return;
    *out = 0;
    if (firmware_known(raw))
        snprintf(out, cap, "%u.%02u", from_bcd(raw >> 24), from_bcd((raw >> 16) & 0xff));
}
const char *firmware_package_limit(void) { return PS5_PACKAGE_LIMIT_TEXT; }

/* "10.xx", "[11.xx+]" and "[BP4.xx]" in a release's filename name the oldest firmware it runs
 * on. Version numbers like v01.004 never end in "xx", so they don't match. */
int firmware_required(const char *name) {
    for (const char *p = name; p && *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > name && (isdigit((unsigned char)p[-1]) || p[-1] == '.')))
            continue;
        int major = 0, digits = 0;
        const char *q = p;
        while (isdigit((unsigned char)*q) && digits < 3) major = major * 10 + (*q++ - '0'), digits++;
        if (digits > 2 || strncasecmp(q, ".xx", 3) || isalnum((unsigned char)q[3])) continue;
        if (major >= 1 && major <= 30) return major;
    }
    return -1;
}

static bool ps5_package(const cJSON *release) {
    const char *format = json_text(release, "format");
    return !*json_text(release, "addonId") && !strcmp(json_text(release, "platform"), "ps5") &&
           (!strcmp(format, "PKG") || !strcmp(format, "FPKG"));
}

void firmware_describe(cJSON *entry, uint32_t raw) {
    const char *issue = NULL;
    /* On a PS4 option, "N.xx" is a PS4 firmware: nothing to compare with the PS5's. */
    int required = strcmp(json_text(entry, "platform"), "ps5") ? -1 : firmware_required(json_text(entry, "filename"));
    if (required > 0) {
        char text[8];
        snprintf(text, sizeof text, "%d.xx", required);
        cJSON_AddStringToObject(entry, "requiredFirmware", text);
        if (firmware_known(raw) && from_bcd(raw >> 24) < (unsigned)required) issue = "newer-firmware";
    }
    if (ps5_package(entry)) {
        cJSON_AddStringToObject(entry, "maxFirmware", PS5_PACKAGE_LIMIT_TEXT);
        if (firmware_known(raw) && (raw & 0xffff0000u) > PS5_PACKAGE_LIMIT) issue = "ps5-package";
    }
    if (issue) cJSON_AddStringToObject(entry, "firmwareIssue", issue);
}
