/* Host test for backend/patchfeed.c: cc -std=c11 -Wall -Wextra -Werror -Ibackend backend/patchfeed.c tests/patchfeed_test.c */
#include "patchfeed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    char *b = malloc(1 << 20);
    *n = fread(b, 1, (1 << 20) - 1, f);
    b[*n] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    const char *fixture = argc > 1 ? argv[1] : "tests/fixtures/dirt5-version.xml";
    size_t n;
    char *xml = slurp(fixture, &n);
    patchfeed_package list[PATCHFEED_MAX];
    int count = patchfeed_parse(xml, n, list, PATCHFEED_MAX);
    CHECK(count == 1);
    CHECK(!strcmp(list[0].title_id, "PPSA01552"));
    CHECK(!strcmp(list[0].content_id, "UP4001-PPSA01552_00-DIRTFIVEPS5EDITI"));
    CHECK(!strcmp(list[0].version, "06.005.000"));
    CHECK(list[0].system_ver == 0x10200006u);
    CHECK(!list[0].mandatory);
    CHECK(!strcmp(list[0].manifest_url, "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA01552_00/app/info/76/f_8b647f994257b65e0387991ccfdf8403bee010d502db0f55d2e957e02c3cc63c/UP4001-PPSA01552_00-DIRTFIVEPS5EDITI.json"));
    CHECK(strstr(list[0].delta_url, "-DP.pkg") != NULL);

    char text[16];
    patchfeed_firmware_text(list[0].system_ver, text, sizeof text);
    CHECK(!strcmp(text, "10.20"));

    /* firmware: only major.minor count */
    CHECK(patchfeed_firmware_ok(0x10200006u, 0x10200000u));   /* console 10.20 takes 10.20.00.06 */
    CHECK(patchfeed_firmware_ok(0x10200006u, 0x13200000u));   /* console 13.20 */
    CHECK(!patchfeed_firmware_ok(0x10200006u, 0x10010000u));  /* console 10.01 */
    CHECK(!patchfeed_firmware_ok(0x10200006u, 0));            /* unknown console */

    /* choices */
    patchfeed_choice c = patchfeed_choose(list, (size_t)count, "06.004.000", 0x13200000u);
    CHECK(c.state == PATCHFEED_AVAILABLE && c.offer == &list[0]);
    c = patchfeed_choose(list, (size_t)count, "06.005.000", 0x13200000u);
    CHECK(c.state == PATCHFEED_UP_TO_DATE && !c.offer);
    c = patchfeed_choose(list, (size_t)count, "06.006.000", 0x13200000u);
    CHECK(c.state == PATCHFEED_UP_TO_DATE);
    c = patchfeed_choose(list, (size_t)count, "06.004.000", 0x10010000u);
    CHECK(c.state == PATCHFEED_NEEDS_NEWER_FW && c.offer == &list[0]);
    c = patchfeed_choose(list, (size_t)count, "", 0x13200000u);   /* installed version unknown */
    CHECK(c.state == PATCHFEED_AVAILABLE);

    /* several packages: best compatible wins, a newer blocked one does not hide it */
    patchfeed_package two[2] = {list[0], list[0]};
    strcpy(two[1].version, "06.006.000");
    two[1].system_ver = 0x11000000u;
    c = patchfeed_choose(two, 2, "06.004.000", 0x10200000u);
    CHECK(c.state == PATCHFEED_AVAILABLE && c.offer == &two[0]);
    c = patchfeed_choose(two, 2, "06.005.000", 0x10200000u);
    CHECK(c.state == PATCHFEED_NEEDS_NEWER_FW && c.offer == &two[1]);

    /* versions */
    CHECK(patchfeed_version_compare("06.005.000", "06.004.000") > 0);
    CHECK(patchfeed_version_compare("06.005.000", "06.005.000") == 0);
    CHECK(patchfeed_version_compare("01.000.000", "06.005.000") < 0);
    CHECK(patchfeed_version_compare("10.000.000", "09.999.999") > 0);
    CHECK(patchfeed_version_compare("junk", "01.000.000") < 0);
    CHECK(patchfeed_version_valid("06.005.000") && !patchfeed_version_valid("6.5") && !patchfeed_version_valid("06.005.00x"));

    /* addresses */
    CHECK(patchfeed_url_allowed(list[0].manifest_url));
    CHECK(!patchfeed_url_allowed("https://evil.example.com/a.json"));
    CHECK(!patchfeed_url_allowed("https://sgst.prod.dl.playstation.net.evil.com/a.json"));
    CHECK(!patchfeed_url_allowed("https://sgst.prod.dl.playstation.net:8443/a.json"));
    CHECK(!patchfeed_url_allowed("https://user:pw@sgst.prod.dl.playstation.net/a.json"));
    CHECK(!patchfeed_url_allowed("https://sgst.prod.dl.playstation.net/a.json?x=1"));
    CHECK(!patchfeed_url_allowed("https://sgst.prod.dl.playstation.net/a/../b.json"));
    CHECK(!patchfeed_url_allowed("ftp://sgst.prod.dl.playstation.net/a.json"));
    CHECK(!patchfeed_url_allowed("https://sgst.prod.dl.playstation.net"));
    CHECK(!patchfeed_url_allowed(NULL));
    char manifest[800];
    CHECK(patchfeed_manifest_from_link("https://sgst.prod.dl.playstation.net/x/y/UP4001-PPSA01552_00-DIRTFIVEPS5EDITI.crc", manifest, sizeof manifest));
    CHECK(!strcmp(manifest, "https://sgst.prod.dl.playstation.net/x/y/UP4001-PPSA01552_00-DIRTFIVEPS5EDITI.json"));
    CHECK(patchfeed_manifest_from_link("https://sgst.prod.dl.playstation.net/x/y/Z_sc.pkg", manifest, sizeof manifest));
    CHECK(!patchfeed_manifest_from_link("https://sgst.prod.dl.playstation.net/x/y/Z.exe", manifest, sizeof manifest));

    /* hostile or broken documents */
    CHECK(patchfeed_parse("not xml", 7, list, PATCHFEED_MAX) == -1);
    const char *bad_host = "<title_patch nptitleid=\"PPSA00001_00\"><app_tag content_id=\"X\"><package content_ver=\"01.000.000\" manifest_url=\"https://evil.example.com/a.json\" system_ver=\"1\"/></app_tag></title_patch>";
    CHECK(patchfeed_parse(bad_host, strlen(bad_host), list, PATCHFEED_MAX) == 0);
    const char *bad_version = "<title_patch nptitleid=\"PPSA00001_00\"><package content_ver=\"1.0\" manifest_url=\"https://sgst.prod.dl.playstation.net/a.json\" system_ver=\"1\"/></title_patch>";
    CHECK(patchfeed_parse(bad_version, strlen(bad_version), list, PATCHFEED_MAX) == 0);
    const char *no_end = "<title_patch nptitleid=\"PPSA00001_00\"><package content_ver=\"01.000.000\" manifest_url=\"https://sgst.prod.dl.playstation.net/a.json";
    CHECK(patchfeed_parse(no_end, strlen(no_end), list, PATCHFEED_MAX) == 0);
    const char *ps4 = "<title_patch titleid=\"CUSA00001\"><tag name=\"x\"><package version=\"01.05\" system_ver=\"0x6000000\" manifest_url=\"http://gst.prod.dl.playstation.net/a/b_0.pkg\"/></tag></title_patch>";
    CHECK(patchfeed_parse(ps4, strlen(ps4), list, PATCHFEED_MAX) == 0);   /* PS4 format is not handled yet: skipped, never misread */
    CHECK(patchfeed_parse(xml, n, list, 0) == -1);
    CHECK(patchfeed_parse(NULL, 0, list, PATCHFEED_MAX) == -1);
    free(xml);
    printf(failures ? "%d FAILED\n" : "all patchfeed tests passed\n", failures);
    return failures != 0;
}
