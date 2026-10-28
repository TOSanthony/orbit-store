import { useEffect, useState } from "react";
import { api } from "./api";
import { t } from "./i18n";
import { rich } from "./Rich";

export interface ArchiveAccountStatus {
  signedIn: boolean;
  name: string;
  busy: boolean;
}

// Archive.org serves some files to signed-in accounts only. The PS5 signs in
// once and keeps only Archive.org's session; the password is never saved.
export function ArchiveAccount() {
  const [status, setStatus] = useState<ArchiveAccountStatus | null>(null);
  const [email, setEmail] = useState(""),
    [password, setPassword] = useState("");
  const [sending, setSending] = useState(false),
    [error, setError] = useState("");
  useEffect(() => {
    let live = true;
    let retry: ReturnType<typeof setTimeout> | undefined;
    function refresh() {
      api<ArchiveAccountStatus>("/archive-account")
        .then((next) => {
          if (!live) return;
          setStatus(next);
          if (next.busy) retry = setTimeout(refresh, 2000);
        })
        .catch((e) => live && setError((e as Error).message));
    }
    refresh();
    return () => {
      live = false;
      clearTimeout(retry);
    };
  }, []);
  async function act(body: Record<string, string>) {
    setSending(true);
    setError("");
    try {
      setStatus(await api<ArchiveAccountStatus>("/archive-account", body));
      setEmail("");
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPassword("");
      setSending(false);
    }
  }
  const busy = sending || !!status?.busy;
  return (
    <section className="updates-panel archive-account-settings" aria-labelledby="archive-account-heading" tabIndex={-1}>
      <h2 id="archive-account-heading">Archive.org</h2>
      <p>{t("Some Archive.org games only download for signed-in accounts. Sign in with your Archive.org account to download them.")}</p>
      <p role="status">
        {status?.signedIn
          ? status.name
            ? t("Signed in as {name}", { name: status.name })
            : t("Signed in")
          : t("Not signed in")}
      </p>
      {status?.signedIn ? (
        <div className="dialog-actions">
          <button disabled={busy} onClick={() => void act({ action: "sign-out" })}>
            {t("Sign out")}
          </button>
        </div>
      ) : (
        <form
          onSubmit={(e) => {
            e.preventDefault();
            void act({ action: "sign-in", email: email.trim(), password });
          }}
        >
          <label className="field" htmlFor="archive-email">{t("Email")}</label>
          <input
            id="archive-email"
            type="email"
            autoComplete="username"
            spellCheck={false}
            value={email}
            disabled={busy || !status}
            onChange={(e) => setEmail(e.target.value)}
          />
          <label className="field" htmlFor="archive-password">{t("Password")}</label>
          <input
            id="archive-password"
            type="password"
            autoComplete="current-password"
            value={password}
            disabled={busy || !status}
            onChange={(e) => setPassword(e.target.value)}
          />
          <p className="fine">
            {t("Orbit sends these to Archive.org once and keeps only the sign-in on your PS5, never your password.")}
          </p>
          <p className="fine">
            {rich("No account? Create one for free at {site}.", {
              site: (
                <a href="https://archive.org/account/signup" target="_blank" rel="noreferrer">
                  archive.org
                </a>
              ),
            })}
          </p>
          <button type="submit" disabled={busy || !status || !email.trim() || !password}>
            {sending ? t("Signing in…") : t("Sign in")}
          </button>
        </form>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
