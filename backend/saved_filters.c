#include "orbit.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
/* Browse's saved filters: a name, the page they belong to (games or add-ons)
 * and every filter with the sort and the search text. They live in Orbit's
 * state on the PS5, so the browser, the TV app and Orbit Zero share them. */
#define SAVED_FILTER_LIMIT 50
#define NAME_LIMIT 40 /* characters */
#define CHOICE_LIMIT 32

static bool text_ok(const char *s, size_t max) { return strlen(s) <= max && utf8_text(s); }
static bool one_of(const char *s, const char *const *values) {
    for (; *values; values++)
        if (!strcmp(s, *values)) return true;
    return false;
}
/* A string field: missing is fallback; anything but a short string is refused. */
static bool text_field(const cJSON *in, const char *key, size_t max, const char *const *values, const char *fallback, cJSON *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(in, key);
    const char *s = v ? (cJSON_IsString(v) ? v->valuestring : NULL) : fallback;
    if (!s || !text_ok(s, max) || (values && !one_of(s, values))) return false;
    cJSON_AddStringToObject(out, key, s);
    return true;
}
/* Sources, formats or tags: distinct short strings. */
static bool list_field(const cJSON *in, const char *key, cJSON *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(in, key), *x;
    cJSON *list = cJSON_AddArrayToObject(out, key);
    if (!v) return true;
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) > CHOICE_LIMIT) return false;
    cJSON_ArrayForEach(x, v) {
        if (!cJSON_IsString(x) || !*x->valuestring || !text_ok(x->valuestring, 64)) return false;
        const cJSON *seen;
        bool duplicate = false;
        cJSON_ArrayForEach(seen, list) duplicate |= !strcmp(seen->valuestring, x->valuestring);
        if (!duplicate) cJSON_AddItemToArray(list, cJSON_CreateString(x->valuestring));
    }
    return true;
}
/* The filters as Browse keeps them (src/browse.ts BrowseFilters), complete, or NULL. */
static cJSON *clean_filters(const cJSON *in) {
    static const char *const platforms[] = {"", "ps4", "ps5", NULL};
    static const char *const sizes[] = {"", "small", "medium", "large", "huge", NULL};
    static const char *const collections[] = {"all", "favorites", NULL};
    static const char *const sorts[] = {"release", "added", "title", "title-desc", "size", "size-desc", NULL};
    if (!cJSON_IsObject(in)) return NULL;
    cJSON *out = cJSON_CreateObject();
    const cJSON *hide = cJSON_GetObjectItemCaseSensitive(in, "hideInstalled");
    bool ok = text_field(in, "query", 200, NULL, "", out) && list_field(in, "source", out) &&
        list_field(in, "format", out) && list_field(in, "tags", out) &&
        text_field(in, "region", 16, NULL, "", out) && text_field(in, "platform", 8, platforms, "", out) &&
        text_field(in, "size", 8, sizes, "", out) && text_field(in, "collection", 16, collections, "all", out) &&
        (!hide || cJSON_IsBool(hide)) && text_field(in, "sort", 16, sorts, "release", out) &&
        text_field(in, "addonType", 32, NULL, "", out);
    cJSON_AddBoolToObject(out, "hideInstalled", cJSON_IsTrue(hide));
    if (!ok) cJSON_Delete(out);
    return ok ? out : NULL;
}
/* Up to NAME_LIMIT characters, not blank, with no edge spaces. */
static bool name_ok(const char *name) {
    size_t chars = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) chars += (*p & 0xc0) != 0x80;
    size_t n = strlen(name);
    return n && chars <= NAME_LIMIT && utf8_text(name) && name[0] != ' ' && name[n - 1] != ' ';
}
static bool page_ok(const char *page) { return !strcmp(page, "games") || !strcmp(page, "addons"); }
/* A saved filter from the state file, checked as a request would be. */
static cJSON *clean_entry(const cJSON *in) {
    const char *id = json_text(in, "id"), *name = json_text(in, "name"), *page = json_text(in, "page");
    cJSON *filters = clean_filters(cJSON_GetObjectItemCaseSensitive(in, "filters"));
    if (!filters || strlen(id) != 17 || id[0] != 'f' || !name_ok(name) || !page_ok(page)) {
        cJSON_Delete(filters);
        return NULL;
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "id", id);
    cJSON_AddStringToObject(out, "name", name);
    cJSON_AddStringToObject(out, "page", page);
    cJSON_AddItemToObject(out, "filters", filters);
    cJSON_AddNumberToObject(out, "savedAt", (double)json_int(in, "savedAt"));
    return out;
}
cJSON *saved_filters_json_locked(void) {
    return orbit.saved_filters ? cJSON_Duplicate(orbit.saved_filters, true) : cJSON_CreateArray();
}
void saved_filters_load_locked(const cJSON *saved) {
    cJSON_Delete(orbit.saved_filters);
    orbit.saved_filters = cJSON_CreateArray();
    const cJSON *x, *y;
    cJSON_ArrayForEach(x, saved) {
        if (cJSON_GetArraySize(orbit.saved_filters) >= SAVED_FILTER_LIMIT) break;
        cJSON *entry = clean_entry(x);
        bool duplicate = false;
        cJSON_ArrayForEach(y, orbit.saved_filters)
            duplicate |= entry && (!strcmp(json_text(y, "id"), json_text(entry, "id")) ||
                                   (!strcmp(json_text(y, "page"), json_text(entry, "page")) &&
                                    !strcasecmp(json_text(y, "name"), json_text(entry, "name"))));
        if (entry && !duplicate) cJSON_AddItemToArray(orbit.saved_filters, entry);
        else cJSON_Delete(entry);
    }
}
/* {"action":"save","name","page","filters"}: saving under a name the page
 * already has replaces that filter. {"action":"delete","id"}. */
