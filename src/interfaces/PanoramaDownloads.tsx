import { InstallationSummary } from "../Installation";
// Panorama's Downloads: Active, Failed, Finished and Cancelled side by side as
// groups of tiles. The first active download is a large tile; the focused
// download shows its buttons on its tile, More opens a flyout with every
// action, and a finished or failed download's actions are tiles of their own.
// Actions and dialogs are Downloads' own.
import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties, type ReactNode } from "react";
import { bytes, formatLabel, type Game, type Job, type Storage } from "../types";
import { formatNumber, placeName, t, tn } from "../i18n";
import { jobActions, progressOf, statusLabels, type DownloadView, type JobAction, type JobCommand } from "../jobs";
import { Glyph, type GlyphName } from "./Glyph";
import { PnGroup, PnTile } from "./PanoramaParts";
import { firstEnabled, menuKeys } from "./menuKeys";

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
  remove: "history",
};
const emptyText: Record<Exclude<DownloadView, "Active">, [string, string]> = {
  Failed: [/* i18n */ "No failed downloads", /* i18n */ "Downloads that need your attention appear here."],
  Finished: [/* i18n */ "No finished downloads", /* i18n */ "Successfully completed downloads appear here."],
  Cancelled: [/* i18n */ "No cancelled downloads", /* i18n */ "Cancelled downloads appear here. Kept partial files can be resumed."],
};

