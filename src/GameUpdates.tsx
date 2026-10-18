import { Icon } from "./components";
import { t } from "./i18n";
import type { GameUpdate } from "./types";

// Installed games that have a newer patch on Sony's servers. The console decides what is listed.
export function GameUpdates({
  games,
  checking,
  check,
}: {
  games: GameUpdate[];
  checking: boolean;
  check: () => Promise<void>;
}) {
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
