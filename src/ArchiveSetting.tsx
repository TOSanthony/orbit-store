import { useState } from "react";
import { api } from "./api";
import { Icon } from "./components";
import { t } from "./i18n";

// Whether a ZIP, 7z or RAR download is deleted once its files are extracted.
export function ArchiveSetting({
  enabled,
  refresh,
}: {
  enabled: boolean;
  refresh: () => Promise<unknown>;
}) {
  const [pending, setPending] = useState(false),
    [error, setError] = useState("");
  const change = async () => {
    setPending(true);
    setError("");
    try {
      await api("/settings/extraction", { deleteArchives: !enabled });
      await refresh();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending(false);
    }
  };
  return (
    <section className="archive-setting">
      <h2 className="download-storage-heading">{t("Compressed downloads")}</h2>
      <p>
        {t(
          "ZIP, 7z and RAR downloads are extracted on the drive after they finish. Orbit refuses password-protected archives and anything that isn’t plain files and folders.",
        )}
      </p>
      <button
        type="button"
        role="checkbox"
        aria-checked={enabled}
        className="source-acknowledgement"
        disabled={pending}
        onClick={() => void change()}
      >
        <span className="choice-check" aria-hidden="true">
          {enabled && <Icon name="check" />}
        </span>
        <span>{t("Delete archives after extracting")}</span>
      </button>
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
