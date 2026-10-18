#include "patchmanifest.h"
#include "cJSON.h"
#include "patchfeed.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *error, size_t cap, const char *text) {
    snprintf(error, cap, "%s", text);
    return -1;
}
static bool whole(const cJSON *v, int64_t *out) {
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 9007199254740991.0 ||
        v->valuedouble != (double)(int64_t)v->valuedouble)
        return false;
    *out = (int64_t)v->valuedouble;
    return true;
}
static int by_offset(const void *a, const void *b) {
    const patchmanifest_piece *x = a, *y = b;
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

int patchmanifest_parse(const char *json, size_t length, patchmanifest *out, char *error, size_t cap) {
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return fail(error, cap, "unreadable manifest");
    }
    int64_t total;
    const cJSON *pieces = cJSON_GetObjectItemCaseSensitive(root, "pieces");
    if (!whole(cJSON_GetObjectItemCaseSensitive(root, "originalFileSize"), &total) || total <= 0 ||
        total > PATCHMANIFEST_MAX_TOTAL || !cJSON_IsArray(pieces)) {
        cJSON_Delete(root);
        return fail(error, cap, "manifest out of bounds");
    }
    int count = cJSON_GetArraySize(pieces);
    if (count <= 0 || count > PATCHMANIFEST_MAX_PIECES) {
        cJSON_Delete(root);
        return fail(error, cap, "manifest out of bounds");
    }
    patchmanifest_piece *list = calloc((size_t)count, sizeof *list);
    if (!list) {
        cJSON_Delete(root);
        return fail(error, cap, "not enough memory");
    }
    int index = 0;
    const cJSON *piece;
    cJSON_ArrayForEach(piece, pieces) {
        patchmanifest_piece *p = &list[index++];
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(piece, "url");
        const cJSON *hash = cJSON_GetObjectItemCaseSensitive(piece, "hashValue");
        size_t n = cJSON_IsString(hash) ? strlen(hash->valuestring) : 0;
        if (!cJSON_IsObject(piece) || !cJSON_IsString(url) || strlen(url->valuestring) >= sizeof p->url ||
            !whole(cJSON_GetObjectItemCaseSensitive(piece, "fileOffset"), &p->offset) ||
            !whole(cJSON_GetObjectItemCaseSensitive(piece, "fileSize"), &p->size) || (n != 40 && n != 64)) {
            free(list);
            cJSON_Delete(root);
            return fail(error, cap, "unreadable piece in the manifest");
        }
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)hash->valuestring[i];
            if (!isxdigit(c)) {
                free(list);
                cJSON_Delete(root);
                return fail(error, cap, "unreadable piece in the manifest");
            }
            p->hash[i] = (char)tolower(c);
        }
        snprintf(p->url, sizeof p->url, "%s", url->valuestring);
        if (!patchfeed_url_allowed(p->url)) {
            free(list);
            cJSON_Delete(root);
            return fail(error, cap, "a piece is not on Sony's download servers");
        }
    }
    cJSON_Delete(root);
    qsort(list, (size_t)count, sizeof *list, by_offset);
    int64_t position = 0;
    for (int i = 0; i < count; i++) {
        if (list[i].offset != position || list[i].size <= 0 || list[i].size > total - position) {
            free(list);
            return fail(error, cap, "pieces of the manifest do not follow each other");
        }
        position += list[i].size;
    }
    if (position != total) {
        free(list);
        return fail(error, cap, "pieces do not add up to the package size");
    }
    out->total = total;
    out->count = (size_t)count;
    out->pieces = list;
    return 0;
}
void patchmanifest_free(patchmanifest *manifest) {
    free(manifest->pieces);
    memset(manifest, 0, sizeof *manifest);
}
