#include "patchfeed.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const SONY_HOSTS[] = {"sgst.prod.dl.playstation.net", "gst.prod.dl.playstation.net",
                                         "gs2.ww.prod.dl.playstation.net", "gs-sec.ww.np.dl.playstation.net"};

bool patchfeed_version_valid(const char *v) {
    if (!v || strlen(v) != 10) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 2 || i == 6) { if (v[i] != '.') return false; }
        else if (!isdigit((unsigned char)v[i])) return false;
    }
    return true;
}

int patchfeed_version_compare(const char *a, const char *b) {
    bool va = patchfeed_version_valid(a), vb = patchfeed_version_valid(b);
    if (!va || !vb) return va == vb ? 0 : va ? 1 : -1;
    for (int i = 0; i < 10; i++) {
        if (i == 2 || i == 6) continue;
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;     /* same width: digit order is number order */
    }
    return 0;
}

void patchfeed_firmware_text(uint32_t packed, char *out, size_t cap) {
    if (!cap) return;
    snprintf(out, cap, "%x.%02x", (packed >> 24) & 0xff, (packed >> 16) & 0xff);
}

bool patchfeed_firmware_ok(uint32_t needed, uint32_t console) {
    if (!console) return false;
    return (needed >> 16) <= (console >> 16);              /* major.minor are the top two bytes */
}

/* The value of attribute `name` inside the tag text [tag, end): copied, never truncated. */
static bool attribute(const char *tag, const char *end, const char *name, char *out, size_t cap) {
    size_t n = strlen(name);
    for (const char *p = tag + 1; p + n + 2 < end; p++) {
        if (!isspace((unsigned char)p[-1])) continue;      /* a whole attribute name, not the end of another */
        if (strncmp(p, name, n) || p[n] != '=' || (p[n + 1] != '"' && p[n + 1] != '\'')) continue;
        char quote = p[n + 1];
        const char *start = p + n + 2, *stop = start;
        while (stop < end && *stop != quote) stop++;
        if (stop >= end || (size_t)(stop - start) >= cap) return false;
        memcpy(out, start, (size_t)(stop - start));
        out[stop - start] = 0;
        return true;
    }
    return false;
}

static bool decode_entities(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r != '&') { *w++ = *r; continue; }
        if (!strncmp(r, "&amp;", 5)) { *w++ = '&'; r += 4; }
        else return false;                                  /* anything else has no place in an address */
    }
    *w = 0;
    return true;
}

static bool parse_system_ver(const char *text, uint32_t *out) {
    if (!*text || strlen(text) > 10) return false;
    char *end;
    unsigned long long v = strtoull(text, &end, 0);
    if (*end || v > 0xffffffffULL) return false;
    *out = (uint32_t)v;
    return true;
}

