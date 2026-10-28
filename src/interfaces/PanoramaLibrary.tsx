// Panorama's Library: the views as tabs over one sideways page — the games in
// the chosen view, then the chosen game's own group (its actions as tiles and
// its details). The Files tab holds the drives, their games and folders
// (PanoramaFiles.tsx). Actions, confirmations and the scan are Library's own.
import { useRef, type ReactNode } from "react";
import { bytes, type LibraryGame, type Storage } from "../types";
import type { LibraryModel } from "../useLibrary";
import { formatDate, formatNumber, placeName, t } from "../i18n";
import { Select } from "../Select";
import { GameActions, LibraryFeedback, StorageProgress } from "../LibraryActions";
import { Glyph } from "./Glyph";
import { PnGroup, PnTile } from "./PanoramaParts";
import { PanoramaFiles } from "./PanoramaFiles";
import type { FilesModel } from "../useFiles";
import type { LibrarySection } from "../Library";

const statusOf = (g: LibraryGame) =>
  !g.onDrive ? t("Unavailable") : g.mounted ? t("Mounted") : g.installed ? t("Installed") : t("On drive");

export function PanoramaLibrary({
  model,
  games,
  rows,
  views,
  view,
  setView,
  section,
  setSection,
  query,
  setQuery,
  location,
  setLocation,
  selected,
  setSelected,
  ready,
  busy,
  stale,
  error,
  lookup,
  dismissLookup,
  openScan,
  dialogs,
  drives: orbitDrives,
  files,
  consoleBrowser,
}: {
  model: LibraryModel;
  games: LibraryGame[];
  rows: LibraryGame[];
  views: readonly string[];
  view: string;
  setView: (view: string) => void;
  section: LibrarySection;
  setSection: (section: LibrarySection) => void;
  query: string;
  setQuery: (value: string) => void;
  location: string;
  setLocation: (value: string) => void;
  selected: string | null;
  setSelected: (titleId: string | null) => void;
  ready: boolean;
  busy: boolean;
  stale: boolean;
  error: string;
  lookup: { title: string } | null;
  dismissLookup: () => void;
  openScan: () => void;
  dialogs: ReactNode;
  drives: Storage[];
  files: FilesModel;
  consoleBrowser: boolean;
}) {
  const search = useRef<HTMLInputElement>(null);
  // The views, then Files: Files works without ShadowMount.
  const tabs = (
    <div className="collection-tabs" role="group" aria-label={t("Library views")}>
      {views.map((v) => (
        <button
          key={v}
          aria-pressed={section !== "files" && view === v}
          onClick={() => {
            setSection("games");
            setView(v);
          }}
        >
          {t(v)}
        </button>
      ))}
      <button aria-pressed={section === "files"} onClick={() => setSection("files")}>
        {t("Files")}
      </button>
    </div>
  );
  if (section === "files")
    return (
      <PanoramaFiles dialogs={dialogs} files={files} drives={orbitDrives} consoleBrowser={consoleBrowser} tabs={tabs} />
    );
  const lookupNotice = lookup && (
    <div className="pn-notice" role="status">
      <strong>{t("Looking for {title}", { title: lookup.title })}</strong>
      <p>
        {t(
          "The completed file is not in the current Library inventory. Check that its drive is connected, then scan for games. A completed download alone does not confirm installation.",
        )}
      </p>
      <div className="pn-notice-actions">
        {model.can("rescan") && (
          <button className="primary" onClick={openScan}>
            {t("Scan for games")}
          </button>
        )}
        <button onClick={dismissLookup}>{t("Dismiss")}</button>
      </div>
    </div>
  );
  if (!ready) {
    return (
      <section className="pn-library">
        {lookupNotice}
        <div className="pn-hero-empty">
          <span className="pn-kicker">{busy ? t("Reading your library") : t("ShadowMount isn’t answering")}</span>
          <h2>{busy ? t("Finding your games") : t("Connect your library")}</h2>
          <p>
            {busy
              ? t("Reading installed titles and drive availability from ShadowMount.")
              : t("Library uses ShadowMount’s local games API on your PS5. Start a version with that API enabled, then select Try again.")}
          </p>
          {!busy && <p>{t("Games are not inferred from download history. An unavailable library does not mean your games are missing.")}</p>}
          {error && <p className="warn">{error}</p>}
          {!busy && (
            <button className="primary" disabled={Date.now() < model.refreshAt} onClick={() => void model.read(true)}>
              {t("Try again")}
            </button>
          )}
        </div>
        {dialogs}
      </section>
    );
  }
  const current = rows.find((g) => g.titleId === selected) || rows[0];
  const updated = model.snapshot?.scanning
    ? t("Looking for new games…")
    : stale && model.snapshot?.updatedAt
      ? t("Saved snapshot · Last refreshed {time}", {
          time: formatDate(new Date(model.snapshot.updatedAt * 1000), { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" }),
        })
      : "";
  return (
    <section className="pn-library">
      <div className="pn-browse-bar pn-library-bar">
        {tabs}
        <div className="page-heading">
          <label className="search-field">
            <Glyph name="search" size={18} />
            <input
              ref={search}
              aria-label={t("Search library")}
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder={t("Search your library")}
            />
          </label>
        </div>
        <div className="browse-selects">
          <Select
            inlineLabel
            label={t("Location")}
            value={location}
            onChange={setLocation}
            options={[
              { value: "", label: t("All locations") },
              ...[...new Set(games.map((g) => g.location))].sort().map((value) => ({ value, label: placeName(value) })),
            ]}
          />
        </div>
        {updated && <span className="pn-snapshot">{updated}</span>}
      </div>
      {lookupNotice}
      <div className="pn-library-status">
        <StorageProgress model={model} />
        <LibraryFeedback model={model} />
        {error && <p className="warn">{error}</p>}
      </div>
      <div className="pn-page" data-scroll="x">
        <PnGroup title={t(view)} count={formatNumber(rows.length)} className="pn-feature">
          {rows.length ? (
            rows.map((g, i) => (
              <PnTile
                key={g.titleId}
                size={i === 0 ? "large" : "small"}
                art={g.cover || undefined}
                fallback={g.coverFallback || undefined}
                glyph={g.cover ? undefined : "box"}
                title={g.title}
                sub={`${statusOf(g)} · ${g.onDrive ? placeName(g.location) : t("Drive disconnected")}`}
                label={t("View library details for {title}", { title: g.title })}
                className="pn-game-tile"
                current={current?.titleId === g.titleId}
                onClick={() => setSelected(g.titleId)}
                onFocus={() => setSelected(g.titleId)}
              />
            ))
          ) : (
            <div className="pn-empty-card wide">
              <strong>{t("No games found")}</strong>
              <small>{t("Try another title, title ID or filter.")}</small>
            </div>
          )}
          <PnTile tone="dark" glyph="search" title={t("Search your library")} onClick={() => search.current?.focus()} />
        </PnGroup>
        {current && (
          <PnGroup title={current.title} className="pn-game-group">
            <GameActions
              game={current}
              model={model}
              driveCards
              tiles={{ status: `${statusOf(current)} · ${current.onDrive ? placeName(current.location) : t("Drive disconnected")}` }}
            />
            <div className="pn-tile large dark pn-details">
              <span className="pn-kicker">{t("Library game details")}</span>
              <dl>
                <div>
                  <dt>{current.platform === "unknown" ? t("Platform unknown") : current.platform.toUpperCase()}</dt>
                  <dd>{current.titleId}</dd>
                </div>
                <div>
                  <dt>{t("Installation")}</dt>
                  <dd>{current.installed ? t("Registered on the console") : t("Not installed on the console")}</dd>
                </div>
                <div>
                  <dt>{t("Mount status")}</dt>
                  <dd>{current.mounted ? t("Mounted") : t("Not mounted")}</dd>
                </div>
                <div>
                  <dt>{t("Source")}</dt>
                  <dd>{current.onDrive ? t("Available on drive") : t("Unavailable; the drive or source may be missing")}</dd>
                </div>
                <div>
                  <dt>{t("Format")}</dt>
                  <dd>{current.format}</dd>
                </div>
                <div>
                  <dt>{t("Source path")}</dt>
                  <dd className="library-path">{current.path || t("Not reported")}</dd>
                </div>
                {current.runtimePath && current.runtimePath !== current.path && (
                  <div>
                    <dt>{t("Game path")}</dt>
                    <dd className="library-path">{current.runtimePath}</dd>
                  </div>
                )}
                <div>
                  <dt>{t("Source size")}</dt>
                  <dd>
                    {current.sizeBytes !== null
                      ? bytes(current.sizeBytes)
                      : current.sizeStatus === "unavailable"
                        ? t("Unavailable")
                        : model.snapshot?.measuring
                          ? t("Measuring…")
                          : t("Not measured")}
                  </dd>
                </div>
                <div>
                  <dt>{t("Size reported by console")}</dt>
                  <dd>{current.installedSizeBytes === null ? t("Not reported") : bytes(current.installedSizeBytes)}</dd>
                </div>
              </dl>
              {current.foundByOrbit && (
                <p className="pn-details-note">
                  {t(
                    "Orbit found this game itself because your ShadowMount version doesn’t list games installed from PKG files. For the most complete list, update ShadowMount to 1.7beta5 or newer.",
                  )}
                </p>
              )}
              {/* What the statuses mean, at the end of the details (the group keeps three rows). */}
              <p className="pn-details-note">
                {t(
                  "Installed means registered on the console. Mounted means its source is currently mounted. On drive means the source is available. These statuses do not confirm that a game will launch.",
                )}
              </p>
            </div>
          </PnGroup>
        )}
        {/* The games' own tool: the scan, for games Orbit didn't add (Files is its own tab). */}
        {model.can("rescan") && (
          <PnGroup title={t("Storage")}>
            <PnTile tone="dark" glyph="scan" title={t("Scan for games")} disabled={model.working} onClick={openScan} />
          </PnGroup>
        )}
      </div>
      {dialogs}
    </section>
  );
}