int saved_filters_change_locked(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    if (orbit.stop) {
        copy_text(error, cap, "Orbit is stopping.");
        return 503;
    }
    if (!orbit.saved_filters) orbit.saved_filters = cJSON_CreateArray();
    cJSON *previous = cJSON_Duplicate(orbit.saved_filters, true), *x;
    if (!strcmp(action, "save")) {
        const char *name = json_text(input, "name"), *page = json_text(input, "page");
        cJSON *filters = clean_filters(cJSON_GetObjectItemCaseSensitive(input, "filters"));
        if (!name_ok(name) || !page_ok(page) || !filters) {
            cJSON_Delete(filters);
            cJSON_Delete(previous);
            copy_text(error, cap, !name_ok(name) ? "Give the filters a name of up to 40 characters." : "These filters can't be saved.");
            return 400;
        }
        cJSON *same = NULL;
        cJSON_ArrayForEach(x, orbit.saved_filters)
            if (!strcmp(json_text(x, "page"), page) && !strcasecmp(json_text(x, "name"), name)) same = x;
        if (!same && cJSON_GetArraySize(orbit.saved_filters) >= SAVED_FILTER_LIMIT) {
            cJSON_Delete(filters);
            cJSON_Delete(previous);
            copy_text(error, cap, "You have 50 saved filters. Delete one before saving another.");
            return 409;
        }
        cJSON *entry = cJSON_CreateObject();
        char hex[17];
        random_hex(hex, 8);
        char id[18];
        snprintf(id, sizeof id, "f%.16s", hex);
        cJSON_AddStringToObject(entry, "id", same ? json_text(same, "id") : id);
        cJSON_AddStringToObject(entry, "name", name);
        cJSON_AddStringToObject(entry, "page", page);
        cJSON_AddItemToObject(entry, "filters", filters);
        cJSON_AddNumberToObject(entry, "savedAt", (double)time(NULL));
        if (same) cJSON_ReplaceItemViaPointer(orbit.saved_filters, same, entry);
        else cJSON_AddItemToArray(orbit.saved_filters, entry);
    } else if (!strcmp(action, "delete")) {
        cJSON *found = NULL;
        cJSON_ArrayForEach(x, orbit.saved_filters)
            if (!strcmp(json_text(x, "id"), json_text(input, "id"))) found = x;
        if (!found) {
            cJSON_Delete(previous);
            copy_text(error, cap, "These saved filters were already deleted.");
            return 404;
        }
        cJSON_Delete(cJSON_DetachItemViaPointer(orbit.saved_filters, found));
    } else {
        cJSON_Delete(previous);
        copy_text(error, cap, "Expected save or delete.");
        return 400;
    }
    if (state_save_locked()) {
        cJSON_Delete(orbit.saved_filters);
        orbit.saved_filters = previous;
        copy_text(error, cap, "Could not save your filters.");
        return 503;
    }
    cJSON_Delete(previous);
    return 200;
}
