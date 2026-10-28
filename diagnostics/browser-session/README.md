# PS5 browser session probe

This is development tooling for Vikingfile account integration, not a sign-in
implementation. It tests cookie and web-storage lifetime using dummy data on a
local HTTP origin. It never opens Vikingfile, reads account credentials, scans
browser memory, changes Orbit settings or downloads a game.

The console's browser and the native app's browser dialog may use different
storage. Test both. A successful HTTP test does **not** establish Secure-cookie
behaviour, real provider sign-in or premium entitlement.

## Native probe

Run `prepare.py --host <computer LAN IPv4> --run <unique test ID>` to create an
isolated app build under `.state/viking-console-session/app`. The normal app's
sources and package stay unchanged. The test title is **Orbit Browser Session
Test**, `PPSA99179`; check that this title ID is unused before installing it.

Build that workspace with the regular native build image and
`make ffpkg ORBIT_PAYLOAD=none`. It carries no Orbit receiver and does not start
one. It opens the seed page, closes it after twelve seconds, opens a check page
in a fresh dialog, and then clears its dummy data in a third dialog. A dark green
background marks completion of the three-page sequence; a red background
marks a failed lifecycle. Only the server report can establish cookie retention.
The tester initializes the user service through the same controller setup as
regular Orbit. Close the test title normally through the PS5 UI.

Run the HTTP server on the same computer and test ID:

```sh
python3 diagnostics/browser-session/server.py \
  --host <computer LAN IPv4> --peer <PS5 LAN IPv4> \
  --run <unique test ID> --report <local report path>
```

Only requests from the specified PS5 are accepted. The report records receipt
of dummy session/persistent HttpOnly cookies, local/session-storage booleans and
native lifecycle return codes. It never logs request headers or cookie values.
The server uses no cross-origin access rules or credential-forwarding route.

## Console browser probe

Build `launcher.c` with `backend/browser_services.c`, `ORBIT_BROWSER_PROBE`, and
a fixed `ORBIT_SESSION_URL` pointing to `/<test ID>/seed/shell`. Run that one-shot
ELF only after owner approval. Close the console browser, then launch a second
ELF pointing to `check/shell`. Finish with `clear/shell`. These launchers don't
install Orbit or change autoload entries.

Running a check without closing the window only tests navigation, not session
persistence. Keep those observations distinct in the report. If a run is
interrupted, reopen its `clear` page before stopping the server; it deletes only
the probe's two cookies and its two storage keys.

## Local verification

`python3 diagnostics/browser-session/test_server.py` checks HTTP responses, peer
restriction, scoped HttpOnly cookies, cleanup and redacted reports. These tests
do not stand in for console results.
