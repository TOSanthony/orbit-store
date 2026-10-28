import { useEffect, useState } from "react";
import { api, ApiError } from "./api";
import { Icon } from "./components";
import { t } from "./i18n";

export interface NtfyStatus {
  enabled: boolean;
  server: string;
  topic: string;
  username: string;
  hasPassword: boolean;
}

// ntfy push notifications: the PS5 tells the owner's ntfy server and topic when
// a download finishes or fails, so a subscribed phone or computer hears about it.
// The password is kept on the PS5 and never sent back.
export function NtfySetting({ refresh }: { refresh: () => Promise<unknown> }) {
  const [saved, setSaved] = useState<NtfyStatus | null>(null);
  const [missing, setMissing] = useState(false);
  const [enabled, setEnabled] = useState(false),
    [server, setServer] = useState(""),
    [topic, setTopic] = useState(""),
    [auth, setAuth] = useState(false),
    [username, setUsername] = useState(""),
    [password, setPassword] = useState("");
  const [pending, setPending] = useState(""),
    [error, setError] = useState(""),
    [message, setMessage] = useState("");
  const show = (status: NtfyStatus) => {
    setSaved(status);
    setEnabled(status.enabled);
    setServer(status.server);
    setTopic(status.topic);
    setAuth(status.hasPassword);
    setUsername(status.username);
    setPassword("");
  };
  useEffect(() => {
    let live = true;
    api<NtfyStatus>("/settings/ntfy")
      .then((status) => live && show(status))
      // An older download service has no ntfy support: say nothing about it.
      .catch((e) => live && (e instanceof ApiError && e.status === 404 ? setMissing(true) : setError((e as Error).message)));
    return () => {
      live = false;
    };
  }, []);
  if (missing) return null;
  const run = async (action: "save" | "test") => {
    setPending(action);
    setError("");
    setMessage("");
    try {
      if (action === "save") {
        show(await api<NtfyStatus>("/settings/ntfy", { enabled, server: server.trim(), topic: topic.trim(), auth, username, password }));
        await refresh();
        setMessage(t("ntfy settings saved."));
      } else {
        await api("/settings/ntfy/test", {});
        setMessage(t("Test notification sent. Check the ntfy app on your phone or computer."));
      }
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending("");
    }
  };
  const changed = !!saved && (enabled !== saved.enabled || server.trim() !== saved.server || topic.trim() !== saved.topic ||
    auth !== saved.hasPassword || username !== saved.username || !!password);
  const busy = !!pending || !saved;
  const check = (on: boolean, change: (value: boolean) => void, label: string) => (
    <button type="button" role="checkbox" aria-checked={on} className="source-acknowledgement" disabled={busy} onClick={() => change(!on)}>
      <span className="choice-check" aria-hidden="true">{on && <Icon name="check" />}</span>
      <span>{label}</span>
    </button>
  );
  return (
    <section className="archive-setting ntfy-setting">
      <h2 className="download-storage-heading">{t("ntfy notifications")}</h2>
      <p>{t("Get a push notification on your phone or computer for the events you choose. Subscribe to the same topic in the ntfy app.")}</p>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          void run("save");
        }}
      >
        {check(enabled, setEnabled, t("Send ntfy notifications"))}
        <label className="field">
          {t("Server address")}
          <input type="url" inputMode="url" spellCheck={false} autoCapitalize="off" placeholder="https://ntfy.sh" maxLength={256}
            value={server} disabled={busy} onChange={(e) => setServer(e.target.value)} />
        </label>
        <label className="field">
          {t("Topic")}
          <input type="text" spellCheck={false} autoCapitalize="off" placeholder="orbit" maxLength={64}
            value={topic} disabled={busy} onChange={(e) => setTopic(e.target.value)} />
        </label>
        <p className="fine">{t("Anyone who knows a topic on a public server can read it, so choose one that’s hard to guess.")}</p>
        {check(auth, setAuth, t("Use authentication"))}
        {auth && (
          <>
            <label className="field">
              {t("Username")}
              <input type="text" autoComplete="off" spellCheck={false} autoCapitalize="off" maxLength={128}
                value={username} disabled={busy} onChange={(e) => setUsername(e.target.value)} />
            </label>
            <label className="field">
              {t("Password")}
              <input type="password" autoComplete="new-password" maxLength={128}
                placeholder={saved?.hasPassword && username === saved.username ? t("Saved. Type a new one to change it.") : ""}
                value={password} disabled={busy} onChange={(e) => setPassword(e.target.value)} />
            </label>
            <p className="fine">{t("For an access token, leave the username empty and enter the token as the password.")}</p>
          </>
        )}
        <div className="dialog-actions">
          <button type="submit" className="primary" disabled={busy || !changed}>
            {pending === "save" ? t("Saving…") : t("Save")}
          </button>
          <button type="button" disabled={busy || changed || !saved?.server || !saved.topic} onClick={() => void run("test")}>
            {pending === "test" ? t("Sending…") : t("Send test notification")}
          </button>
        </div>
      </form>
      {message && <p role="status">{message}</p>}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
