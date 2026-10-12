#include "orbit.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
bool provider_browser_source(const char *source) {
    return source && (!strcmp(source, "vikingfile") || !strcmp(source, "akirabox") ||
                      !strcmp(source, "datanodes"));
}
static bool browser_file(const Release *r) {
    if (!provider_browser_source(r->source) || strlen(r->title_id) != 9 ||
        (strncmp(r->title_id, "PPSA", 4) && strncmp(r->title_id, "CUSA", 4)) || !r->filename[0] ||
        strchr(r->filename, '/') || strchr(r->filename, '\\') || strstr(r->filename, ".."))
        return false;
    for (size_t i = 4; i < 9; i++)
        if (!isdigit((unsigned char)r->title_id[i])) return false;
    for (const unsigned char *p = (const void *)r->filename; *p; p++)
        if (*p < 32 || *p == 127) return false;
    if (r->archive_unverified && (strcmp(r->format, "Unknown") || r->extracted_size || !r->compression[0])) return false;
    const char *suffix = !strcmp(r->format, "exFAT") ? ".exfat" :
                         !strcmp(r->format, "FFPFSC") ? ".ffpfsc" :
                         (!strcmp(r->format, "FPKG") || !strcmp(r->format, "PKG")) ? ".pkg" : NULL;
    if (!suffix && !r->archive_unverified && (strcmp(r->format, "Game folder") || !r->compression[0] || strncmp(r->title_id, "PPSA", 4))) return false;
    if (r->compression[0]) {
        if (r->extracted_size <= 0 && !r->archive_unverified) return false;
        suffix = !strcmp(r->compression, "zip") ? ".zip" :
                 !strcmp(r->compression, "7z") ? ".7z" :
                 !strcmp(r->compression, "rar") ? ".rar" : NULL;
    }
    size_t n = strlen(r->filename);
    if (!suffix || n <= strlen(suffix) || strcasecmp(r->filename + n - strlen(suffix), suffix))
        return false;
    /* A source may use a descriptive filename without its title ID. If an ID
     * is present it must agree with the catalogue; matching remains exact. */
    const char *prefixes[] = {"PPSA", "CUSA"};
    for (size_t i = 0; i < 2; i++)
        for (const char *p = strstr(r->filename, prefixes[i]); p; p = strstr(p + 4, prefixes[i]))
            if (strncmp(p, r->title_id, 9) || isdigit((unsigned char)p[9])) return false;
    return true;
}
static bool browser_page(const Release *r, const char *url) {
    if (strcmp(r->source, "vikingfile")) {
        const char *prefixes[] = {"https://akirabox.com/", "https://akirabox.to/", "https://datanodes.to/"};
        for (size_t i = 0; i < 3; i++) {
            if ((i == 2) != !strcmp(r->source, "datanodes")) continue;
            size_t n = strlen(prefixes[i]);
            if (strncmp(url, prefixes[i], n)) continue;
            const char *p = url + n;
            if (strlen(p) != (i == 2 ? 12U : 17U)) return false;
            for (size_t j = 0; j < 12; j++) if (!isalnum((unsigned char)p[j])) return false;
            return i == 2 || !strcmp(p + 12, "/file");
        }
        return false;
    }
    const char *prefixes[] = {"https://vik1ngfile.site/f/", "https://vikingfile.com/f/"};
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(url, prefixes[i], n) || strlen(url + n) != 10) continue;
        for (const char *p = url + n; *p; p++)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9'))) return false;
        return true;
    }
    return false;
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* Browser-generated routes remain provider-bound and filename-bound. Tokens
 * are opaque; they are neither constructed nor retained in the catalogue. */
