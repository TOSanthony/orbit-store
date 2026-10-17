import { useEffect, useRef, useState } from "react";
import { Icon } from "./components";
import type { SourceId, SourceSettings } from "./types";
import { t, tn } from "./i18n";

// The service words these in English; the count is part of the sentence.
function nasMessage(error: string): string {
  const skipped = /^(\d+) entr(?:y was|ies were) skipped: invalid, duplicated or not on your NAS address\.$/.exec(error);
  if (skipped)
    return tn(
      Number(skipped[1]),
      "{count} entry was skipped: invalid, duplicated or not on your NAS address.",
      "{count} entries were skipped: invalid, duplicated or not on your NAS address.",
    );
  return t(error);
}

export function Sources({
  settings,
  paired,
  pair,
  save,
  saved,
}: {
  settings: SourceSettings;
  paired: boolean;
  pair: () => void;
  save: (
    enabled: SourceId[],
    noticeVersion: number,
    nasUrl?: string,
  ) => Promise<void>;
  saved?: () => void;
}) {
  const [selected, setSelected] = useState<SourceId[]>(settings.enabled),
    [acknowledged, setAcknowledged] = useState(settings.acknowledged),
    [nasUrl, setNasUrl] = useState(""),
    [pending, setPending] = useState(false),
    [error, setError] = useState("");
  const form = useRef<HTMLFormElement>(null);
  useEffect(() => {
    form.current?.querySelector<HTMLButtonElement>(".source-card")?.focus();
  }, []);
  async function submit() {
    setPending(true);
    setError("");
    try {
      await save(selected, settings.noticeVersion, nasUrl.trim() || undefined);
      saved?.();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending(false);
    }
  }
  return (
    <form
      ref={form}
      className="sources-form"
      onSubmit={(e) => {
        e.preventDefault();
        if (paired) void submit();
        else pair();
      }}
    >
      <h1>
        {settings.acknowledged
          ? t("Download sources")
          : t("Choose your sources")}
      </h1>
      <p className="source-intro">
        {t(
          "Select your download sources. You can change them anytime.",
        )}
      </p>
      <div
        className="source-grid"
        role="group"
        aria-label={t("Download sources")}
      >
        {settings.options.map((source) => {
          const checked = selected.includes(source.id);
          return (
            <button
              key={source.id}
              type="button"
              role="checkbox"
              aria-checked={checked}
              aria-label={source.label}
              className={`source-card ${checked ? "selected" : ""}`}
              disabled={pending}
              onClick={() =>
                setSelected((current) =>
                  current.includes(source.id)
                    ? current.filter((id) => id !== source.id)
                    : [...current, source.id],
                )
              }
            >
              <span className="choice-check" aria-hidden="true">
                {checked && <Icon name="check" />}
              </span>
              {source.label}
            </button>
          );
        })}
      </div>
      {selected.includes("nas") && (
        <div className="nas-settings">
          <label htmlFor="nas-url">{t("My NAS index address")}</label>
          <input
            id="nas-url"
            type="url"
            inputMode="url"
            autoComplete="off"
            spellCheck={false}
            placeholder="https://nas.example.com/orbit-secret/index.json"
            value={nasUrl}
            disabled={pending}
            onChange={(e) => setNasUrl(e.target.value)}
          />
          <p className="fine">
            {settings.nasConfigured
              ? settings.nasError
                ? `${settings.nasHost}: ${nasMessage(settings.nasError)}`
                : t("{host}: {count} games found.")
                    .replace("{host}", settings.nasHost ?? "")
                    .replace("{count}", String(settings.nasCount ?? 0))
              : t("Enter the https address of the index.json on your NAS.")}
            {settings.nasConfigured &&
              ` ${t("Leave the field empty to keep the saved address.")}`}
          </p>
        </div>
      )}
      <div className="source-notice">
        <h2>{t("Before you download")}</h2>
        <p>
          {t(
            "You are responsible for checking that your downloads are lawful. Third-party downloads are at your own risk. Only download content you have permission to access and use, following applicable law and the provider’s terms. Public links do not prove permission, and Orbit does not guarantee the files’ safety or authenticity.",
          )}
        </p>
      </div>
      <button
        type="button"
        role="checkbox"
        aria-checked={acknowledged}
        className="source-acknowledgement"
        disabled={pending}
        onClick={() => setAcknowledged((current) => !current)}
      >
        <span className="choice-check" aria-hidden="true">
          {acknowledged && <Icon name="check" />}
        </span>
        <span>
          {t(
            "I understand the risks and will only download content I’m legally entitled to access and use.",
          )}
        </span>
      </button>
      {settings.acknowledged && (
        <p className="fine">
          {t(
            "Turning off a source hides its download options and pauses its unfinished downloads. Saved files stay on your drive. Re-enable the source and resume from Downloads when you’re ready.",
          )}
        </p>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
      <div className="dialog-actions">
        <button
          className="primary"
          disabled={
            pending ||
            !acknowledged ||
            (!settings.acknowledged && !selected.length)
          }
        >
          {pending
            ? t("Saving…")
            : !paired
              ? t("Pair to save sources")
              : t("Save sources")}
        </button>
      </div>
      <p className="fine">
        {t("Saved for this console and its paired devices.")}
      </p>
    </form>
  );
}
