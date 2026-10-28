import { useEffect, useRef, useState } from "react";
import { api } from "./api";
import { bytes } from "./types";
import type { UpdateStatus as Status, TvAppStatus } from "./updateStatus";
import { t } from "./i18n";
import { rich } from "./Rich";
import { readRestart, rememberRestart, checkRestart, clearRestart, restartStorage } from "./restartVerification";
export function Updates({ onStopped }: { onStopped: () => void }) {
  const [restart, setRestart] = useState(() => readRestart(restartStorage()));
  const [status, setStatus] = useState<Status | null>(null);
  const [tv, setTv] = useState<TvAppStatus | null>(null);
  const [error, setError] = useState("");
  const [sending, setSending] = useState(false);
  const [confirmStop, setConfirmStop] = useState(false);
  const [confirmRestart, setConfirmRestart] = useState(false);
  const [stopped, setStopped] = useState(false);
  // Orbit restarting itself: its port closes for a few seconds, then a new session answers.
  const [restarting, setRestarting] = useState(false);
  const restartingRef = useRef(false);
  restartingRef.current = restarting || status?.phase === "restarting";
  const [restartSince, setRestartSince] = useState(0);
  useEffect(() => setRestartSince(restarting ? Date.now() : 0), [restarting]);
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (stopped) return;
    let active = true;
    let loading = false;
    const refresh = async () => {
      if (loading) return;
      loading = true;
      try {
        const next = await api<Status>("/updates");
        const tvNext = await api<TvAppStatus>("/tv-app").catch(() => null);
        if (active) {
          setStatus(next);
          setTv(tvNext);
          setError("");
          if (next.phase !== "restarting") setRestarting(false);
          // Update Orbit reached its restart (from here or another device): check what comes back.
          else if (next.updateAll)
            setRestart((current) => current?.instance === next.instanceId ? current : rememberRestart(next, restartStorage()));
        }
      } catch (e) {
        if (active && !restartingRef.current) setError((e as Error).message);
        else if (active) setRestarting(true);
      } finally {
        loading = false;
      }
    };
    void refresh();
    const timer = setInterval(() => {
      setNow(Date.now());
      void refresh();
    }, 2000);
    return () => {
      active = false;
      clearInterval(timer);
    };
  }, [stopped]);
  async function act(action: string) {
    setSending(true);
    setError("");
    try {
      const next = await api<Status>("/updates", {
        action,
        ...(action === "install"
          ? { version: status?.latestVersion, checksum: status?.checksum }
          : action === "stop" || action === "restart"
            ? { confirmed: true }
            : {}),
      });
      setStatus(next);
      if (action === "stop") {
        setRestart(rememberRestart(next, restartStorage()));
        setStopped(true);
        onStopped();
      }
      if (action === "restart") {
        setRestart(rememberRestart(next, restartStorage()));
        setRestarting(true);
        setConfirmRestart(false);
      }
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setSending(false);
    }
  }
  // Waits for a check or install the service accepted, for up to a minute.
  async function settle<T extends { busy: boolean }>(path: string) {
    for (let i = 0; i < 120; i++) {
      const next = await api<T>(path);
      if (!next.busy) return next;
      await new Promise((done) => setTimeout(done, 500));
    }
    throw new Error(t("Update interrupted. Try again."));
  }
  // One press: confirm what's newer if the last check is old, then install it all.
  async function updateAll() {
    if (!status) return;
    setSending(true);
    setError("");
    try {
      const stale = (checkedAt: number) => Date.now() / 1000 - checkedAt > 840;
      const needed = (s: Status) => s.updateAvailable && !(s.restartRequired && s.savedVersion === s.latestVersion);
      let service = status, app = tv;
      // A check that failed says why (no network, GitHub's limit) better than the install would.
      if (needed(service) && stale(service.checkedAt)) {
        await api("/updates", { action: "check" });
        service = await settle<Status>("/updates");
        if (service.phase === "error" && service.error) throw new Error(service.error);
      }
      if (app?.updateAvailable && stale(app.checkedAt)) {
        await api("/tv-app", { action: "check" });
        app = await settle<TvAppStatus>("/tv-app");
        if (app.phase === "error" && app.error) throw new Error(app.error);
      }
      const next = await api<Status>("/updates", {
        action: "update-all",
        ...(needed(service) ? { serviceVersion: service.latestVersion, serviceChecksum: service.checksum } : {}),
        ...(app?.updateAvailable ? { tvAppVersion: app.latestVersion, tvAppChecksum: app.checksum } : {}),
      });
      setStatus(next);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setSending(false);
    }
  }
  const fresh = !!status?.checkedAt && now / 1000 - status.checkedAt <= 900;
  // While an update or restart is under way, the session that started it still answers.
  const restartResult =
    restart && status && !stopped && !restarting && !(status.instanceId === restart.instance && (status.updateAll || status.phase === "restarting"))
      ? checkRestart(restart, status)
      : null;
  const updating = !!status?.updateAll && (status.busy || status.phase === "restarting");
  const tvUpdate = !!tv?.available && tv.updateAvailable;
  // A release that's already saved only needs a restart, offered below.
  const serviceUpdate = !!status?.updateAvailable && !(status.restartRequired && status.savedVersion === status.latestVersion);
  const disabled = sending || status?.busy;
  const wait = Math.max(
    0,
    Math.ceil(((status?.retryAt || 0) * 1000 - now) / 1000),
  );
  // After a failed install, Orbit asks for a pause before the next one.
  const updateWait = Math.max(
    serviceUpdate ? wait : 0,
    tvUpdate ? Math.ceil(((tv?.retryAt || 0) * 1000 - now) / 1000) : 0,
  );
  const checkWait = Math.max(
    wait,
    Math.ceil(((status?.checkAfter || 0) * 1000 - now) / 1000),
  );
  return (
    <section className="updates-panel" aria-labelledby="updates-title">
      <h2 id="updates-title">{t("Update / reinstall")}</h2>
      {restarting && !stopped ? (
        <div className="notice" role="status">
          <strong>{t("Restarting Orbit…")}</strong>
          <p>{t("Downloads pause while Orbit restarts, then continue. This page reconnects by itself.")}</p>
          {restartSince > 0 && now - restartSince > 60000 && (
            <p>
              {rich("Orbit hasn’t answered yet. If it doesn’t come back, run {path} from your payload manager.", {
                path: <b>/data/orbit-store/orbit_store.elf</b>,
              })}
            </p>
          )}
        </div>
      ) : stopped ? (
        <div className="notice" role="status">
          <strong>{t("Orbit is stopping.")}</strong>
          <p>
            {rich(
              "Open your payload manager and run {path} once, or use a copy you opted to keep in sync. Then reopen the Orbit icon or reconnect here. Resume your downloads when you’re ready.",
              { path: <b>/data/orbit-store/orbit_store.elf</b> },
            )}
          </p>
          <button onClick={() => location.reload()}>
            {t("Reconnect to Orbit")}
          </button>
        </div>
      ) : !status ? (
        <p>{t("Loading installed version…")}</p>
      ) : (
        <>
          <dl className="update-versions">
            <div>
              <dt>{t("Running")}</dt>
              <dd>{status.runningVersion}</dd>
            </div>
            <div>
              <dt>{t("Saved for next start")}</dt>
              <dd>{status.savedVersion || t("Not recorded yet")}</dd>
            </div>
            {status.latestVersion && (
              <div>
                <dt>{t("Latest release")}</dt>
                <dd>{status.latestVersion}</dd>
              </div>
            )}
          </dl>
          {restartResult && (
            <div className="notice" role="status">
              <p>{restartResult === "verified"
                ? t("Orbit {version} restarted successfully.", { version: status.runningVersion })
                : restartResult === "wrong-version"
                  ? t("Orbit {running} is running, but {expected} was selected. Start the saved payload from /data/orbit-store/orbit_store.elf.", {
                      running: status.runningVersion, expected: restart!.version,
                    })
                  : restartResult === "previous"
                    ? t("The previous Orbit session is still running. Wait for it to stop, then start the saved payload.")
                    : t("The running version matches, but this older service cannot confirm a new session.")}
              </p>
              <button onClick={() => { clearRestart(restartStorage()); setRestart(null); }}>{t("Dismiss")}</button>
            </div>
          )}
          {!status.available ? (
            <p className="notice">
              {t(
                "Open Orbit on your PS5 or pair your phone with it to update the console’s saved app.",
              )}
            </p>
          ) : (
            <>
              {(serviceUpdate || tvUpdate || updating || (status.updateAll && status.phase === "updated")) && (
                <div className="notice update-all" role="status">
                  <strong>{t("Update Orbit")}</strong>
                  <p>
                    {serviceUpdate && tvUpdate
                      ? t("TV app {tv} and download service {service} are available.", { tv: tv!.latestVersion, service: status.latestVersion })
                      : serviceUpdate
                        ? t("Download service {version} is available.", { version: status.latestVersion })
                        : tvUpdate
                          ? t("TV app {version} is available.", { version: tv!.latestVersion })
                          : ""}
                  </p>
                  <p className="fine">{t("The TV app and the download service update together. Downloads pause while Orbit restarts, then continue.")}</p>
                  {(serviceUpdate || tvUpdate) && !updating && (
                    <div className="dialog-actions">
                      <button className="primary" disabled={disabled || updateWait > 0} onClick={() => void updateAll()}>
                        {t("Update Orbit")}
                      </button>
                    </div>
                  )}
                  <p className="fine">
                    {status.updateAll && status.phase === "tv-app" && tv?.busy
                      ? t("Downloading the TV app: {received} of {size}", { received: bytes(tv.received), size: bytes(tv.size) })
                      : status.updateAll && status.phase === "downloading"
                        ? t("Downloading Orbit: {size}", { size: bytes(status.received) })
                        : status.updateAll && status.phase === "saving"
                          ? t("Saving the verified release…")
                          : status.phase === "restarting"
                            ? t("Restarting Orbit…")
                            : (serviceUpdate || tvUpdate) && updateWait > 0
                              ? t("Please wait {seconds} s before another request.", { seconds: updateWait })
                              : ""}
                  </p>
                  {status.updateAll && status.tvAppUpdated && !status.busy && (
                    <p>
                      {t("TV app {version} is installed. Close Orbit Store on your PS5 (press the PS button, then Close Application) and open it again from your Games row.", {
                        version: status.tvAppUpdated,
                      })}
                    </p>
                  )}
                </div>
              )}
              <p>
                {t(
                  "Get the official release directly on your PS5. Orbit verifies its SHA-256 checksum and replaces its saved copy. Pairing, sources and your queue stay saved. Only manager copies you opted to sync are refreshed. A manually imported copy needs replacing if you have not enabled sync under Payload managers.",
                )}
              </p>
              <div className="dialog-actions">
                <button
                  disabled={disabled || checkWait > 0}
                  onClick={() => void act("check")}
                >
                  {status.phase === "checking"
                    ? t("Checking…")
                    : checkWait > 0
                      ? t("Check again in {seconds} s", { seconds: checkWait })
                      : t("Check for updates")}
                </button>
                {status.latestVersion && (
                  <button
                    className="primary"
                    disabled={disabled || wait > 0 || !fresh}
                    onClick={() => void act("install")}
                  >
                    {status.phase === "downloading" || status.phase === "saving"
                      ? t("Installing…")
                      : status.latestVersion === status.runningVersion
                        ? t("Reinstall release")
                        : t("Install update")}
                  </button>
                )}
              </div>
              <p className="fine" role="status">
                {/* During Update Orbit, its own notice shows the progress. */}
                {status.phase === "downloading" && !status.updateAll
                  ? t("Downloading Orbit: {size}", {
                      size: bytes(status.received),
                    })
                  : status.phase === "saving" && !status.updateAll
                    ? t("Saving the verified release…")
                    : wait > 0
                      ? t("Please wait {seconds} s before another request.", {
                          seconds: wait,
                        })
                      : status.phase === "checked" &&
                          status.latestVersion === status.runningVersion
                        ? t(
                            "You’re running the latest published version. You can reinstall it if needed.",
                          )
                        : ""}
              </p>
              {status.latestVersion && !fresh && !status.busy && (
                <p className="fine">
                  {t(
                    "Check for updates to confirm this release before installing.",
                  )}
                </p>
              )}
              {status.restartRequired && (
                <div className="notice" role="status">
                  <strong>{t("Restart needed")}</strong>
                  <p>
                    {t(
                      "The saved version starts the next time you run Orbit. The current session stays open until you stop it.",
                    )}
                  </p>
                </div>
              )}
              {status.selfRestart && !confirmStop && (
                !confirmRestart ? (
                  <button
                    className={status.restartRequired ? "primary" : undefined}
                    disabled={disabled}
                    onClick={() => setConfirmRestart(true)}
                  >
                    {t("Restart Orbit")}
                  </button>
                ) : (
                  <div className="notice">
                    <p>{t("Restart Orbit now? Active downloads pause and continue once Orbit is back.")}</p>
                    <div className="dialog-actions">
                      <button className="primary" disabled={disabled} onClick={() => void act("restart")}>
                        {t("Restart now")}
                      </button>
                      <button disabled={disabled} onClick={() => setConfirmRestart(false)}>
                        {t("Keep running")}
                      </button>
                    </div>
                  </div>
                )
              )}
              {confirmRestart ? null : !confirmStop ? (
                <button
                  disabled={disabled}
                  onClick={() => setConfirmStop(true)}
                >
                  {t("Stop Orbit to restart")}
                </button>
              ) : (
                <div className="notice">
                  <p>
                    {rich(
                      "Stop Orbit and pause active downloads? Then run {path} (or a synced copy) from your payload manager and reopen Orbit. Other payloads keep running.",
                      { path: <b>/data/orbit-store/orbit_store.elf</b> },
                    )}
                  </p>
                  <div className="dialog-actions">
                    <button
                      disabled={disabled}
                      onClick={() => void act("stop")}
                    >
                      {t("Stop Orbit")}
                    </button>
                    <button
                      disabled={disabled}
                      onClick={() => setConfirmStop(false)}
                    >
                      {t("Keep running")}
                    </button>
                  </div>
                </div>
              )}
            </>
          )}
          {status.error && (
            <p className="form-error" role="alert">
              {t(status.error)}
            </p>
          )}
        </>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
