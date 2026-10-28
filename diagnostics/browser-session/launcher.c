/* One-shot ShellUI session probe. Launches only the compiled-in test page. */
#include "browser_platform.h"
#include <stdio.h>
#include <unistd.h>
#ifndef ORBIT_SESSION_URL
#error Supply the explicit dummy test page URL
#endif
int main(void) {
    int rc = probe_browser_open(ORBIT_SESSION_URL);
    printf("Orbit browser session probe launch: %d\n", rc);
    sleep(2);
    return rc ? 1 : 0;
}
