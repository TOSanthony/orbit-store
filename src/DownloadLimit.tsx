import { useState } from "react";
import { api } from "./api";
import { t } from "./i18n";

// A whole number saved to /settings/downloads, with − and + beside it.
function NumberSetting({ field, value, min, max, title, help, less, more, invalid, refresh }: {
  field: string; value: number; min: number; max: number; title: string; help: string;
  less: string; more: string; invalid: string; refresh: () => Promise<unknown>;
}) {
  const [draft, setDraft] = useState<string | null>(null);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState("");
  const text = draft ?? String(value);
  const number = Number(text);
  const valid = text.trim() !== "" && Number.isInteger(number) && number >= min && number <= max;
  async function save() {
    if (!valid || pending) return;
    setPending(true); setError("");
    try {
      await api("/settings/downloads", { [field]: number });
      await refresh(); setDraft(null);
    } catch (e) { setError((e as Error).message); }
    finally { setPending(false); }
  }
  return <section className="download-limit-setting">
    <h2 className="download-storage-heading">{title}</h2>
    <p id={`${field}-help`}>{help}</p>
    <div className="download-limit-controls">
      <button type="button" aria-label={less} disabled={pending || number <= min}
        onClick={() => setDraft(String(Math.max(min, (valid ? number : value) - 1)))}>−</button>
      <input type="number" min={min} max={max} step="1" inputMode="numeric" aria-label={title}
        aria-describedby={`${field}-help`} value={text} disabled={pending}
        onChange={e => setDraft(e.target.value)} />
      <button type="button" aria-label={more} disabled={pending || number >= max}
        onClick={() => setDraft(String(Math.min(max, (valid ? number : value) + 1)))}>+</button>
      <button type="button" disabled={pending || !valid || number === value} onClick={() => void save()}>{t("Save")}</button>
    </div>
    {!valid && <p className="form-error">{invalid}</p>}
    {error && <p className="form-error" role="alert">{error}</p>}
  </section>;
}
export function DownloadLimit({ value, refresh }: { value: number; refresh: () => Promise<unknown> }) {
  return <NumberSetting field="concurrentDownloads" value={value} min={1} max={8} refresh={refresh}
    title={t("Simultaneous downloads")} help={t("Choose 1–8 games. One by default. Lowering the limit lets current downloads finish.")}
    less={t("Decrease download limit")} more={t("Increase download limit")} invalid={t("Choose a whole number from 1 to 8.")} />;
}
// Connections per download (issue #86): how many ranges a new download is split into.
export function DownloadConnections({ value, refresh }: { value: number; refresh: () => Promise<unknown> }) {
  return <NumberSetting field="downloadConnections" value={value} min={1} max={64} refresh={refresh}
    title={t("Connections per download")}
    help={t("Choose 1–64. Eight by default. Downloads running at once share up to 64, and one already started keeps its split. More connections can speed up slow sources that allow them.")}
    less={t("Decrease connections")} more={t("Increase connections")} invalid={t("Choose 1 to 64 connections per download.")} />;
}