static bool generated_route(const Release *r, const char *url) {
    if (strncmp(url, "https://", 8)) return false;
    const char *host = url + 8, *path = strchr(host, '/');
    if (!path || path - host > 253 || strchr(url, '#')) return false;
    size_t n = (size_t)(path - host);
    bool akira = !strcmp(r->source, "akirabox"), data = !strcmp(r->source, "datanodes");
    bool gateway = data && n == strlen("tunnel5.dlproxy.uk") && !memcmp(host, "tunnel5.dlproxy.uk", n);
    bool allowed = akira && ((n == 12 && !strncmp(host, "akirabox.com", n)) ||
                            (n == 11 && !strncmp(host, "akirabox.to", n)));
    if (data) {
        const char *base = "datanodes.to"; size_t b = strlen(base);
        allowed = gateway || (n >= b && !memcmp(host + n - b, base, b) &&
                  (n == b || host[n - b - 1] == '.'));
    }
    if (!allowed) return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)host[i]) && host[i] != '.' && host[i] != '-') return false;
    if (akira && strncmp(path, "/download/", 10)) return false;
    const char *end = strchr(path, '?');
    if (!end) end = url + strlen(url);
    /* Reject path normalization tricks before curl interprets the address. */
    for (const char *p = path + 1; p < end;) {
        char segment[2048]; size_t used = 0;
        while (p < end && *p != '/') {
            unsigned char c = (unsigned char)*p++;
            if (c == '%') {
                if (end - p < 2 || hex(p[0]) < 0 || hex(p[1]) < 0) return false;
                c = (unsigned char)(hex(p[0]) * 16 + hex(p[1])); p += 2;
            }
            if (!c || c < 32 || c == 127 || c == '/' || c == '\\' || used + 1 >= sizeof segment) return false;
            segment[used++] = (char)c;
        }
        segment[used] = 0;
        if (!used || !strcmp(segment, ".") || !strcmp(segment, "..")) return false;
        if (p < end) p++;
    }
    const char *name = end;
    while (name > path && name[-1] != '/') name--;
    if (name == path + 1 || name == end) return false;
    if (gateway) {
        /* Opaque routes need the same exact Content-Disposition/size/identity
         * validation as filename routes. Only this observed gateway is allowed. */
        if (name - path != 10 || end - name < 32 || end - name > 1024 ||
            strncmp(end, "?sig=", 5) || !end[5] || strchr(end + 5, '&')) return false;
        for (const unsigned char *p = (const void *)url; *p; p++)
            if (*p <= 32 || *p >= 127 || *p == '\\') return false;
        return true;
    }
    const unsigned char *expected = (const void *)r->filename;
    for (const char *p = name; p < end; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '%') {
            if (end - p < 3 || hex(p[1]) < 0 || hex(p[2]) < 0) return false;
            c = (unsigned char)(hex(p[1]) * 16 + hex(p[2])); p += 2;
        }
        if (!c || !*expected || c != *expected++) return false;
    }
    if (*expected) return false;
    for (const unsigned char *p = (const void *)url; *p; p++)
        if (*p <= 32 || *p >= 127 || *p == '\\') return false;
    return true;
}
/* Provider boundary: never turn metadata/page URLs into unverified downloads. */
bool provider_url_supported(const Release *release) {
    if (!strcmp(release->id, "orbit-network-check"))
        return !strcmp(release->url,
                       "https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/LICENSE");
    if (!strcmp(release->source, "archive"))
        return !strncmp(release->url, "https://archive.org/download/", 29) &&
               strlen(release->url) > 29 &&
               (!strcmp(release->format, "FFPFSC") || !strcmp(release->format, "exFAT") ||
                !strcmp(release->format, "FPKG") || !strcmp(release->format, "PKG") ||
                (release->archive_unverified && release->compression[0]) ||
                (!strcmp(release->format, "Game folder") && !strncmp(release->title_id, "PPSA", 4) &&
                 release->extracted_size > 0 &&
                 (!strcmp(release->compression, "zip") || !strcmp(release->compression, "7z") || !strcmp(release->compression, "rar"))));
    /* Only exact filename-bound direct routes may enter the download queue.
     * Landing pages are a separate browser capability, never a transfer URL. */
    if (!browser_file(release)) return false;
    if (strcmp(release->source, "vikingfile")) return generated_route(release, release->url);
    const char *prefix = "https://vikingfile.com/d/";
    if (strncmp(release->url, prefix, strlen(prefix)))
        return false;
    const char *path = release->url + strlen(prefix);
    if (strlen(path) < 12 || path[10] != '/')
        return false;
    for (size_t i = 0; i < 10; i++)
        if (!isalnum((unsigned char)path[i])) return false;
    const unsigned char *expected = (const void *)release->filename;
    for (const unsigned char *p = (const void *)(path + 11); *p; p++) {
        unsigned char c = *p;
        if (c == '%') {
            if (!p[1] || !p[2] || hex(p[1]) < 0 || hex(p[2]) < 0) return false;
            c = (unsigned char)(hex(p[1]) * 16 + hex(p[2])); p += 2;
        } else if (c <= 32 || c >= 127 || c == '?' || c == '#' || c == '/' || c == '\\')
            return false;
        if (!*expected || c != *expected++) return false;
    }
    return !*expected;
}

bool provider_supported(const Release *release) {
#ifdef ORBIT_TEST
    /* Test binaries may parse the catalogue, but may only transfer fixtures. */
    return !strncmp(release->url, "http://127.0.0.1:", 17) &&
           (!strcmp(release->source, "archive") || provider_browser_source(release->source));
#else
    return provider_url_supported(release);
#endif
}

bool provider_browser_supported(const Release *release) {
    if (!release || !provider_browser_source(release->source)) return false;
#ifdef ORBIT_TEST
    if (!strncmp(release->browser_url, "http://127.0.0.1:", 17)) return true;
#endif
    return browser_file(release) && browser_page(release, release->browser_url) &&
        (!strcmp(release->url, release->browser_url) || provider_url_supported(release));
}

bool provider_capture_url(const Release *release, const char *candidate) {
    if (!candidate || strlen(candidate) >= sizeof release->url) return false;
    Release resolved = *release;
    strcpy(resolved.url, candidate);
    return provider_browser_source(release->source) && provider_supported(&resolved);
}
