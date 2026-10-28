// Store's Files: the drives, then a folder as a list, with an entry's
// details and actions in a dialog like a Library game's. Only files: the
// games are Library's.
import { useState } from "react";
import { Icon, Modal } from "./components";
import { bytes, type FileEntry, type Storage } from "./types";
import { driveRoot } from "./LibraryActions";
import type { FilesModel, FileSort } from "./useFiles";
import { placeName, t, tn } from "./i18n";
import {
  FileClip,
  FileDialogs,
  activityLine,
  fileActions,
  fileBytes,
  fileFacts,
  fileLine,
  fileTypeName,
  lockedText,
  placeLine,
} from "./FileActions";

export const sortOptions = () => [
  { value: "name", label: t("Name") },
  { value: "size", label: t("Size") },
  { value: "date", label: t("Date modified") },
];

export function Files({
  files,
  drives,
  consoleBrowser,
}: {
  files: FilesModel;
  drives: Storage[];
  consoleBrowser: boolean;
}) {
  const drive = drives.find((d) => d.id === files.storageId);
  const [selected, setSelected] = useState<FileEntry | null>(null);
  const [error, setError] = useState("");
  const current = selected && files.entries.find((e) => e === selected);
  // A hidden operation: one line that opens its dialog again.
  const activity = files.activity && !files.activityOpen && (
    <button onClick={files.showActivity}>{activityLine(files)}</button>
  );
  if (!files.storageId)
    return (
      <section className="storage-overview" aria-label={t("Files")}>
        <div className="storage-overview-heading">
          <div>
            <h2>{t("Your drives")}</h2>
            <p>{t("Browse, copy, move and delete files on your PS5’s drives.")}</p>
          </div>
          {activity}
        </div>
        <FileClip files={files} report={setError} />
        <div className="drive-grid" role="group" aria-label={t("Select drive")}>
          {drives.map((d) => (
            <button key={d.id} className="drive-card" onClick={() => files.open(d.id)}>
              <span className="drive-card-title">
                <Icon name={d.external ? "usb" : "storage"} />
                <strong>{placeName(d.label)}</strong>
              </span>
              <span>{driveRoot(d)}</span>
              <span className="storage-meter">
                <span style={{ width: `${d.totalBytes ? Math.min(100, (100 * (d.totalBytes - d.freeBytes)) / d.totalBytes) : 0}%` }} />
              </span>
              <strong>{t("{size} free", { size: bytes(d.freeBytes) })}</strong>
              <span>{t("{used} used of {total}", { used: bytes(d.totalBytes - d.freeBytes), total: bytes(d.totalBytes) })}</span>
            </button>
          ))}
        </div>
        {!drives.length && (
          <div className="empty compact">
            <Icon name="usb" />
            <h3>{t("No drives reported")}</h3>
          </div>
        )}
        {error && (
          <p className="form-error" role="alert">
            {error}
          </p>
        )}
        <FileDialogs files={files} drives={drives} />
      </section>
    );
  return (
    <section className="storage-overview" aria-label={t("Files")}>
      <div className="storage-overview-heading">
        <div>
          <h2>{files.path ? files.path.slice(files.path.lastIndexOf("/") + 1) : placeName(drives.find((d) => d.id === files.storageId)?.label || "")}</h2>
          <p className="library-path">{files.path ? placeLine(drives, files.storageId, files.path) : drive && driveRoot(drive)}</p>
        </div>
        <div className="library-heading-actions">
          {activity}
          <button disabled={files.running} onClick={() => files.setDialog({ kind: "folder" })}>
            {t("New folder")}
          </button>
          <button onClick={() => files.up()}>{files.path ? t("Up one folder") : t("All drives")}</button>
        </div>
      </div>
      <FileClip files={files} report={setError} />
      <div className="library-toolbar">
        <div className="library-tabs" role="group" aria-label={t("Sort by")}>
          {sortOptions().map((o) => (
            <button key={o.value} aria-pressed={files.sort === o.value} onClick={() => files.setSort(o.value as FileSort)}>
              {o.label}
            </button>
          ))}
        </div>
        <div className="library-filters">
          <label className="search-field">
            <Icon name="search" />
            <input aria-label={t("Search this folder")} value={files.query} onChange={(e) => files.setQuery(e.target.value)} placeholder={t("Search this folder")} />
          </label>
        </div>
      </div>
      <p className="fine">
        {tn(files.entries.length, "{count} item", "{count} items")}
        {files.listing?.truncated ? ` · ${t("Showing the first 2,000 items.")}` : ""}
      </p>
      {files.entries.length ? (
        <div className="storage-game-list">
          {files.entries.map((entry) => (
            <div className="file-row" key={entry.name}>
            <button className="storage-game" onClick={() => entry.folder
              ? files.open(files.storageId, files.pathOf(entry)) : setSelected(entry)}>
              <span>
                <strong>{entry.name}</strong>
                <small>{fileLine(files, entry)}</small>
              </span>
              <span>{files.sizeOf(entry) !== null ? fileBytes(files.sizeOf(entry)!) : ""}</span>
              <span aria-hidden="true">›</span>
            </button>
            {entry.folder && <button className="file-row-more" aria-label={`${t("More")} · ${entry.name}`}
              title={t("More")} aria-haspopup="dialog" onClick={() => setSelected(entry)}><Icon name="more" /></button>}
            </div>
          ))}
        </div>
      ) : (
        <div className="empty compact">
          <h3>{files.loading ? t("Reading folder…") : files.query ? t("No matching files") : t("This folder is empty")}</h3>
        </div>
      )}
      {(error || files.error) && (
        <p className="form-error" role="alert">
          {error || files.error}
        </p>
      )}
      {current && (
        <Modal title={t("File details")} close={() => setSelected(null)} className="library-details">
          <p className="library-eyebrow">
            {fileTypeName(current)} · {placeName(drives.find((d) => d.id === files.storageId)?.label || "")}
          </p>
          <h1>{current.name}</h1>
          {current.locked && <p className="notice">{lockedText(current.locked)}</p>}
          <div className="dialog-actions">
            {fileActions(files, current, consoleBrowser, setError).map((a) => (
              <button
                key={a.key}
                className={a.primary ? "primary" : undefined}
                disabled={a.disabled}
                onClick={() => {
                  a.run();
                  if (a.key !== "measure") setSelected(null);
                }}
              >
                {a.label}
              </button>
            ))}
          </div>
          <dl className="library-facts">
            {fileFacts(files, current, drives).map(([label, value]) => (
              <div key={label}>
                <dt>{label}</dt>
                <dd className={label === t("Location") ? "library-path" : undefined}>{value}</dd>
              </div>
            ))}
          </dl>
        </Modal>
      )}
      <FileDialogs files={files} drives={drives} />
    </section>
  );
}
