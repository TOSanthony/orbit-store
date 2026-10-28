// Crossbar's Library: views as folders, the games as a list with the focused
// game's buttons on its row and its details beside it, then Files (the drives
// and their folders: CrossbarFiles.tsx), the scan and game sizes. Actions,
// confirmations and the scan are Library's own.
import { useState, type ReactNode } from "react";
import { bytes, type LibraryGame, type Storage } from "../types";
import type { LibraryModel } from "../useLibrary";
import { placeName, t, tn } from "../i18n";
import { Artwork } from "../Artwork";
import { GameActions } from "../LibraryActions";
import { CrossbarList, type XbRow } from "./CrossbarList";
import { CrossbarFiles } from "./CrossbarFiles";
import type { FilesModel } from "../useFiles";
import type { LibrarySection } from "../Library";
import { Glyph, type GlyphName } from "./Glyph";

const statusOf = (g: LibraryGame) =>
  !g.onDrive ? t("Unavailable") : g.mounted ? t("Mounted") : g.installed ? t("Installed") : t("On drive");
const viewGlyph: Record<string, GlyphName> = { Installed: "check", "On drive": "drive", Unavailable: "warn" };

export function CrossbarLibrary({
  model,
  games,
  rows,
  views,
  view,
  setView,
  section,
  setSection,
  ready,
  busy,
  error,
  openScan,
  dialogs,
  drives,
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
  ready: boolean;
  busy: boolean;
  error: string;
  openScan: () => void;
  dialogs: ReactNode;
  drives: Storage[];
  files: FilesModel;
  consoleBrowser: boolean;
}) {
  const [current, setCurrent] = useState<string | undefined>();
  const folderRow = (key: string, label: string, sub: string, glyph: GlyphName, open: () => void): XbRow => ({
    key,
    activate: open,
    render: () => (
      <>
        <span className="xb-row-icon">
          <Glyph name={glyph} size={22} />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{label}</span>
          <span className="xb-row-sub">{sub}</span>
        </span>
        <Glyph name="next" size={18} />
      </>
    ),
  });
  const leave = () => {
    if (view !== views[0]) {
      setCurrent(`folder-${view}`);
      setView(views[0]);
      return true;
    }
    return false;
  };
  // Files works without ShadowMount, so it comes before Library's own state.
  if (section === "files")
    return (
      <CrossbarFiles
        files={files}
        drives={drives}
        consoleBrowser={consoleBrowser}
        leave={() => {
          setSection("games");
          setCurrent("folder-files");
        }}
      />
    );
  if (!ready) {
    return (
      <section className="xb-page xb-library">
        <div className="xb-split">
          <div className="xb-column">
            <div className="xb-empty">
              <h2>{busy ? t("Finding your games") : t("Connect your library")}</h2>
              <p>
                {busy
                  ? t("Reading installed titles and drive availability from ShadowMount.")
                  : t("Library uses ShadowMount’s local games API on your PS5. Start a version with that API enabled, then select Try again.")}
              </p>
              {error && <p className="warn">{error}</p>}
              {!busy && (
                <button className="xb-pill primary" disabled={Date.now() < model.refreshAt} onClick={() => void model.read(true)}>
                  {t("Try again")}
                </button>
              )}
            </div>
          </div>
        </div>
        {dialogs}
      </section>
    );
  }
  const focusedGame = rows.find((g) => `game-${g.titleId}` === current) || rows[0];
  const gameRows: XbRow[] = rows.map((g) => ({
    key: `game-${g.titleId}`,
    disabled: !g.onDrive,
    render: () => (
      <>
        <span className="xb-row-art">
          {g.cover ? (
            <Artwork src={g.cover} fallbackSrc={g.coverFallback || undefined} alt="" decoding="async" referrerPolicy="no-referrer" />
          ) : (
            <Glyph name="box" size={22} />
          )}
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{g.title}</span>
          <span className={`xb-row-sub${!g.onDrive ? " warn" : ""}`}>
            {statusOf(g)} · {g.onDrive ? placeName(g.location) : t("Drive disconnected")}
          </span>
        </span>
      </>
    ),
    actions: <GameActions game={g} model={model} driveCards />,
  }));
  const allRows: XbRow[] = [
    ...(view === views[0]
      ? views.slice(1).map((v) =>
          folderRow(`folder-${v}`, t(v), tn(games.filter((g) => (v === "Installed" ? g.installed : v === "On drive" ? g.onDrive : !g.onDrive)).length, "{count} game", "{count} games"), viewGlyph[v] || "folder", () => {
            setView(v);
            setCurrent(undefined);
          }),
        )
      : []),
    ...gameRows,
    ...(view === views[0]
      ? [
          folderRow("folder-files", t("Files"), tn(drives.length, "{count} drive", "{count} drives"), "folder", () => {
            setSection("files");
            setCurrent(undefined);
          }),
          ...(model.can("rescan")
            ? [
                folderRow(
                  "scan",
                  t("Scan for games"),
                  model.snapshot?.scanning ? t("Looking for new games…") : t("Search your drives with ShadowMount"),
                  "scan",
                  openScan,
                ),
              ]
            : []),
        ]
      : []),
  ];
  return (
    <section className="xb-page xb-library">
      <div className="xb-split">
        <div className="xb-column">
          {view !== views[0] && (
            <p className="xb-breadcrumb">
              {t("Library")} <span aria-hidden="true">›</span> {t(view)}
            </p>
          )}
          <CrossbarList rows={allRows} current={current || (focusedGame ? `game-${focusedGame.titleId}` : undefined)} setCurrent={setCurrent} back={leave} ariaLabel={t("Library")} />
        </div>
        {focusedGame && (
          <aside className="xb-aside xb-card" aria-live="polite">
            <p className="xb-card-kicker">
              {focusedGame.platform === "unknown" ? t("Platform unknown") : focusedGame.platform.toUpperCase()} · {focusedGame.titleId}
              {model.stale && <span className="xb-tag">{t("Saved snapshot")}</span>}
            </p>
            <h2>{focusedGame.title}</h2>
            <dl className="xb-facts">
              <div>
                <dt>{t("Installation")}</dt>
                <dd>{focusedGame.installed ? t("Registered on the console") : t("Not installed on the console")}</dd>
              </div>
              <div>
                <dt>{t("Mount status")}</dt>
                <dd>{focusedGame.mounted ? t("Mounted") : t("Not mounted")}</dd>
              </div>
              <div>
                <dt>{t("Source")}</dt>
                <dd>{focusedGame.onDrive ? t("Available on drive") : t("Unavailable; the drive or source may be missing")}</dd>
              </div>
              <div>
                <dt>{t("Format")}</dt>
                <dd>{focusedGame.format}</dd>
              </div>
              <div>
                <dt>{t("Source path")}</dt>
                <dd className="library-path">{focusedGame.path || t("Not reported")}</dd>
              </div>
              {focusedGame.runtimePath && focusedGame.runtimePath !== focusedGame.path && (
                <div>
                  <dt>{t("Game path")}</dt>
                  <dd className="library-path">{focusedGame.runtimePath}</dd>
                </div>
              )}
              <div>
                <dt>{t("Source size")}</dt>
                <dd>
                  {focusedGame.sizeBytes !== null
                    ? bytes(focusedGame.sizeBytes)
                    : focusedGame.sizeStatus === "unavailable"
                      ? t("Unavailable")
                      : model.snapshot?.measuring
                        ? t("Measuring…")
                        : t("Not measured")}
                </dd>
              </div>
              <div>
                <dt>{t("Size reported by console")}</dt>
                <dd>{focusedGame.installedSizeBytes === null ? t("Not reported") : bytes(focusedGame.installedSizeBytes)}</dd>
              </div>
            </dl>
            {focusedGame.foundByOrbit && (
              <p className="xb-card-fine">
                {t(
                  "Orbit found this game itself because your ShadowMount version doesn’t list games installed from PKG files. For the most complete list, update ShadowMount to 1.7beta5 or newer.",
                )}
              </p>
            )}
            <p className="xb-card-fine">
              {t(
                "Installed means registered on the console. Mounted means its source is currently mounted. On drive means the source is available. These statuses do not confirm that a game will launch.",
              )}
            </p>
            {error && <p className="xb-card-error">{error}</p>}
          </aside>
        )}
      </div>
      {dialogs}
    </section>
  );
}
