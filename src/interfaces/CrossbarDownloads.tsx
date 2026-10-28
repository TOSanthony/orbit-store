// Crossbar's Downloads: the queue as a list (Active first, then Failed,
// Finished and Cancelled as folders), the focused download's buttons on its
// row and a summary beside it. Actions and dialogs are Downloads' own.
import { useEffect, useState, type ReactNode } from "react";
import { bytes, formatLabel, type Game, type Job, type Storage } from "../types";
import { formatNumber, placeName, t, tn } from "../i18n";
import { Artwork } from "../Artwork";
import { jobActions, progressOf, statusLabels, type DownloadView, type JobAction, type JobCommand } from "../jobs";
import { CrossbarList, type XbRow } from "./CrossbarList";
import { Glyph, type GlyphName } from "./Glyph";
import { Ring } from "./Ring";
import { firstEnabled, menuKeys } from "./menuKeys";

const folders: { view: DownloadView; glyph: GlyphName; empty: string }[] = [
  { view: "Failed", glyph: "warn", empty: /* i18n */ "No failed downloads" },
  { view: "Finished", glyph: "check", empty: /* i18n */ "No finished downloads" },
  { view: "Cancelled", glyph: "close", empty: /* i18n */ "No cancelled downloads" },
];
const commandGlyph: Record<JobCommand, GlyphName> = {
  packages: "books",
  pause: "pause",
  resume: "play",
  retry: "refresh",
  "cancel-dialog": "close",
  password: "key",
  "refresh-link": "link",
  "move-up": "up",
  "move-down": "arrowDown",
  remove: "trash",
};

