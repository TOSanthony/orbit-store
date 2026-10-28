// Panorama's Files: one sideways page like Library's — the drives, or the
// folder's entries as tiles, then the chosen entry's own group (its actions as
// tiles and its details), then the folder's tools. Only files: the games are Library's.
import { useState, type ReactNode } from "react";
import { bytes, type Storage } from "../types";
import { driveRoot } from "../LibraryActions";
import type { FilesModel, FileSort } from "../useFiles";
import { formatNumber, placeName, t } from "../i18n";
import { Select } from "../Select";
import { sortOptions } from "../Files";
import {
  FileClip,
  FileDialogs,
  activityLine,
  operationPercent,
  canPaste,
  fileActions,
  fileFacts,
  fileGlyph,
  fileShortLine,
  fileTypeName,
  lockedText,
  paste,
  placeLine,
} from "../FileActions";
import { Glyph } from "./Glyph";
import { PnGroup, PnTile } from "./PanoramaParts";

export function PanoramaFiles({
  dialogs,
  files,
  drives,
  consoleBrowser,
  tabs,
}: {
  dialogs: ReactNode;
  files: FilesModel;
  drives: Storage[];
  consoleBrowser: boolean;
  tabs: ReactNode;
}) {
  const [pick, setPick] = useState("");
  const [error, setError] = useState("");
  const entry = files.entries.find((e) => e.name === pick);
  const here = files.storageId ? placeLine(drives, files.storageId, files.path) : t("Files");
  // A hidden operation: a tile that opens its dialog again.
  const activity = files.activity && !files.activityOpen && (
    <PnTile
      size="wide"
      tone="accent"
      glyph="clock"
      title={activityLine(files)}
      progress={operationPercent(files.activity)}
      onClick={files.showActivity}
    />
  );
  return (
    <section className="pn-library">
      <div className="pn-browse-bar pn-library-bar">
        {tabs}
        {files.storageId && (
          <>
            <div className="page-heading">
              <label className="search-field">
                <Glyph name="search" size={18} />
                <input aria-label={t("Search this folder")} value={files.query} onChange={(e) => files.setQuery(e.target.value)} placeholder={t("Search this folder")} />
              </label>
            </div>
            <div className="browse-selects">
              <Select inlineLabel label={t("Sort by")} value={files.sort} onChange={(v) => files.setSort(v as FileSort)} options={sortOptions()} />
            </div>
          </>
        )}
      </div>
      <div className="pn-library-status">
        <FileClip files={files} report={setError} className="pn-notice" />
        {(error || files.error) && <p className="warn">{error || files.error}</p>}
      </div>
      <div className="pn-page" data-scroll="x">
        {!files.storageId ? (
          <>
            {/* The drives: select one to open it. */}
            <PnGroup title={t("Files")} className="pn-feature">
              {drives.map((d) => (
                <PnTile
                  key={d.id}
                  size="wide"
                  tone="dark"
                  glyph="drive"
                  title={placeName(d.label)}
                  sub={`${t("{free} free of {total}", { free: bytes(d.freeBytes), total: bytes(d.totalBytes) })} · ${driveRoot(d)}`}
                  progress={d.totalBytes ? (100 * (d.totalBytes - d.freeBytes)) / d.totalBytes : 0}
                  onClick={() => files.open(d.id)}
                />
              ))}
              {!drives.length && (
                <div className="pn-empty-card wide">
                  <strong>{t("No drives reported")}</strong>
                </div>
              )}
              {activity}
            </PnGroup>
          </>
        ) : (
          <>
            <PnGroup
              title={files.path ? files.path.slice(files.path.lastIndexOf("/") + 1) : placeName(drives.find((d) => d.id === files.storageId)?.label || "")}
              count={formatNumber(files.entries.length)}
              className="pn-feature"
              foot={<p className="pn-group-fine library-path">{here}</p>}
            >
              <PnTile size="wide" tone="dark" glyph="back" title={files.path ? t("Up one folder") : t("All drives")} onClick={() => files.up()} />
              {files.entries.length ? (
                files.entries.map((e) => (
                  <PnTile
                    key={e.name}
                    size="wide"
                    tone="dark"
                    glyph={fileGlyph[e.type]}
                    title={e.name}
                    sub={fileShortLine(files, e)}
                    current={entry?.name === e.name}
                    onClick={() => (e.folder ? files.open(files.storageId, files.pathOf(e)) : setPick(e.name))}
                    onFocus={() => setPick(e.name)}
                  />
                ))
              ) : (
                <div className="pn-empty-card wide">
                  <strong>{files.loading ? t("Reading folder…") : t("This folder is empty")}</strong>
                </div>
              )}
            </PnGroup>
            {entry && (
              <PnGroup title={entry.name}>
                {/* Its details first, then its actions in Panorama's three rows. */}
                <div className="pn-tile large dark pn-details">
                  <span className="pn-kicker">{fileTypeName(entry)}</span>
                  <dl>
                    {fileFacts(files, entry, drives).map(([label, value]) => (
                      <div key={label}>
                        <dt>{label}</dt>
                        <dd className={label === t("Location") ? "library-path" : undefined}>{value}</dd>
                      </div>
                    ))}
                  </dl>
                  {entry.locked && <p className="pn-details-note">{lockedText(entry.locked)}</p>}
                </div>
                {fileActions(files, entry, consoleBrowser, setError).map((a) => (
                  <PnTile key={a.key} tone={a.primary ? "accent" : "dark"} glyph={a.glyph || "more"} title={a.label} disabled={a.disabled} onClick={a.run} />
                ))}
              </PnGroup>
            )}
            <PnGroup title={t("This folder")}>
              {activity}
              {files.clip && (
                <PnTile tone="accent" glyph={files.clip.action === "copy" ? "copy" : "move"} title={t("Paste here")} disabled={!canPaste(files)} onClick={() => paste(files, setError)} />
              )}
              <PnTile tone="dark" glyph="folder" title={t("New folder")} disabled={files.running} onClick={() => files.setDialog({ kind: "folder" })} />
              <PnTile tone="dark" glyph="refresh" title={t("Refresh")} onClick={() => files.refresh()} />
            </PnGroup>
          </>
        )}
      </div>
      <FileDialogs files={files} drives={drives} />
      {dialogs}
    </section>
  );
}
