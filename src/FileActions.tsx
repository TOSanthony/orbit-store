// What every look shares in Files: names for kinds of file, the actions an
// entry offers, the running operation, the clipboard and the dialogs. Each
// look lays these out in its own way (Files.tsx, interfaces/*Files.tsx).
import { useState } from "react";
import { Modal } from "./components";
import type { GlyphName } from "./interfaces/Glyph";
import { bytes, type FileEntry, type FileOperation, type Storage } from "./types";
import type { FilesModel } from "./useFiles";
import { formatDate, formatNumber, placeName, t } from "./i18n";

export const fileTypeName = (entry: FileEntry) =>
  t(
    {
      folder: /* i18n */ "Folder",
      game: /* i18n */ "Game folder",
      archive: /* i18n */ "Archive",
      package: /* i18n */ "Package",
      image: /* i18n */ "Game image",
      file: /* i18n */ "File",
    }[entry.type],
  );
export const fileGlyph: Record<FileEntry["type"], GlyphName> = {
  folder: "folder",
  game: "ps",
  archive: "archive",
  package: "box",
  image: "layers",
  file: "list",
};
// Small files in kilobytes; Orbit's own sizes start at megabytes.
export const fileBytes = (n: number) =>
  n < 1e6 ? formatNumber(n / 1e3, { style: "unit", unit: "kilobyte", maximumFractionDigits: n < 1e4 ? 1 : 0 }) : bytes(n);
export const fileDate = (seconds: number) =>
  seconds ? formatDate(new Date(seconds * 1000), { year: "numeric", month: "short", day: "numeric" }) : "";
// "Archive · 12.4 GB · 20 Oct 2026", without what isn't known.
export function fileLine(files: FilesModel, entry: FileEntry) {
  const size = files.sizeOf(entry);
  return [fileTypeName(entry), size !== null ? fileBytes(size) : "", fileDate(entry.modifiedAt)].filter(Boolean).join(" · ");
}
// The same without the date, for tiles.
export function fileShortLine(files: FilesModel, entry: FileEntry) {
  const size = files.sizeOf(entry);
  return [entry.locked ? t("In use") : "", fileTypeName(entry), size !== null ? fileBytes(size) : ""].filter(Boolean).join(" · ");
}
export const lockedText = (locked: string) =>
  t(
    {
      drive: /* i18n */ "This is the top of the drive.",
      orbit: /* i18n */ "Orbit keeps its own settings and downloads list here.",
      download: /* i18n */ "A download or transfer is using this.",
      game: /* i18n */ "ShadowMount is using this game. Unmount or uninstall it first.",
      install: /* i18n */ "The PS5 is installing from this.",
      busy: /* i18n */ "Another file operation is using this.",
    }[locked] || locked,
  );
export const driveName = (drives: Storage[], id: string) => placeName(drives.find((d) => d.id === id)?.label || id);
// The drive and folder as one line: "USB storage 1 / homebrew / Games".
export const placeLine = (drives: Storage[], storageId: string, path: string) =>
  [driveName(drives, storageId), ...(path ? path.split("/") : [])].join(" / ");

