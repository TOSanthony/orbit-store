#ifndef ORBIT_PATCHFEED_H
#define ORBIT_PATCHFEED_H
/* Sony's per-game update list ("version.xml") and the choice of which patch to offer.
 *
 * Pure parsing and comparison, no network and no allocation, so it can be tested on a computer.
 * A version.xml carries one <package> per patch Sony still serves (in practice only the newest).
 * Each has the patch's version, the lowest system software it needs ("system_ver", the firmware
 * written as hexadecimal digits that read as decimal: 0x10200006 is firmware 10.20.00.06) and the
 * address of its manifest, the JSON that lists the pieces to download. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PATCHFEED_MAX 8

typedef struct {
    char title_id[16];        /* "PPSA01552" (without the _00 suffix) */
    char content_id[48];      /* "UP4001-PPSA01552_00-DIRTFIVEPS5EDITI" */
    char version[16];         /* "06.005.000" */
    char manifest_url[768];   /* ends in .json */
    char delta_url[768];      /* the -DP.pkg, informational */
    uint32_t system_ver;      /* packed firmware, e.g. 0x10200006 */
    bool mandatory;
} patchfeed_package;

typedef enum {
    PATCHFEED_UP_TO_DATE,       /* nothing newer than the installed version */
    PATCHFEED_AVAILABLE,        /* a newer patch this console can install */
    PATCHFEED_NEEDS_NEWER_FW    /* a newer patch exists but needs a newer firmware */
} patchfeed_state;

typedef struct {
    patchfeed_state state;
    const patchfeed_package *offer;   /* the patch to install (AVAILABLE) or the one that is blocked */
} patchfeed_choice;

/* Reads every <package> of a version.xml. Returns how many were stored (at most `cap`), or -1 when
 * the text is not a title_patch document. Packages with an unusable version or manifest are skipped. */
int patchfeed_parse(const char *xml, size_t length, patchfeed_package *out, size_t cap);

/* "06.005.000" ordering: <0, 0, >0. Parts are compared as numbers; malformed text sorts lowest. */
int patchfeed_version_compare(const char *a, const char *b);

/* "NN.NNN.NNN" with digits only. */
bool patchfeed_version_valid(const char *version);

/* 0x10200006 -> "10.20" (major.minor, what the console's firmware screen shows). */
void patchfeed_firmware_text(uint32_t packed, char *out, size_t cap);

/* True when the console (packed, same format) is new enough. Only major.minor count: a console on
 * 10.20 accepts a patch that needs 10.20.00.06. A console value of 0 (unknown) never matches. */
bool patchfeed_firmware_ok(uint32_t needed, uint32_t console);

/* Picks what to offer for a game. `installed` is "" when unknown (then any patch counts as newer). */
patchfeed_choice patchfeed_choose(const patchfeed_package *list, size_t count, const char *installed, uint32_t console);

/* Only Sony's download servers, over http or https, no credentials, port, query or fragment. */
bool patchfeed_url_allowed(const char *url);

/* The manifest address for a link that ends in .crc, .json, _sc.pkg or -DP.pkg; false if not valid. */
bool patchfeed_manifest_from_link(const char *link, char *out, size_t cap);
#endif
