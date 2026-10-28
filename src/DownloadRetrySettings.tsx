import { useState } from "react";
import { api } from "./api";
import { t } from "./i18n";
import type { System } from "./types";
const fields = [
  ["downloadRetries", /* i18n */ "Automatic retries", 0, 10, 3],
  ["downloadRetryDelay", /* i18n */ "Wait between retries (seconds)", 1, 300, 10],
  ["downloadStallTimeout", /* i18n */ "Wait before reconnecting (seconds)", 15, 600, 60],
  ["downloadMinSpeed", /* i18n */ "Reconnect below this speed (KiB/s)", 1, 1024, 1],
] as const;

function RetryField({ field, settings, refresh }: { field: typeof fields[number]; settings: System; refresh: () => Promise<unknown> }) {
  const [key, label, min, max, fallback] = field;
  const value = settings[key] ?? fallback;
  const [draft, setDraft] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");
  const text = draft ?? String(value), number = Number(text);
  const valid = text.trim() !== "" && Number.isInteger(number) && number >= min && number <= max;
  async function save() {
    if (!valid || saving) return;
    setSaving(true); setError("");
    try {
      await api("/settings/downloads", { [key]: number });
      await refresh(); setDraft(null);
    } catch (error) { setError((error as Error).message); }
    finally { setSaving(false); }
  }
  return <div className="download-retry-field">
    <label htmlFor={key}>{t(label)}</label>
    <div className="download-limit-controls">
      <input id={key} type="number" min={min} max={max} step="1" inputMode="numeric"
        value={text} disabled={saving} onChange={event => setDraft(event.target.value)} />
      <button type="button" disabled={saving || !valid || number === value} onClick={() => void save()}>{t("Save")}</button>
    </div>
    {!valid && <p role="alert">{t("Enter a whole number from {min} to {max}.", { min, max })}</p>}
    {error && <p className="form-error" role="alert">{error}</p>}
  </div>;
}
export function DownloadRetrySettings({ settings, refresh }: { settings: System; refresh: () => Promise<unknown> }) {
  return <section className="download-retry-settings">
    <h2>{t("Download recovery")}</h2>
    <p>{t("Orbit can try again if a download fails or gets stuck. Your progress is kept. Set retries to 0 to turn this off.")}</p>
    <p>{t("Orbit reconnects if the speed stays below your chosen limit for the time you set. Changes apply on the next try. Some websites may require a longer wait.")}</p>
    {fields.map(field => <RetryField key={field[0]} field={field} settings={settings} refresh={refresh} />)}
  </section>;
}