export interface FileAction {
  key: string;
  label: string;
  glyph?: GlyphName;
  primary?: boolean;
  disabled?: boolean;
  run: () => void;
}
// An entry's actions, the way in first: Open, Unpack here or Install on PS5.
export function fileActions(files: FilesModel, entry: FileEntry, consoleBrowser: boolean, report: (message: string) => void): FileAction[] {
  const busy = files.running || files.submitting;
  const writable = !entry.locked;
  // Reading is fine while others use it, not while something is still writing it.
  const written = ["download", "busy"].includes(entry.locked);
  const go = (action: string, fields: Record<string, unknown> = {}) =>
    void files.run(action, { path: files.pathOf(entry), ...fields }).catch((e) => report((e as Error).message));
  const list: FileAction[] = [];
  if (entry.folder) list.push({ key: "open", label: t("Open"), glyph: "next", primary: true, run: () => files.open(files.storageId, files.pathOf(entry)) });
  if (entry.type === "archive")
    list.push({ key: "unpack", label: t("Unpack here"), glyph: "archive", primary: true, disabled: busy || written, run: () => go("unpack") });
  if (["package", "image", "game"].includes(entry.type))
    list.push({ key: "install", label: t("Install on PS5"), glyph: "down", primary: !entry.folder, disabled: busy || written || entry.locked === "install", run: () => go("install") });
  if (!entry.folder && !consoleBrowser)
    list.push({ key: "save", label: t("Save to this device"), glyph: "down", run: () => void files.save(entry).catch((e) => report((e as Error).message)) });
  if (entry.folder && files.sizeOf(entry) === null)
    list.push({ key: "measure", label: t("Measure size"), glyph: "layers", disabled: busy, run: () => go("measure") });
  list.push(
    {
      key: "copy",
      label: t("Copy"),
      glyph: "copy",
      disabled: busy || written,
      run: () => files.setClip({ action: "copy", storageId: files.storageId, path: files.pathOf(entry), name: entry.name }),
    },
    {
      key: "move",
      label: t("Move"),
      glyph: "move",
      disabled: busy || !writable,
      run: () => files.setClip({ action: "move", storageId: files.storageId, path: files.pathOf(entry), name: entry.name }),
    },
    { key: "rename", label: t("Rename"), glyph: "pencil", disabled: busy || !writable, run: () => files.setDialog({ kind: "rename", entry }) },
    { key: "delete", label: t("Delete"), glyph: "trash", disabled: busy || !writable, run: () => files.setDialog({ kind: "delete", entry }) },
  );
  return list;
}
// Label and value pairs; each look draws them in its own list.
export function fileFacts(files: FilesModel, entry: FileEntry, drives: Storage[]): [string, string][] {
  const size = files.sizeOf(entry);
  return [
    [t("Type"), fileTypeName(entry)],
    [t("Size"), size !== null ? fileBytes(size) : entry.folder ? t("Not measured") : t("Unavailable")],
    [t("Modified"), fileDate(entry.modifiedAt) || t("Not reported")],
    [t("Location"), placeLine(drives, files.storageId, files.pathOf(entry))],
  ];
}

// Whole sentences per operation, so each language can phrase them its own way.
const outcomes = {
  copy: { running: /* i18n */ "Copying {title}", completed: /* i18n */ "Copied {title}", cancelled: /* i18n */ "Copy cancelled: {title}", failed: /* i18n */ "Copy failed: {title}" },
  move: { running: /* i18n */ "Moving {title}", completed: /* i18n */ "Moved {title}", cancelled: /* i18n */ "Move cancelled: {title}", failed: /* i18n */ "Move failed: {title}" },
  delete: { running: /* i18n */ "Deleting {title}", completed: /* i18n */ "Deleted {title}", cancelled: /* i18n */ "Delete cancelled: {title}", failed: /* i18n */ "Delete failed: {title}" },
  unpack: { running: /* i18n */ "Unpacking {title}", completed: /* i18n */ "Unpacked {title}", cancelled: /* i18n */ "Unpack cancelled: {title}", failed: /* i18n */ "Unpack failed: {title}" },
  install: { running: /* i18n */ "Installing {title}", completed: /* i18n */ "Installed {title}", cancelled: /* i18n */ "Install cancelled: {title}", failed: /* i18n */ "Install failed: {title}" },
  measure: { running: /* i18n */ "Measuring {title}", completed: /* i18n */ "Measured {title}", cancelled: /* i18n */ "Measuring cancelled: {title}", failed: /* i18n */ "Measuring failed: {title}" },
};
export function operationTitle(o: FileOperation) {
  const words = outcomes[o.action];
  return t(o.state === "complete" ? words.completed : o.state === "cancelled" ? words.cancelled : o.state === "failed" ? words.failed : words.running, { title: o.name });
}
const percent = (value: number) => formatNumber(value / 100, { style: "percent", minimumFractionDigits: 1, maximumFractionDigits: 1 });
export const operationPercent = (o: FileOperation) => (o.totalBytes ? Math.min(100, (100 * o.processedBytes) / o.totalBytes) : 0);
export function operationLine(o: FileOperation) {
  if (o.state === "measuring") return t("Measuring…");
  if (o.state === "password") return t("This archive needs a password.");
  if (o.state === "paused") return `${t("Paused")} · ${bytes(o.processedBytes)} / ${bytes(o.totalBytes)}`;
  if (o.state === "complete") return o.action === "measure" && o.sizeBytes !== null ? bytes(o.sizeBytes) : `${bytes(o.processedBytes)} · ${t("Complete")}`;
  if (o.state === "cancelled") return t("Operation stopped");
  if (o.state === "failed") return t("Operation could not finish");
  return `${bytes(o.processedBytes)} / ${bytes(o.totalBytes)}`;
}

