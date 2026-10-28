import { useState } from "react";
import { addonTypes } from "./AddonInfo";
import { Modal } from "./components";
import { t } from "./i18n";
import { TAGS, narrowed, type BrowseFilters, type SavedFilter } from "./browse";

const SIZES: Record<string, string> = {
  small: /* i18n */ "Under 10 GB",
  medium: /* i18n */ "10–50 GB",
  large: /* i18n */ "50–100 GB",
  huge: /* i18n */ "100 GB or more",
};
const SORTS: Record<string, string> = {
  release: /* i18n */ "Newest first",
  added: /* i18n */ "Recently added",
  title: /* i18n */ "Title A–Z",
  "title-desc": /* i18n */ "Title Z–A",
  size: /* i18n */ "Smallest download",
  "size-desc": /* i18n */ "Largest download",
};
// One line naming what a filter keeps: the search, then each filter, then the sort.
export function filterSummary(f: BrowseFilters, defaultSort: string, sources: Map<string, string>) {
  const parts = [
    f.query.trim() && `“${f.query.trim()}”`,
    f.collection === "favorites" && t("Favourites"),
    f.platform.toUpperCase(),
    ...f.source.map((id) => sources.get(id) || id),
    ...f.format,
    ...f.tags.map((tag) => (TAGS[tag] ? t(TAGS[tag]) : tag)),
    f.addonType && t(addonTypes[f.addonType as keyof typeof addonTypes] || f.addonType),
    f.hideInstalled && t("Hide installed"),
    f.region && (f.region === "unknown" ? t("Unknown region") : f.region),
    SIZES[f.size] && t(SIZES[f.size]),
    f.sort !== defaultSort && SORTS[f.sort] && t(SORTS[f.sort]),
  ].filter(Boolean);
  return parts.length ? parts.join(" · ") : t("No filters");
}

// Browse's saved filters: apply or delete one, or save what is set now under a
// name. They are kept on the PS5, so every Orbit app shows the same ones.
export function SavedFilters({
  page,
  filters,
  defaultSort,
  saved,
  sources,
  change,
  apply,
  close,
}: {
  page: SavedFilter["page"];
  filters: BrowseFilters;
  defaultSort: string;
  saved: SavedFilter[];
  sources: Map<string, string>;
  change: (body: Record<string, unknown>) => Promise<unknown>;
  apply: (entry: SavedFilter) => void;
  close: () => void;
}) {
  const [name, setName] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const mine = saved.filter((s) => s.page === page).sort((a, b) => a.name.localeCompare(b.name));
  const trimmed = name.trim();
  const existing = mine.find((s) => s.name.toLowerCase() === trimmed.toLowerCase());
  const anything = narrowed(filters, defaultSort);
  const run = async (body: Record<string, unknown>) => {
    setBusy(true);
    setError("");
    try {
      await change(body);
      return true;
    } catch (e) {
      setError((e as Error).message);
      return false;
    } finally {
      setBusy(false);
    }
  };
  return (
    <Modal title={t("Saved filters")} close={close} className="saved-filters">
      <h1>{t("Saved filters")}</h1>
      <p>{t("Save your filters, sort and search under a name, then apply them later in any Orbit app.")}</p>
      {mine.length > 0 && (
        <div className="storage-game-list">
          {mine.map((entry, i) => (
            <div className="saved-filter" key={entry.id}>
              <button
                className="storage-game"
                data-initial-focus={i === 0 ? "" : undefined}
                disabled={busy}
                onClick={() => apply(entry)}
              >
                <span>
                  <strong>{entry.name}</strong>
                  <small>{filterSummary(entry.filters, defaultSort, sources)}</small>
                </span>
                <span>{t("Apply")}</span>
              </button>
              <button disabled={busy} onClick={() => void run({ action: "delete", id: entry.id })}>
                {t("Delete")}
              </button>
            </div>
          ))}
        </div>
      )}
      <h3 className="saved-filters-heading">{t("Save these filters")}</h3>
      <p className="fine">
        {anything ? filterSummary(filters, defaultSort, sources) : t("Choose some filters or a search first.")}
      </p>
      <label className="field">
        {t("Name")}
        <input
          data-initial-focus={mine.length ? undefined : ""}
          type="text"
          value={name}
          maxLength={40}
          autoComplete="off"
          spellCheck={false}
          onChange={(e) => setName(e.target.value)}
        />
      </label>
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
      <div className="dialog-actions">
        <button
          className="primary"
          disabled={busy || !trimmed || !anything}
          onClick={() =>
            void run({ action: "save", name: trimmed, page, filters }).then((ok) => ok && setName(""))
          }
        >
          {existing ? t("Replace {name}", { name: existing.name }) : t("Save filters")}
        </button>
        <button disabled={busy} onClick={close}>
          {t("Back")}
        </button>
      </div>
    </Modal>
  );
}