export function PanoramaDownloads({
  groups,
  queued,
  targetId,
  gameByRelease,
  drives,
  concurrent,
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
  targetId: string | null;
  gameByRelease: Map<string, Game>;
  drives: Storage[];
  concurrent?: number;
  busy: string;
  error: string;
  message: string;
  run: (job: Job, name: string, remove?: boolean) => Promise<void>;
  cancel: (job: Job) => void;
  password: (job: Job) => void;
  clear: (view: "Finished" | "Cancelled") => void;
  browse: () => void;
  openLibrary?: (job: Job) => void;
  dialogs: ReactNode;
}) {
  const [chosen, setCurrent] = useState<string | null>(targetId);
  // The first active download is focused until another is chosen, as designed.
  const current = chosen ?? groups.Active[0]?.id ?? null;
  const [menu, setMenu] = useState<string | null>(null);
  useEffect(() => {
    if (targetId) setCurrent(targetId);
  }, [targetId]);
  useEffect(() => setMenu(null), [current]);
  // The flyout sits beside More, kept on screen (the page scrolls sideways and clips).
  const anchor = useRef<HTMLSpanElement>(null);
  const [place, setPlace] = useState<CSSProperties>({});
  useLayoutEffect(() => {
    const box = anchor.current?.getBoundingClientRect();
    if (!menu || !box) return;
    const width = 288, height = 320;
    setPlace({
      left: Math.min(box.right + 8, window.innerWidth - width - 16),
      top: Math.max(16, Math.min(box.top, window.innerHeight - height - 16)),
    });
  }, [menu]);
  const doCommand = (job: Job, command: JobCommand) => {
    setMenu(null);
    if (command === "cancel-dialog") cancel(job);
    else if (command === "password") password(job);
    else void run(job, command);
  };
  const facts = (job: Job) => {
    const game = gameByRelease.get(job.releaseId);
    const release = game?.releases.find((r) => r.id === job.releaseId);
    const drive = drives.find((d) => d.id === job.storageId);
    return { game, release, drive, title: game?.title || job.title || job.filename, actions: jobActions(job, { busy: !!busy, drives, queued }) };
  };
  const label = (job: Job, a: JobAction) => (a.command === "cancel-dialog" && job.status !== "cancelled" ? t("Cancel download") : t(a.label));
  // A focused download's buttons on its tile, More with every action beside it.
  const tileButtons = (job: Job, actions: JobAction[]) => (
    <>
      {actions
        .filter((a) => a.command !== "move-up" && a.command !== "move-down" && a.command !== "refresh-link")
        .slice(0, 2)
        .map((a, i) => (
          <button key={a.command} className={i === 0 ? "primary" : ""} disabled={a.disabled} onClick={() => doCommand(job, a.command)}>
            <Glyph name={commandGlyph[a.command]} size={13} stroke={2} />
            {t(a.label)}
          </button>
        ))}
      {actions.length > 2 && (
        <span className="pn-flyout-anchor" ref={menu === job.id ? anchor : undefined}>
          <button aria-haspopup="menu" aria-expanded={menu === job.id} onClick={() => setMenu((m) => (m === job.id ? null : job.id))}>
            <Glyph name="more" size={13} stroke={2} />
            {t("More")}
          </button>
          {menu === job.id && (
            <span
              className="pn-flyout"
              style={place}
              role="menu"
              aria-label={facts(job).title}
              data-own-arrows
              onKeyDown={(e) => menuKeys(e, () => setMenu(null))}
            >
              {actions.map((a, i) => (
                <button
                  key={a.command}
                  role="menuitem"
                  autoFocus={i === firstEnabled(actions)}
                  disabled={a.disabled}
                  onClick={() => doCommand(job, a.command)}
                >
                  <Glyph name={commandGlyph[a.command]} size={15} stroke={2} />
                  <span>
                    <strong>{label(job, a)}</strong>
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
  );
  // One download as a tile: its state, title and progress; large for the first active one.
  const jobTile = (job: Job, size: "large" | "wide") => {
    const { game, release, drive, title, actions } = facts(job);
    const { extracting, done, total, percent } = progressOf(job);
    const status = t(statusLabels[job.status] || job.status);
    const lost = !drive && job.status !== "complete";
    const timing =
      job.status === "retrying"
        ? Math.ceil(job.retryAt - Date.now() / 1000) > 0
          ? t("Retrying in {seconds} s", { seconds: Math.ceil(job.retryAt - Date.now() / 1000) })
          : t("Waiting to retry…")
        : job.speed > 0
          ? t("{speed}/s · {minutes} min remaining", { speed: bytes(job.speed), minutes: Math.ceil((total - done) / job.speed / 60) })
          : "";
    const percentText = formatNumber(percent / 100, { style: "percent", maximumFractionDigits: 0 });
    const sub =
      job.status === "complete"
        ? [status, job.verification === "sha256" ? t("SHA-256 verified") : t("Size verified"), bytes(job.total)].join(" · ")
        : job.status === "error"
          ? [status, job.error && t(job.error)].filter(Boolean).join(" · ")
          : size === "large"
            ? [`${percentText} · ${bytes(done)} / ${bytes(total)}`, timing].filter(Boolean).join(" · ")
            : [
                status.toLowerCase(),
                lost ? t("Reconnect this drive") : "",
                job.delivery === "torbox" ? t("via TorBox") : "",
                job.deliveryPhase && ["queued", "downloading"].includes(job.status) ? t(job.deliveryPhase) : "",
                done > 0 ? percentText : total > 0 ? bytes(total) : "",
              ]
                .filter(Boolean)
                .join(" · ");
    const focused = current === job.id;
    return (
      <PnTile
        key={job.id}
        size={size}
        tone={job.status === "error" ? "red" : undefined}
        art={job.status === "error" ? undefined : game?.hero || game?.cover}
        fallback={game?.heroFallback || game?.cover}
        glyph={job.status === "error" ? "warn" : undefined}
        kicker={
          focused && size === "large"
            ? [extracting ? t("Extracting") : status, release?.provider, release && formatLabel(release)].filter(Boolean).join(" · ")
            : undefined
        }
        title={title}
        sub={sub}
        progress={job.status === "complete" || job.status === "error" || (job.status === "cancelled" && !job.received) ? undefined : percent}
        label={`${title} · ${status}`}
        current={focused}
        onClick={() => setCurrent(job.id)}
        onFocus={() => setCurrent(job.id)}
      >
        {focused && job.status !== "complete" && job.status !== "error" ? tileButtons(job, actions) : null}
      </PnTile>
    );
  };
  // A finished, failed or cancelled download's actions as tiles of their own.
  const actionTiles = (job: Job | undefined) => {
    if (!job) return null;
    const { actions } = facts(job);
    return (
      <>
        <InstallationSummary job={job} />
        {job.status === "complete" && openLibrary && (
          <PnTile key="library" tone="dark" glyph="books" title={t("View in Library")} onClick={() => openLibrary(job)} />
        )}
        {actions
          .filter((a) => a.command !== "move-up" && a.command !== "move-down")
          .map((a) => (
            <PnTile
              key={a.command}
              tone="dark"
              glyph={commandGlyph[a.command]}
              title={label(job, a)}
              disabled={a.disabled}
              onClick={() => doCommand(job, a.command)}
            />
          ))}
      </>
    );
  };
  // The focused download in a group, else its first: its actions follow its tiles.
  const focusedIn = (list: Job[]) => list.find((j) => j.id === current) || list[0];
  const empty = (view: Exclude<DownloadView, "Active">) => (
    <div className="pn-empty-card" key={view}>
      <span className="pn-kicker">{t(view)}</span>
      <strong>{t(emptyText[view][0])}</strong>
      <small>{t(emptyText[view][1])}</small>
    </div>
  );
  const projected = drives.find((d) => d.external) || drives[0];
  const active = groups.Active;
  const others = (["Failed", "Finished", "Cancelled"] as const).filter((v) => groups[v].length);
  const blank = (["Failed", "Finished", "Cancelled"] as const).filter((v) => !groups[v].length);
  return (
    <section className="pn-downloads">
      {!active.length && (
        <div className="pn-hero-empty">
          <span className="pn-kicker">{t("Active")}</span>
          <h2>{t("Nothing in progress")}</h2>
          <p>{t("Choose a game to add it to your queue.")}</p>
          <button className="primary" onClick={browse}>
            {t("Browse games")}
          </button>
        </div>
      )}
      {(active.length > 0 || others.length > 0) && (
        <div className="pn-page" data-scroll="x">
          {active.length > 0 && (
            <PnGroup title={t("Active")} count={formatNumber(active.length)} className="pn-feature">
              {active.map((job, i) => jobTile(job, i === 0 ? "large" : "wide"))}
              {concurrent ? (
                <PnTile
                  tone="dark"
                  number={formatNumber(concurrent)}
                  title={t("at once")}
                  sub={t("simultaneous downloads")}
                  label={tn(concurrent, "{count} download at a time", "{count} downloads at a time")}
                />
              ) : null}
              {projected && (
                <PnTile
                  tone="dark"
                  glyph="drive"
                  title={bytes(projected.projectedFreeBytes)}
                  sub={t("free after your queue")}
                  label={t("{size} free after your queue", { size: bytes(projected.projectedFreeBytes) })}
                />
              )}
            </PnGroup>
          )}
          {others.map((view) => (
            <PnGroup key={view} title={t(view)} count={formatNumber(groups[view].length)}>
              {groups[view].map((job) => jobTile(job, "wide"))}
              {actionTiles(focusedIn(groups[view]))}
              {view !== "Failed" && (
                <PnTile
                  tone="dark"
                  glyph="history"
                  title={view === "Cancelled" ? t("Clear cancelled history") : t("Clear finished history")}
                  disabled={!!busy}
                  onClick={() => clear(view)}
                />
              )}
            </PnGroup>
          ))}
        </div>
      )}
      {blank.length > 0 && <div className="pn-empty-cards">{blank.map(empty)}</div>}
      {(error || message) && (
        <p className={error ? "pn-status warn" : "pn-status"} role={error ? "alert" : "status"}>
          {error || message}
        </p>
      )}
      {dialogs}
    </section>
  );
}
