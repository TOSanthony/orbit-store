// Crossbar's Files: the drives, then each folder as a list. The focused entry's
// actions sit on its row and its details beside it, as Library's games do.
import { useState } from "react";
import { menuKeys } from "./menuKeys";
import { bytes, type Storage } from "../types";
import { driveRoot } from "../LibraryActions";
import type { FilesModel } from "../useFiles";
import { placeName, t, tn } from "../i18n";
import {
  FileDialogs,
  activityLine,
  canPaste,
  clipText,
  fileActions,
  fileFacts,
  fileGlyph,
  fileLine,
  fileTypeName,
  lockedText,
  paste,
} from "../FileActions";
import { CrossbarList, type XbRow } from "./CrossbarList";
import { Glyph } from "./Glyph";

export function CrossbarFiles({
  files,
  drives,
  consoleBrowser,
  leave,
}: {
  files: FilesModel;
  drives: Storage[];
  consoleBrowser: boolean;
  // Circle at the top of Files: back to Library.
  leave: () => void;
}) {
  const [current, setCurrent] = useState<string | undefined>();
  const [error, setError] = useState("");
  const back = () => {
    if (files.storageId) {
      const name = files.path ? files.path.slice(files.path.lastIndexOf("/") + 1) : files.storageId;
      files.up();
      setCurrent(files.path ? `entry-${name}` : `drive-${name}`);
    } else leave();
    return true;
  };
  const breadcrumb = (
    <p className="xb-breadcrumb">
      {t("Library")} <span aria-hidden="true">›</span> {t("Files")}
      {files.storageId && (
        <>
          {" "}
          <span aria-hidden="true">›</span> {placeName(drives.find((d) => d.id === files.storageId)?.label || "")}
        </>
      )}
      {files.path.split("/").filter(Boolean).map((part, i) => (
        <span key={i}>
          {" "}
          <span aria-hidden="true">›</span> {part}
        </span>
      ))}
    </p>
  );
  const [menu, setMenu] = useState("");
  const status = (
    <>
      {files.activity && !files.activityOpen && (
        <button className="xb-pill" onClick={files.showActivity}>
          {activityLine(files)}
        </button>
      )}
      {(error || files.error) && <p className="xb-card-error">{error || files.error}</p>}
    </>
  );
  // The drives: space on the row, the focused one's details beside.
  if (!files.storageId) {
    const chosen = drives.find((d) => `drive-${d.id}` === current) || drives[0];
    const rows: XbRow[] = drives.map(
      (d): XbRow => ({
        key: `drive-${d.id}`,
        activateOnClick: true,
        activate: () => files.open(d.id),
        render: () => (
          <>
            <span className="xb-row-icon">
              <Glyph name="drive" size={22} />
            </span>
            <span className="xb-row-text">
              <span className="xb-row-title">{placeName(d.label)}</span>
              <span className="xb-row-sub">{t("{free} free of {total}", { free: bytes(d.freeBytes), total: bytes(d.totalBytes) })}</span>
              <span className="xb-progress" aria-hidden="true">
                <i style={{ width: `${d.totalBytes ? Math.min(100, (100 * (d.totalBytes - d.freeBytes)) / d.totalBytes) : 0}%` }} />
              </span>
            </span>
            <Glyph name="next" size={18} />
          </>
        ),
        actions: (
          <button className="primary" onClick={() => files.open(d.id)}>
            <Glyph name="folder" size={15} stroke={2} />
            {t("Open drive")}
          </button>
        ),
      }),
    );
    return (
      <section className="xb-page xb-library">
        <div className="xb-split">
          <div className="xb-column">
            {breadcrumb}
            {rows.length ? (
              <CrossbarList rows={rows} current={current} setCurrent={setCurrent} back={back} ariaLabel={t("Files")} />
            ) : (
              <div className="xb-empty">
                <h2>{t("No drives reported")}</h2>
              </div>
            )}
          </div>
          <aside className="xb-aside xb-card">
            {chosen ? (
              <>
                <p className="xb-card-kicker">{t("Files")}</p>
                <h2>{placeName(chosen.label)}</h2>
                <p>{t("{size} free", { size: bytes(chosen.freeBytes) })}</p>
                <p className="xb-card-fine">
                  {t("{used} used of {total}", { used: bytes(chosen.totalBytes - chosen.freeBytes), total: bytes(chosen.totalBytes) })}
                </p>
                <p className="xb-card-fine library-path">{driveRoot(chosen)}</p>
              </>
            ) : (
              <p>{t("Browse, copy, move and delete files on your PS5’s drives.")}</p>
            )}
            {files.clip && <p className="xb-card-fine">{clipText(files)}</p>}
            {status}
          </aside>
        </div>
        <FileDialogs files={files} drives={drives} />
      </section>
    );
  }
  const rows: XbRow[] = files.entries.map((entry) => ({
    key: `entry-${entry.name}`,
    activateOnClick: entry.folder,
    activate: entry.folder ? () => files.open(files.storageId, files.pathOf(entry)) : undefined,
    render: () => (
      <>
        <span className="xb-row-icon">
          <Glyph name={fileGlyph[entry.type]} size={22} />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">
            {entry.name}
            {entry.locked && <span className="xb-tag">{t("In use")}</span>}
          </span>
          <span className="xb-row-sub">{fileLine(files, entry)}</span>
        </span>
        {entry.folder && <Glyph name="next" size={18} />}
      </>
    ),
    actions: (() => {
      // The way in on the row, every action under More, as Downloads does.
      const all = fileActions(files, entry, consoleBrowser, setError);
      return (
        <>
          {all.filter((a) => a.primary).slice(0, 1).map((a) => (
            <button key={a.key} className={a.primary ? "primary" : undefined} disabled={a.disabled} onClick={a.run}>
              {a.glyph && <Glyph name={a.glyph} size={15} stroke={2} />}
              {a.label}
            </button>
          ))}
          <span className="xb-menu-anchor">
            <button
              className="xb-more"
              aria-label={t("More")}
              aria-haspopup="menu"
              aria-expanded={menu === entry.name}
              onClick={() => setMenu(menu === entry.name ? "" : entry.name)}
            >
              <Glyph name="more" size={16} stroke={2} />
            </button>
            {menu === entry.name && (
              <span className="xb-menu" role="menu" aria-label={entry.name} data-own-arrows onKeyDown={(e) => menuKeys(e, () => setMenu(""))}>
                {all.map((a, i) => (
                  <button
                    key={a.key}
                    role="menuitem"
                    autoFocus={i === all.findIndex((x) => !x.disabled)}
                    disabled={a.disabled}
                    onClick={() => {
                      setMenu("");
                      a.run();
                    }}
                  >
                    <Glyph name={a.glyph || "more"} size={16} stroke={2} />
                    <span>
                      <strong>{a.label}</strong>
                    </span>
                  </button>
                ))}
              </span>
            )}
          </span>
        </>
      );
    })(),
  }));
  if (files.clip)
    rows.unshift({
      key: "paste",
      compact: true,
      disabled: !canPaste(files),
      activate: () => paste(files, setError),
      render: () => (
        <>
          <span className="xb-row-icon">
            <Glyph name={files.clip!.action === "copy" ? "copy" : "move"} size={22} />
          </span>
          <span className="xb-row-text">
            <span className="xb-row-title">{t("Paste here")}</span>
            <span className="xb-row-sub">{clipText(files)}</span>
          </span>
        </>
      ),
      actions: <button onClick={() => files.setClip(null)}>{t("Cancel")}</button>,
    });
  rows.push({
    key: "new-folder",
    compact: true,
    disabled: files.running,
    activate: () => files.setDialog({ kind: "folder" }),
    render: () => (
      <>
        <span className="xb-row-icon">
          <Glyph name="folder" size={22} />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{t("New folder")}</span>
        </span>
      </>
    ),
  });
  const focused = files.entries.find((e) => `entry-${e.name}` === current) || (current ? undefined : files.entries[0]);
  return (
    <section className="xb-page xb-library">
      <div className="xb-split">
        <div className="xb-column">
          {breadcrumb}
          {files.entries.length || files.clip ? (
            <CrossbarList rows={rows} current={current} setCurrent={setCurrent} back={back} ariaLabel={t("Files")} />
          ) : (
            <div className="xb-empty">
              <h2>{files.loading ? t("Reading folder…") : t("This folder is empty")}</h2>
              <button className="xb-pill primary" onClick={() => files.setDialog({ kind: "folder" })}>
                {t("New folder")}
              </button>
            </div>
          )}
        </div>
        <aside className="xb-aside xb-card" aria-live="polite">
          {focused ? (
            <>
              <p className="xb-card-kicker">
                {fileTypeName(focused)} · {placeName(drives.find((d) => d.id === files.storageId)?.label || "")}
              </p>
              <h2>{focused.name}</h2>
              <dl className="xb-facts">
                {fileFacts(files, focused, drives).map(([label, value]) => (
                  <div key={label}>
                    <dt>{label}</dt>
                    <dd className={label === t("Location") ? "library-path" : undefined}>{value}</dd>
                  </div>
                ))}
              </dl>
              {focused.locked && <p className="xb-card-fine">{lockedText(focused.locked)}</p>}
            </>
          ) : (
            <>
              <p className="xb-card-kicker">{tn(files.entries.length, "{count} item", "{count} items")}</p>
              <p>{files.clip ? clipText(files) : t("Browse, copy, move and delete files on your PS5’s drives.")}</p>
            </>
          )}
          {status}
        </aside>
      </div>
      <FileDialogs files={files} drives={drives} />
    </section>
  );
}