// The status line each look shows for the operation while its dialog is hidden.
export function activityLine(files: FilesModel) {
  const o = files.activity;
  if (!o) return "";
  if (["complete", "failed", "cancelled"].includes(o.state)) return operationTitle(o);
  return `${operationTitle(o)} · ${o.state === "password" ? t("Needs a password") : o.state === "paused" ? t("Paused") : percent(operationPercent(o))}`;
}
// The running or last operation in a dialog: its progress, Pause, Cancel and Hide.
function FileActivity({ files, drives, report }: { files: FilesModel; drives: Storage[]; report: (message: string) => void }) {
  const o = files.activity;
  if (!o || !files.activityOpen) return null;
  const terminal = ["complete", "failed", "cancelled"].includes(o.state);
  const act = (action: string) => void files.run(action, { id: o.id }).catch((e) => report((e as Error).message));
  return (
    <Modal title={t("File activity")} close={files.hideActivity} className="file-activity">
      <p className="library-eyebrow">{t("FILE ACTIVITY")}</p>
      <h1>{operationTitle(o)}</h1>
      <p className="library-path">
        {placeLine(drives, o.storageId, o.path)}
        {o.toStorageId && (
          <>
            <br />
            {t("To {path}", { path: placeLine(drives, o.toStorageId, o.toPath) })}
          </>
        )}
      </p>
      <p>{operationLine(o)}</p>
      {!terminal && o.state !== "password" && (
        <>
          <progress max={100} value={operationPercent(o)} aria-label={t("File operation progress")} />
          <div className="transfer-meta">
            <span>{percent(operationPercent(o))}</span>
            <span>{o.state === "running" && o.speed > 0 ? `${bytes(o.speed)}/s` : ""}</span>
          </div>
        </>
      )}
      {o.error && (
        <p className="form-error" role="alert">
          {t(o.error)}
        </p>
      )}
      <div className="dialog-actions">
        {o.state === "password" && (
          <button className="primary" onClick={() => files.setDialog({ kind: "password" })}>
            {t("Enter password")}
          </button>
        )}
        {!terminal && ["copy", "move"].includes(o.action) && o.state !== "password" && (
          <button disabled={files.submitting} onClick={() => act(o.state === "paused" ? "resume" : "pause")}>
            {o.state === "paused" ? t("Resume") : t("Pause")}
          </button>
        )}
        {/* The PS5's installer can't be stopped once it has a package. */}
        {!terminal && o.action !== "install" && (
          <button disabled={files.submitting} onClick={() => act("cancel")}>
            {t("Cancel operation")}
          </button>
        )}
        <button className={terminal ? "primary" : undefined} onClick={files.hideActivity}>
          {terminal ? t("Done") : t("Hide")}
        </button>
      </div>
    </Modal>
  );
}

// A copy or move waiting for its destination.
export function clipText(files: FilesModel) {
  const clip = files.clip;
  if (!clip) return "";
  return t(clip.action === "copy" ? "Copy {name}: open the folder to copy it to, then choose Paste here." : "Move {name}: open the folder to move it to, then choose Paste here.", { name: clip.name });
}
export function canPaste(files: FilesModel) {
  const clip = files.clip;
  if (!clip || !files.storageId || files.running || files.submitting) return false;
  const inside = clip.storageId === files.storageId && (files.path === clip.path || files.path.startsWith(`${clip.path}/`));
  const same = clip.storageId === files.storageId && files.path === (clip.path.includes("/") ? clip.path.slice(0, clip.path.lastIndexOf("/")) : "");
  return !inside && !(clip.action === "move" && same);
}
export function paste(files: FilesModel, report: (message: string) => void) {
  const clip = files.clip;
  if (!clip) return;
  void files
    .run(clip.action, { storageId: clip.storageId, path: clip.path, toStorageId: files.storageId, toPath: files.path })
    .then(() => files.setClip(null))
    .catch((e) => report((e as Error).message));
}
export function FileClip({ files, report, className = "notice" }: { files: FilesModel; report: (message: string) => void; className?: string }) {
  if (!files.clip) return null;
  return (
    <div className={className} role="status">
      <p>{clipText(files)}</p>
      <div className="dialog-actions">
        <button className="primary" disabled={!canPaste(files)} onClick={() => paste(files, report)}>
          {t("Paste here")}
        </button>
        <button onClick={() => files.setClip(null)}>{t("Cancel")}</button>
      </div>
    </div>
  );
}

