#include "patchmanifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(name, cond) do { int ok_ = (cond); printf("%s %s\n", ok_ ? "ok  " : "FAIL", name); failures += !ok_; } while (0)

#define H64 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789ABCDEF"
#define URL "https://gs2.ww.prod.dl.playstation.net/x/a.pkg"

static int parse(const char *text, patchmanifest *m) {
    char error[160];
    return patchmanifest_parse(text, strlen(text), m, error, sizeof error);
}

int main(void) {
    patchmanifest m;
    CHECK("two pieces in order",
          parse("{\"originalFileSize\":300,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":100,\"fileSize\":200,\"hashValue\":\"" H64 "\"},"
                "{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":100,\"hashValue\":\"0123456789abcdef0123456789abcdef01234567\"}]}", &m) == 0 &&
          m.total == 300 && m.count == 2 && m.pieces[0].offset == 0 && m.pieces[0].size == 100 && m.pieces[1].offset == 100);
    CHECK("hashes are lowercased", m.count == 2 && !strcmp(m.pieces[1].hash, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    patchmanifest_free(&m);
    CHECK("a gap between pieces is refused",
          parse("{\"originalFileSize\":300,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":100,\"hashValue\":\"" H64 "\"},"
                "{\"url\":\"" URL "\",\"fileOffset\":150,\"fileSize\":150,\"hashValue\":\"" H64 "\"}]}", &m) < 0);
    CHECK("pieces that do not reach the total are refused",
          parse("{\"originalFileSize\":300,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":100,\"hashValue\":\"" H64 "\"}]}", &m) < 0);
    CHECK("a piece past the total is refused",
          parse("{\"originalFileSize\":100,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":200,\"hashValue\":\"" H64 "\"}]}", &m) < 0);
    CHECK("an address outside Sony is refused",
          parse("{\"originalFileSize\":100,\"pieces\":[{\"url\":\"https://evil.example/a.pkg\",\"fileOffset\":0,\"fileSize\":100,\"hashValue\":\"" H64 "\"}]}", &m) < 0);
    CHECK("a bad hash is refused",
          parse("{\"originalFileSize\":100,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":100,\"hashValue\":\"zz\"}]}", &m) < 0);
    CHECK("a negative size is refused",
          parse("{\"originalFileSize\":100,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":-5,\"hashValue\":\"" H64 "\"}]}", &m) < 0);
    CHECK("no pieces is refused", parse("{\"originalFileSize\":100,\"pieces\":[]}", &m) < 0);
    CHECK("a size over the limit is refused", parse("{\"originalFileSize\":1e15,\"pieces\":[]}", &m) < 0);
    CHECK("not JSON is refused", parse("hello", &m) < 0);
    CHECK("an array is refused", parse("[]", &m) < 0);
    CHECK("a zero total is refused", parse("{\"originalFileSize\":0,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":1,\"hashValue\":\"" H64 "\"}]}", &m) < 0);
    char noise[400];
    srand(5);
    const char *base = "{\"originalFileSize\":300,\"pieces\":[{\"url\":\"" URL "\",\"fileOffset\":0,\"fileSize\":300,\"hashValue\":\"" H64 "\"}]}";
    size_t length = strlen(base);
    int accepted = 0;
    for (int i = 0; i < 5000; i++) {
        memcpy(noise, base, length + 1);
        for (int k = 0; k < 3; k++) noise[rand() % length] = (char)(32 + rand() % 95);
        if (parse(noise, &m) == 0) { accepted++; patchmanifest_free(&m); }
    }
    CHECK("5000 corrupted manifests never crash", accepted >= 0);
    if (failures) { printf("%d failed\n", failures); return 1; }
    puts("all patchmanifest tests passed");
    return 0;
}