export function CrossbarDownloads({
  groups,
  queued,
  view,
  setView,
  targetId,
  gameByRelease,
  drives,
  busy,
  error,
  message,
  run,
  cancel,
  password,
  clear,
  browse,
  openLibrary,
  dialogs,
}: {
  groups: Record<DownloadView, Job[]>;
  queued: Job[];
  view: DownloadView;
  setView: (view: DownloadView) => void;
  targetId: string | null;
  gameByRelease: Map<string, Game>;
  drives: Storage[];
  busy: string;
  error: string;
  message: string;
  run: (job: Job, name: string, remove?: boolean) => Promise<void>;
  cancel: (job: Job) => void;
  password: (job: Job) => void;
  clear: () => void;
  browse: () => void;
  openLibrary?: (job: Job) => void;
  dialogs: ReactNode;
}) {
  const [current, setCurrent] = useState<string | undefined>(targetId || undefined);
  const [menu, setMenu] = useState(false);
  useEffect(() => {
    if (targetId) setCurrent(targetId);
  }, [targetId]);
  useEffect(() => setMenu(false), [current]);
  const list = groups[view];
  const doCommand = (job: Job, command: JobCommand) => {
    setMenu(false);
    if (command === "cancel-dialog") cancel(job);
    else if (command === "password") password(job);
    else void run(job, command);
  };
  const button = (job: Job, a: JobAction, primary: boolean) => (
    <button key={a.command} className={primary ? "primary" : ""} disabled={a.disabled} onClick={() => doCommand(job, a.command)}>
      <Glyph name={commandGlyph[a.command]} size={15} stroke={2} />
      {t(a.label)}
    </button>
  );
  const jobRow = (job: Job): XbRow => {
    const game = gameByRelease.get(job.releaseId);
    const release = game?.releases.find((r) => r.id === job.releaseId);
    const drive = drives.find((d) => d.id === job.storageId);
    const title = game?.title || job.title || job.filename;
    const { extracting, done, total, percent } = progressOf(job);
    const seconds = Math.max(0, Math.ceil(job.retryAt - Date.now() / 1000));
    const actions = jobActions(job, { busy: !!busy, drives, queued });
    const status = t(statusLabels[job.status] || job.status);
    const verified = job.verification === "sha256" ? t("SHA-256 verified") : t("Size verified");
    const lost = !drive && job.status !== "complete";
    const timing =
      job.status === "retrying"
        ? seconds > 0
          ? t("Retrying in {seconds} s", { seconds })
          : t("Waiting to retry…")
        : job.speed > 0
          ? t("{speed}/s · {minutes} min remaining", { speed: bytes(job.speed), minutes: Math.ceil((total - done) / job.speed / 60) })
          : "";
    // The focused card's first line, as in Store's row: where it comes from, or why it stopped.
    const meta =
      job.status === "complete"
        ? [status, verified, bytes(job.total)].join(" · ")
        : job.status === "error"
          ? [status, job.error && t(job.error)].filter(Boolean).join(" · ")
          : [
              release ? `${release.provider} · ${formatLabel(release)}` : "",
              job.delivery === "torbox" ? t("via TorBox") : "",
              drive ? placeName(drive.label) : job.storageId,
              lost ? t("Reconnect this drive") : "",
            ]
              .filter(Boolean)
              .join(" · ");
    // A row out of focus: its state and size in one line.
    const brief =
      job.status === "retrying"
        ? [timing, job.error && t(job.error)].filter(Boolean).join(" · ")
        : [
            status,
            lost ? t("Reconnect this drive") : job.status === "error" && job.error ? t(job.error) : "",
            job.status === "complete" ? bytes(job.total) : done > 0 ? `${bytes(done)} / ${bytes(total)}` : total > 0 ? bytes(total) : "",
          ]
            .filter(Boolean)
            .join(" · ");
    const warn = job.status === "error" || lost;
    const moving = job.status !== "queued" && job.status !== "cancelled" && job.status !== "complete" && done > 0;
    // Store's order: the first action leads, View in Library leads a finished one.
    const finished = job.status === "complete" && openLibrary;
    const shown = actions.slice(0, finished ? 1 : 2);
    return {
      key: job.id,
      render: (focused) => (
        <>
          <span className="xb-row-art">
            {game && <Artwork src={game.cover} fallbackSrc={game.coverFallback} alt="" decoding="async" referrerPolicy="no-referrer" />}
          </span>
          <span className="xb-row-text">
            <span className="xb-row-title">{title}</span>
            <span className={`xb-row-sub${warn ? " warn" : ""}`}>{focused ? meta : brief}</span>
            {(focused ? total > 0 && job.status !== "cancelled" : moving) && (
              <span className="xb-progress" aria-hidden="true">
                <i style={{ width: `${job.status === "complete" ? 100 : percent}%` }} />
              </span>
            )}
            {focused && (
              <span className="xb-row-fine">
                {job.status === "complete"
                  ? job.path && `${t("Location")} ${job.path}`
                  : `${extracting ? `${t("Extracting")} · ` : ""}${bytes(done)} / ${bytes(total)} · ${formatNumber(percent / 100, {
                      style: "percent",
                      minimumFractionDigits: 1,
                      maximumFractionDigits: 1,
                    })}${timing ? ` · ${timing}` : ""}`}
              </span>
            )}
            {focused && job.installation && <span className="xb-row-fine">{job.installation.packages.find(p => ["installing", "unconfirmed", "error", "installed"].includes(p.state))?.message || job.installation.shadowMountMessage}</span>}
            {focused && job.deliveryPhase && ["queued", "downloading"].includes(job.status) && (
              <span className="xb-row-fine">{t(job.deliveryPhase)}</span>
            )}
            {focused && job.status === "cancelled" && job.received > 0 && (
              <span className="xb-row-fine">{t("Partial file kept. Resume it, or delete the partial file before removing this entry.")}</span>
            )}
          </span>
        </>
      ),
      actions: (
        <>
          {finished && (
            <button className="primary" onClick={() => openLibrary(job)}>
              <Glyph name="books" size={15} stroke={2} />
              {t("View in Library")}
            </button>
          )}
          {shown.map((a, i) => button(job, a, !finished && i === 0))}
          {actions.length > shown.length && (
            <span className="xb-menu-anchor">
              <button className="xb-more" aria-label={t("More")} aria-haspopup="menu" aria-expanded={menu} onClick={() => setMenu((v) => !v)}>
                <Glyph name="more" size={16} stroke={2} />
              </button>
              {menu && (
                <span className="xb-menu" role="menu" aria-label={title} data-own-arrows onKeyDown={(e) => menuKeys(e, () => setMenu(false))}>
                  {/* Every action, as More shows in the design. */}
                  {actions.map((a, i) => (
                    <button
                      key={a.command}
                      role="menuitem"
                      autoFocus={i === firstEnabled(actions)}
                      disabled={a.disabled}
                      onClick={() => doCommand(job, a.command)}
                    >
                      <Glyph name={commandGlyph[a.command]} size={16} stroke={2} />
                      <span>
                        <strong>{a.command === "cancel-dialog" && job.status !== "cancelled" ? t("Cancel download") : t(a.label)}</strong>
                        {a.disabled && a.command === "move-up" && <small>{t("Already first")}</small>}
                        {a.disabled && a.command === "move-down" && <small>{t("Already last")}</small>}
                      </span>
                    </button>
                  ))}
                </span>
              )}
            </span>
          )}
        </>
      ),
    };
  };
  const folderRow = (f: (typeof folders)[number]): XbRow => ({
    key: `folder-${f.view}`,
    activate: () => {
      setView(f.view);
      setCurrent(undefined);
    },
    render: () => (
      <>
        <span className={`xb-row-icon${f.view === "Failed" && groups.Failed.length ? " warn" : ""}`}>
          <Glyph name={f.glyph} size={22} />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{t(f.view)}</span>
          <span className="xb-row-sub">
            {!groups[f.view].length
              ? t(f.empty)
              : f.view === "Failed"
                ? tn(groups.Failed.length, "{count} needs attention", "{count} need attention")
                : tn(groups[f.view].length, "{count} download", "{count} downloads")}
          </span>
        </span>
        <Glyph name="next" size={18} />
      </>
    ),
  });
  // Finished and Cancelled end with their history cleanup, as a row.
  const clearRow: XbRow | null =
    (view === "Finished" || view === "Cancelled") && list.length
      ? {
          key: "clear-history",
          disabled: !!busy,
          activate: () => !busy && clear(),
          render: () => (
            <>
              <span className="xb-row-icon">
                <Glyph name="history" size={22} />
              </span>
              <span className="xb-row-text">
                <span className="xb-row-title">{view === "Cancelled" ? t("Clear cancelled history") : t("Clear finished history")}</span>
                <span className="xb-row-sub">
                  {view === "Cancelled" ? t("Kept partial files stay listed") : t("Your downloaded files stay on the drive")}
                </span>
              </span>
            </>
          ),
        }
      : null;
  const rows: XbRow[] =
    view === "Active"
      ? [...list.map(jobRow), ...folders.map(folderRow)]
      : list.length
        ? [...list.map(jobRow), ...(clearRow ? [clearRow] : [])]
        : [
            {
              key: "empty",
              render: () => (
                <span className="xb-row-text">
                  <span className="xb-row-title">{t(folders.find((f) => f.view === view)!.empty)}</span>
                </span>
              ),
            },
          ];
  const running = groups.Active.filter((j) => j.status === "downloading");
  const speed = running.reduce((n, j) => n + (j.speed || 0), 0);
  const share = running.length ? running.reduce((n, j) => n + progressOf(j).percent, 0) / running.length / 100 : 0;
  const projected = drives.find((d) => d.external) || drives[0];
  // Failed: the download that needs its archive password, as the design's card.
  const locked = view === "Failed" ? list.find((j) => j.canEnterArchivePassword) : undefined;
  const lockedTitle = locked && (gameByRelease.get(locked.releaseId)?.title || locked.title || locked.filename);
  const lockedActions = locked ? jobActions(locked, { busy: !!busy, drives, queued }) : [];
  return (
    <section className="xb-page xb-downloads">
      <div className="xb-split">
        <div className="xb-column">
          {view !== "Active" && (
            <p className="xb-breadcrumb">
              {t("Downloads")} <span aria-hidden="true">›</span> {t(view)}
            </p>
          )}
          <CrossbarList
            rows={rows}
            current={current}
            setCurrent={setCurrent}
            back={() => {
              if (view === "Active") return false;
              setCurrent(`folder-${view}`);
              setView("Active");
              return true;
            }}
            ariaLabel={t("Downloads")}
          />
        </div>
        <aside className="xb-aside" aria-live="polite">
          {view === "Active" && (
            <div className="xb-card">
              <p className="xb-card-kicker">{t("Downloads")}</p>
              <h2>
                {tn(groups.Active.length, "{count} active", "{count} active")} ·{" "}
                {tn(groups.Failed.length, "{count} needs attention", "{count} need attention")}
              </h2>
              <p>{t("Downloads run on your PS5 and keep going when this page is closed.")}</p>
              {(speed > 0 || projected) && (
                <p className="xb-card-line">
                  {speed > 0 && (
                    <span>
                      <Ring fraction={share} size={26} />
                      {`${bytes(speed)}/s`}
                    </span>
                  )}
                  {projected && (
                    <span>
                      <Glyph name="drive" size={16} />
                      {t("{size} free after your queue", { size: bytes(projected.projectedFreeBytes) })}
                    </span>
                  )}
                </p>
              )}
              {!list.length && (
                <button className="xb-pill primary" onClick={browse}>
                  {t("Browse games")}
                </button>
              )}
            </div>
          )}
          {locked && (
            <div className="xb-card">
              <p className="xb-card-kicker">{lockedTitle}</p>
              <p>{t("Retry extraction using the downloaded archive. Nothing will be downloaded again.")}</p>
              <p className="xb-card-actions">{lockedActions.slice(0, 2).map((a, i) => button(locked, a, i === 0))}</p>
            </div>
          )}
          {(error || message) && (
            <div className="xb-card xb-card-status">
              {error && (
                <p className="xb-card-error" role="alert">
                  {error}
                </p>
              )}
              {message && <p role="status">{message}</p>}
            </div>
          )}
        </aside>
      </div>
      {dialogs}
    </section>
  );
}
