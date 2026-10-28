import { useState } from "react";
import { api } from "./api";
import { Icon } from "./components";
import { placeName, t } from "./i18n";
import { bytes, type Storage } from "./types";

// The default drive, as the TV app's Storage settings choose it: new downloads start
// with it selected under Save to. "" chooses automatically.
export function DefaultDrive({
  drives,
  preferred,
  refresh,
}: {
  drives: Storage[];
  preferred: string;
  refresh: () => Promise<unknown>;
}) {
  const [pending, setPending] = useState<string | null>(null),
    [error, setError] = useState("");
  const choose = async (id: string) => {
    if (id === preferred) return;
    setPending(id);
    setError("");
    try {
      await api("/storage/preferred", { storageId: id });
      await refresh();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending(null);
    }
  };
  const known = !preferred || drives.some((d) => d.id === preferred);
  const choice = (id: string, title: string, detail: string) => (
    <button
      key={id || "automatic"}
      type="button"
      role="radio"
      aria-checked={id === preferred}
      className="source-acknowledgement default-drive-choice"
      disabled={pending !== null}
      onClick={() => void choose(id)}
    >
      <span className="choice-check" aria-hidden="true">
        {id === preferred && <Icon name="check" />}
      </span>
      <span>
        <strong>{title}</strong>
        <small>{detail}</small>
      </span>
    </button>
  );
  return (
    <section className="archive-setting default-drive">
      <h2 className="download-storage-heading">{t("Default drive")}</h2>
      <p>
        {t(
          "New downloads start with this drive selected under Save to. Choosing another drive for a download also makes it the default.",
        )}
      </p>
      <div role="radiogroup" aria-label={t("Default drive")}>
        {choice("", t("Choose automatically"), t("The first USB or external drive, otherwise internal storage"))}
        {drives.map((d) => choice(d.id, placeName(d.label), t("{size} free", { size: bytes(d.freeBytes) })))}
      </div>
      {!known && (
        <p className="notice">
          {t("Your default drive isn’t connected. Until it is, Save to starts with the first drive available.")}
        </p>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