int patchfeed_parse(const char *xml, size_t length, patchfeed_package *out, size_t cap) {
    if (!xml || !out || !cap) return -1;
    if (length > (1u << 20)) return -1;
    char *copy = malloc(length + 1);                        /* attributes are read with strstr: need a terminator */
    if (!copy) return -1;
    memcpy(copy, xml, length);
    copy[length] = 0;
    int result = -1;
    if (!strstr(copy, "<title_patch")) goto done;
    char title[16] = "", content[48] = "";
    const char *title_tag = strstr(copy, "<title_patch");
    const char *title_end = strchr(title_tag, '>');
    if (title_end) {
        char nptitle[32];
        if (attribute(title_tag, title_end, "nptitleid", nptitle, sizeof nptitle) && strlen(nptitle) >= 9) {
            memcpy(title, nptitle, 9);
            title[9] = 0;
        }
    }
    size_t count = 0;
    for (const char *p = copy; (p = strstr(p, "<")) != NULL;) {
        const char *end = strchr(p, '>');
        if (!end) break;
        if (!strncmp(p, "<app_tag", 8) && !isalnum((unsigned char)p[8])) {
            if (!attribute(p, end, "content_id", content, sizeof content)) content[0] = 0;
        } else if (!strncmp(p, "<package", 8) && !isalnum((unsigned char)p[8]) && count < cap) {
            patchfeed_package pk;
            memset(&pk, 0, sizeof pk);
            char version[16], system[16], mandatory[8];
            bool ok = (attribute(p, end, "content_ver", version, sizeof version) ||
                       attribute(p, end, "version", version, sizeof version)) &&
                      patchfeed_version_valid(version) &&
                      attribute(p, end, "manifest_url", pk.manifest_url, sizeof pk.manifest_url) &&
                      decode_entities(pk.manifest_url) && patchfeed_url_allowed(pk.manifest_url) &&
                      attribute(p, end, "system_ver", system, sizeof system) && parse_system_ver(system, &pk.system_ver);
            if (ok) {
                memcpy(pk.version, version, sizeof pk.version);
                memcpy(pk.title_id, title, sizeof pk.title_id);
                memcpy(pk.content_id, content, sizeof pk.content_id);
                if (attribute(p, end, "delta_url", pk.delta_url, sizeof pk.delta_url) &&
                    (!decode_entities(pk.delta_url) || !patchfeed_url_allowed(pk.delta_url)))
                    pk.delta_url[0] = 0;
                pk.mandatory = attribute(p, end, "mandatory", mandatory, sizeof mandatory) && !strcmp(mandatory, "true");
                out[count++] = pk;
            }
        }
        p = end + 1;
    }
    result = (int)count;
done:
    free(copy);
    return result;
}

patchfeed_choice patchfeed_choose(const patchfeed_package *list, size_t count, const char *installed, uint32_t console) {
    patchfeed_choice choice = {PATCHFEED_UP_TO_DATE, NULL};
    const patchfeed_package *best = NULL, *blocked = NULL;
    for (size_t i = 0; i < count; i++) {
        const patchfeed_package *p = &list[i];
        if (installed && *installed && patchfeed_version_compare(p->version, installed) <= 0) continue;
        if (patchfeed_firmware_ok(p->system_ver, console)) {
            if (!best || patchfeed_version_compare(p->version, best->version) > 0) best = p;
        } else if (!blocked || patchfeed_version_compare(p->version, blocked->version) > 0) {
            blocked = p;
        }
    }
    if (best) { choice.state = PATCHFEED_AVAILABLE; choice.offer = best; }
    else if (blocked) { choice.state = PATCHFEED_NEEDS_NEWER_FW; choice.offer = blocked; }
    return choice;
}

bool patchfeed_url_allowed(const char *url) {
    if (!url) return false;
    const char *rest;
    if (!strncmp(url, "https://", 8)) rest = url + 8;
    else if (!strncmp(url, "http://", 7)) rest = url + 7;
    else return false;
    size_t host = strcspn(rest, "/");
    if (!host || rest[host] != '/') return false;
    bool known = false;
    for (size_t i = 0; i < sizeof SONY_HOSTS / sizeof *SONY_HOSTS; i++)
        if (strlen(SONY_HOSTS[i]) == host && !strncmp(rest, SONY_HOSTS[i], host)) known = true;
    if (!known || strlen(url) >= 768) return false;
    for (const unsigned char *p = (const unsigned char *)rest + host; *p; p++) {
        if (!(isalnum(*p) || *p == '/' || *p == '.' || *p == '_' || *p == '-')) return false;   /* no ? # : @ % space */
    }
    return strstr(rest + host, "..") == NULL;
}

bool patchfeed_manifest_from_link(const char *link, char *out, size_t cap) {
    static const char *const suffixes[] = {".crc", ".json", "_sc.pkg", "-DP.pkg"};
    if (!patchfeed_url_allowed(link)) return false;
    size_t n = strlen(link);
    for (size_t i = 0; i < sizeof suffixes / sizeof *suffixes; i++) {
        size_t s = strlen(suffixes[i]);
        if (n > s && !strcmp(link + n - s, suffixes[i])) {
            if (n - s + 6 > cap) return false;
            memcpy(out, link, n - s);
            strcpy(out + n - s, ".json");
            return patchfeed_url_allowed(out);
        }
    }
    return false;
}
