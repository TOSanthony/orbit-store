import { useState } from "react";
import { api } from "./api";
import { t } from "./i18n";

export function DownloadLimit({ value, refresh }: { value: number; refresh: () => Promise<unknown> }) {
  const [draft, setDraft] = useState<string | null>(null);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState("");
  const text = draft ?? String(value);
  const number = Number(text);
  const valid = text.trim() !== "" && Number.isInteger(number) && number >= 1 && number <= 8;
  async function save() {
    if (!valid || pending) return;
    setPending(true); setError("");
    try {
      await api("/settings/downloads", { concurrentDownloads: number });
      await refresh(); setDraft(null);
    } catch (e) { setError((e as Error).message); }
    finally { setPending(false); }
  }
  return <section className="download-limit-setting">
    <h2 className="download-storage-heading">{t("Simultaneous downloads")}</h2>
    <p id="download-limit-help">{t("Choose 1–8 games. One by default. Lowering the limit lets current downloads finish.")}</p>
    <div className="download-limit-controls">
      <button type="button" aria-label={t("Decrease download limit")} disabled={pending || number <= 1}
        onClick={() => setDraft(String(Math.max(1, (valid ? number : value) - 1)))}>−</button>
      <input type="number" min="1" max="8" step="1" inputMode="numeric" aria-label={t("Simultaneous downloads")}
        aria-describedby="download-limit-help" value={text} disabled={pending}
        onChange={e => setDraft(e.target.value)} />
      <button type="button" aria-label={t("Increase download limit")} disabled={pending || number >= 8}
        onClick={() => setDraft(String(Math.min(8, (valid ? number : value) + 1)))}>+</button>
      <button type="button" disabled={pending || !valid || number === value} onClick={() => void save()}>{t("Save")}</button>
    </div>
    {!valid && <p className="form-error">{t("Choose a whole number from 1 to 8.")}</p>}
    {error && <p className="form-error" role="alert">{error}</p>}
  </section>;
}
