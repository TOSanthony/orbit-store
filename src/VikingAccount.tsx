import { useEffect, useState } from "react";
import { api } from "./api";
import { t } from "./i18n";

type Status = { id: string; vikingAccountAvailable?: boolean; vikingBrowserState?: string; accountPage?: boolean; active: boolean; state: string; message: string };

// Authentication stays on the official provider page in the same PS5 browser
// used for file capture. A successful launch is not a successful sign-in.
export function VikingAccount() {
  const [status, setStatus] = useState<Status | null>(null);
  const [sending, setSending] = useState(false);
  const [error, setError] = useState("");
  useEffect(() => {
    let live = true;
    api<Status>("/browser").then(s => { if (live) setStatus(s); })
      .catch(e => { if (live) setError(e.message); });
    return () => { live = false; };
  }, []);
  async function open(check = false) {
    if (sending) return;
    setSending(true); setError("");
    try {
      const opened = await api<Status>("/browser", { action: check ? "viking-check" : "viking-account", returnTarget: "browser" });
      // Report launch failures from the worker, without treating an open page
      // as proof of authentication. The user finishes sign-in on the PS5.
      for (let i = 0; i < 40; i++) {
        await new Promise(resolve => setTimeout(resolve, 250));
        const next = await api<Status & { id: string }>("/browser");
        if (next.id !== opened.id) break;
        setStatus(next);
        if (next.state === "failed") { setError(next.message); break; }
        if (!next.active) break;
      }
    } catch (e) { setError((e as Error).message); }
    finally { setSending(false); }
  }
  useEffect(() => {
    const refresh = () => { if (status?.vikingAccountAvailable && !status.active && !sending) void open(true); };
    window.addEventListener("focus", refresh);
    return () => window.removeEventListener("focus", refresh);
  }, [status?.active, status?.vikingAccountAvailable, sending]);
  return <section className="updates-panel archive-account-settings" aria-labelledby="viking-account-heading" tabIndex={-1}>
    <h2 id="viking-account-heading">Vikingfile</h2>
    <p>{t("Sign in on Vikingfile, then return to Orbit.")}</p>
    <p className="fine">{t("Orbit uses your Vikingfile sign-in when requesting download links. If verification is needed, its page opens.")}</p>
    <p className="fine">{t("Enter your Vikingfile account code (hash), complete verification if asked, then select Login. Return to Orbit when finished. Manage or sign out on the same account page.")}</p>
    <p>{status?.vikingBrowserState === "signed-in" ? t("Signed in") : status?.vikingBrowserState === "signed-out" ? t("Not signed in") : t("Sign-in could not be confirmed")}</p>
    <div className="dialog-actions"><button disabled={sending || !status?.vikingAccountAvailable || status.active}
      onClick={() => void open()}>{t("Open Vikingfile account")}</button>
      <button disabled={sending || !status?.vikingAccountAvailable || status.active}
        onClick={() => void open(true)}>{t("Check sign-in")}</button></div>
    {status && !status.vikingAccountAvailable && <p>{status.vikingAccountAvailable === undefined
      ? t("Update Orbit to open provider pages.") : t("Provider verification is available on the PS5 only.")}</p>}
    {error && <p className="form-error" role="alert">{t(error)}</p>}
  </section>;
}
