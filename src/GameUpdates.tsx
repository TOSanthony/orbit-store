import { Icon } from "./components";
import { t } from "./i18n";
import { GameIcon } from "./LocalCover";
import { useState } from "react";
import { bytes, type Game, type GameUpdate, type GameUpdateJob } from "./types";

// The catalogue game with this title ID, for its cover (apps and unlisted games have none).
function cover(catalogue: Game[], titleId: string): Game | undefined {
  return catalogue.find((g) => g.releases.some((r) => r.titleId === titleId));
}

function statusLine(job: GameUpdateJob): string {
  const percent = job.total > 0 ? Math.floor((job.done * 100) / job.total) : 0;
  switch (job.phase) {
    case "preparing":
      return t("Preparing the download…");
    case "downloading":
      return t("Downloading {percent}% · {speed}", {
        percent,
        speed: `${(job.speed / 1048576).toFixed(1)} MB/s`,
      });
    case "verifying":
      return t("Checking the downloaded files…");
    case "paused":
      return `${t("Paused")} · ${percent}%`;
    case "installing":
      return t("Installing {percent}%", { percent: job.installPercent });
    case "installed":
      return t("Installed");
    case "downloaded":
      return job.error ? t(job.error) : t("Downloaded");
    case "cancelled":
      return t("Cancelled");
    default: {
      const why = t(job.error) + (job.detail ? ` (${job.detail})` : "");
      return t("Update failed: {error}", { error: why });
    }
  }
}

type UpdateAction =
  | "download"
  | "pause"
  | "cancel"
  | "queue"
  | "queueAll"
  | "dequeue"
  | "clearQueue";

// Installed games that have a newer patch on Sony's servers. The console decides what is listed.
export function GameUpdates({
  games,
  checking,
  check,
  job,
  queue,
  act,
  catalogue,
}: {
  games: GameUpdate[];
  checking: boolean;
  check: () => Promise<void>;
  job: GameUpdateJob | null;
  queue: string[];
  catalogue: Game[];
  act: (action: UpdateAction, titleId?: string) => Promise<void>;
}) {
  const [message, setMessage] = useState("");
  const run = (action: UpdateAction, id = "") => {
    setMessage("");
    act(action, id).catch((e: unknown) =>
      setMessage(e instanceof Error ? e.message : ""),
    );
  };
  const ready = games.filter((game) => game.state === "available").length;
  const totalBytes = games.reduce((sum, game) => sum + (game.size ?? 0), 0);
  // One update has the line while it runs or is paused; the others wait behind it.
  const busy = !!job && (job.active || job.phase === "paused");
  return (
    <section className="page">
      <div className="page-heading">
        <h1>{t("Game updates")}</h1>
        <span>{t("Updates found: {count}", { count: ready })}</span>
        <button
          disabled={checking}
          onClick={() => void check().catch(() => {})}
        >
          {checking ? t("Checking…") : t("Check for game updates")}
        </button>
        {games.length > 0 && (
          <button onClick={() => run("queueAll")}>
            {t("Update all")} ({games.length})
            {totalBytes ? ` · ${bytes(totalBytes)}` : ""}
          </button>
        )}
        {queue.length > 0 && (
          <button onClick={() => run("clearQueue")}>{t("Clear queue")}</button>
        )}
      </div>
      {message && <p className="muted">{message}</p>}
      {games.length === 0 ? (
        <div className="empty">
          <Icon name="download" />
          <h2>{t("No game updates")}</h2>
          <p>
            {t(
              "Installed games with a newer patch on Sony’s servers appear here.",
            )}
          </p>
        </div>
      ) : (
        <ul className="game-updates">
          {games.map((game) => (
            <li key={game.titleId}>
              <GameIcon
                className="update-cover"
                titleId={game.titleId}
                cover={cover(catalogue, game.titleId)?.cover}
                fallback={cover(catalogue, game.titleId)?.coverFallback}
              />
              <div className="update-body">
                <div>
                  <strong>{game.name || game.titleId}</strong>
                  <span>{game.titleId}</span>
                </div>
                <p>
                  {t("Installed {version}", { version: game.installed || "?" })}
                  {" → "}
                  {t("Available {version}", { version: game.version })}
                  {game.size ? ` · ${bytes(game.size)}` : ""}
                </p>
                {queue.includes(game.titleId) && (
                  <p>
                    {t("Waiting")} · #{queue.indexOf(game.titleId) + 1}
                  </p>
                )}
                {job && job.titleId === game.titleId && (
                  <>
                    <p>{statusLine(job)}</p>
                    {(job.active || job.phase === "paused") &&
                      job.total > 0 && (
                        <progress value={job.done} max={job.total} />
                      )}
                  </>
                )}
                {game.state === "available" &&
                  (() => {
                    const mine = job?.titleId === game.titleId ? job : null;
                    const paused =
                      !!mine && !mine.active && mine.phase === "paused";
                    const installing =
                      !!mine?.active && mine.phase === "installing";
                    const running = !!mine?.active && !installing;
                    const waiting = queue.includes(game.titleId);
                    return (
                      <div className="update-actions">
                        {running && (
                          <button onClick={() => run("pause", game.titleId)}>
                            {t("Pause")}
                          </button>
                        )}
                        {paused && (
                          <button onClick={() => run("download", game.titleId)}>
                            {t("Resume")}
                          </button>
                        )}
                        {waiting && (
                          <button onClick={() => run("dequeue", game.titleId)}>
                            {t("Remove from queue")}
                          </button>
                        )}
                        {!running && !paused && !installing && !waiting && (
                          <button
                            onClick={() =>
                              run(busy ? "queue" : "download", game.titleId)
                            }
                          >
                            {busy
                              ? t("Add to queue")
                              : t("Download and install")}
                          </button>
                        )}
                        {(running || paused || installing) && (
                          <button onClick={() => run("cancel", game.titleId)}>
                            {t("Cancel")}
                          </button>
                        )}
                      </div>
                    );
                  })()}
                {game.state === "needs-firmware" && (
                  <p className="muted">
                    {t("Needs firmware {firmware} or later", {
                      firmware: game.firmware ?? "",
                    })}
                  </p>
                )}
              </div>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
