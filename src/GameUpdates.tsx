import { Icon } from "./components";
import { t } from "./i18n";
import { useState } from "react";
import type { GameUpdate, GameUpdateJob } from "./types";

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

// Installed games that have a newer patch on Sony's servers. The console decides what is listed.
export function GameUpdates({
  games,
  checking,
  check,
  job,
  act,
}: {
  games: GameUpdate[];
  checking: boolean;
  check: () => Promise<void>;
  job: GameUpdateJob | null;
  act: (action: "download" | "cancel", titleId: string) => Promise<void>;
}) {
  const [message, setMessage] = useState("");
  const run = (action: "download" | "cancel", id: string) => {
    setMessage("");
    act(action, id).catch((e: unknown) => setMessage(e instanceof Error ? e.message : ""));
  };
  const ready = games.filter((game) => game.state === "available").length;
  return (
    <section className="page">
      <div className="page-heading">
        <h1>{t("Game updates")}</h1>
        <span>{t("Updates found: {count}", { count: ready })}</span>
        <button disabled={checking} onClick={() => void check().catch(() => {})}>
          {checking ? t("Checking…") : t("Check for game updates")}
        </button>
      </div>
      {message && <p className="muted">{message}</p>}
      {games.length === 0 ? (
        <div className="empty">
          <Icon name="download" />
          <h2>{t("No game updates")}</h2>
          <p>
            {t("Installed games with a newer patch on Sony’s servers appear here.")}
          </p>
        </div>
      ) : (
        <ul className="game-updates">
          {games.map((game) => (
            <li key={game.titleId}>
              <div>
                <strong>{game.name || game.titleId}</strong>
                <span>{game.titleId}</span>
              </div>
              <p>
                {t("Installed {version}", { version: game.installed || "?" })}
                {" → "}
                {t("Available {version}", { version: game.version })}
              </p>
              {job && job.titleId === game.titleId && (
                <>
                  <p>{statusLine(job)}</p>
                  {job.active && job.total > 0 && (
                    <progress value={job.done} max={job.total} />
                  )}
                </>
              )}
              {game.state === "available" &&
                (job?.active && job.titleId === game.titleId ? (
                  <button onClick={() => run("cancel", game.titleId)}>{t("Cancel")}</button>
                ) : (
                  <button disabled={!!job?.active} onClick={() => run("download", game.titleId)}>
                    {t("Download and install")}
                  </button>
                ))}
              {game.state === "needs-firmware" && (
                <p className="muted">
                  {t("Needs firmware {firmware} or later", {
                    firmware: game.firmware ?? "",
                  })}
                </p>
              )}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
