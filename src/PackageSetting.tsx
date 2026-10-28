import { useState } from "react";
import { api } from "./api";
import { Icon } from "./components";
import { t } from "./i18n";

// Whether a downloaded PKG or FPKG is deleted once the PS5 reports it installed.
export function PackageSetting({
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
      await api("/settings/installation", { deletePackages: !enabled });
      await refresh();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending(false);
    }
  };
  return (
    <section className="archive-setting">
      <h2 className="download-storage-heading">{t("Installed packages")}</h2>
      <p>{t("Orbit can delete a PKG or FPKG once it has installed on your PS5, to free up space. If installation fails, the package is kept so you can try again.")}</p>
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
        <span>{t("Delete packages after installing")}</span>
      </button>
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
