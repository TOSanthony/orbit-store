import { useState } from "react";
import { api } from "./api";
import { Icon } from "./components";
import { t } from "./i18n";
import { NtfySetting } from "./NtfySetting";
import type { NotificationEvent, System } from "./types";

// The events Orbit can tell you about, in the order the TV app lists them.
export const notificationEvents: { id: NotificationEvent; label: string }[] = [
  { id: "downloadFinished", label: /* i18n */ "Download finished" },
  { id: "downloadFailed", label: /* i18n */ "Download failed" },
  { id: "installFinished", label: /* i18n */ "Package installed" },
  { id: "installFailed", label: /* i18n */ "Installation failed" },
  { id: "updateAvailable", label: /* i18n */ "Update available" },
];

// Notifications: where they go (this PS5, ntfy) and which events they cover, one
// list for both.
export function NotificationSettings({
  system,
  refresh,
}: {
  system: System;
  refresh: () => Promise<unknown>;
}) {
  const [pending, setPending] = useState(""),
    [error, setError] = useState(""),
    // As in the TV app: ntfy's state and a button, its form only when asked for.
    [ntfyOpen, setNtfyOpen] = useState(false);
  const onConsole = system.downloadNotifications !== false;
  const events = system.notificationEvents;
  const save = async (key: string, body: object) => {
    setPending(key);
    setError("");
    try {
      await api("/settings/notifications", body);
      await refresh();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending("");
    }
  };
  const check = (key: string, on: boolean, label: string, change: () => void) => (
    <button
      key={key}
      type="button"
      role="checkbox"
      aria-checked={on}
      className="source-acknowledgement"
      disabled={!!pending}
      onClick={change}
    >
      <span className="choice-check" aria-hidden="true">
        {on && <Icon name="check" />}
      </span>
      <span>{label}</span>
    </button>
  );
  return (
    <>
      <section className="archive-setting notification-setting">
        <h2 className="download-storage-heading">{t("Where to notify")}</h2>
        <p>{t("Orbit tells you about the events you choose on your PS5, even while you play, and on your phone or computer through ntfy.")}</p>
        {check("console", onConsole, t("On this PS5"), () => void save("console", { console: !onConsole }))}
        {typeof system.ntfyEnabled === "boolean" && !ntfyOpen && (
          <div className="notification-ntfy">
            <p>
              {t("ntfy notifications")} · {system.ntfyEnabled ? t("On") : t("Off")}
            </p>
            <button type="button" onClick={() => setNtfyOpen(true)}>
              {system.ntfyEnabled ? t("Change ntfy settings") : t("Set up ntfy")}
            </button>
          </div>
        )}
      </section>
      {typeof system.ntfyEnabled === "boolean" && ntfyOpen && <NtfySetting refresh={refresh} />}
      {events && (
        <section className="archive-setting notification-events">
          <h2 className="download-storage-heading">{t("What to notify about")}</h2>
          <p>{t("Your choices apply to this PS5 and to ntfy.")}</p>
          {notificationEvents.map((event) =>
            check(event.id, events[event.id], t(event.label), () =>
              void save(event.id, { events: { [event.id]: !events[event.id] } }),
            ),
          )}
          {events.updateAvailable && (
            <p className="fine">{t("To tell you about updates, Orbit checks for a new release every six hours.")}</p>
          )}
        </section>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </>
  );
}