// Rename, New folder, Delete and the archive password: the same in every look.
export function FileDialogs({ files, drives }: { files: FilesModel; drives: Storage[] }) {
  const dialog = files.dialog;
  const [report, setReport] = useState("");
  const [value, setValue] = useState("");
  const [error, setError] = useState("");
  const [opened, setOpened] = useState<typeof dialog>(null);
  if (dialog !== opened) {
    setOpened(dialog);
    setValue(dialog?.kind === "rename" ? dialog.entry.name : "");
    setError("");
  }
  if (!dialog)
    return (
      <>
        <FileActivity files={files} drives={drives} report={setReport} />
        {report && !files.activityOpen && (
          <p className="form-error" role="alert">
            {report}
          </p>
        )}
      </>
    );
  const close = () => !files.submitting && files.setDialog(null);
  const submit = (action: string, fields: Record<string, unknown>) =>
    void files
      .run(action, fields)
      .then(() => files.setDialog(null))
      .catch((e) => setError((e as Error).message));
  const named = dialog.kind === "rename" || dialog.kind === "folder";
  const valid = !named || (value.trim() && !/[/\\]/.test(value) && value !== "." && value !== "..");
  return (
    <Modal
      title={
        dialog.kind === "rename" ? t("Rename") : dialog.kind === "folder" ? t("New folder") : dialog.kind === "delete" ? t("Delete") : t("Archive password")
      }
      close={close}
    >
      {dialog.kind === "delete" ? (
        <>
          <h1>{t("Delete {name}?", { name: dialog.entry.name })}</h1>
          <p>
            {dialog.entry.folder
              ? t("The folder and everything in it will be deleted from your PS5. This can't be undone.")
              : t("The file will be deleted from your PS5. This can't be undone.")}
          </p>
          <p className="library-path">{placeLine(drives, files.storageId, files.pathOf(dialog.entry))}</p>
        </>
      ) : (
        <>
          <h1>
            {dialog.kind === "rename"
              ? t("Rename {name}", { name: dialog.entry.name })
              : dialog.kind === "folder"
                ? t("New folder")
                : t("This archive needs a password")}
          </h1>
          {dialog.kind === "password" && (
            <p>{t("Orbit tried the passwords it knows. Enter the archive's password to unpack it.")}</p>
          )}
          <label className="field">
            {dialog.kind === "password" ? t("Password") : t("Name")}
            <input
              data-initial-focus
              type={dialog.kind === "password" ? "password" : "text"}
              value={value}
              autoComplete="off"
              spellCheck={false}
              onChange={(e) => setValue(e.target.value)}
            />
          </label>
        </>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
      <div className="dialog-actions">
        <button
          className="primary"
          disabled={files.submitting || !valid}
          onClick={() =>
            dialog.kind === "delete"
              ? submit("delete", { path: files.pathOf(dialog.entry) })
              : dialog.kind === "rename"
                ? submit("rename", { path: files.pathOf(dialog.entry), name: value.trim() })
                : dialog.kind === "folder"
                  ? submit("folder", { name: value.trim() })
                  : submit("password", { id: files.operation?.id, password: value })
          }
        >
          {dialog.kind === "delete" ? t("Delete") : dialog.kind === "rename" ? t("Rename") : dialog.kind === "folder" ? t("Create folder") : t("Unpack")}
        </button>
        <button disabled={files.submitting} onClick={close}>
          {t("Back")}
        </button>
      </div>
    </Modal>
  );
}
