// Crossbar, Cinema and Panorama: three ways of arranging the same storefront.
// Every interface uses App's state and handlers and today's components for a
// game, Library, Downloads, Browse controls, notices and App settings; only the
// arrangement and the look differ.
import { addonTypes } from "../AddonInfo";
import { useCallback, useEffect, useMemo, useState, type ReactNode, type UIEvent } from "react";
import type { useOrbit } from "../useOrbit";
import { bytes, type Game } from "../types";
import type { BrowseFilters } from "../browse";
import { CATALOG_STEP } from "../browse";
import type { CollectionState } from "../collection";
import { Backdrop, GameTile } from "../components";
import { Artwork } from "../Artwork";
import { formatDate, formatNumber, t, tn } from "../i18n";
import { Glyph, type GlyphName } from "./Glyph";
import { CinemaCollections, CinemaRows, CinemaStage, type CinemaFrame, type CinemaSection } from "./CinemaParts";
import { Ring } from "./Ring";
import { CrossbarList, type XbRow } from "./CrossbarList";
import type { InterfaceName } from "./choice";

export type Tab = "Discover" | "Browse" | "Add-ons" | "Library" | "Downloads";

export interface ShellProps {
  iface: Exclude<InterfaceName, "store">;
  orbit: ReturnType<typeof useOrbit>;
  tab: Tab;
  go: (tab: Tab) => void;
  game: Game | undefined;
  back: () => void;
  open: (game: Game) => void;
  focused: Game | undefined;
  setFocusedId: (id: string) => void;
  newlyAdded: Game[];
  latestGames: Game[];
  discoverGames: Game[];
  filtered: Game[];
  filters: BrowseFilters;
  filter: <K extends keyof BrowseFilters>(key: K, value: BrowseFilters[K]) => void;
  favorites: Set<string>;
  collectionStates: Map<string, CollectionState | undefined>;
  // A game's download in Downloads, and its copy in Library (the first
  // button follows the game's state, as its page does).
  openDownload: (jobId: string) => void;
  openLibrary: (game: { titleId: string; path: string; title: string }) => void;
  active: number;
  paired: (action: () => void) => () => void;
  openSettings: () => void;
  // The settings page when it's open (it replaces the section's content).
  settings: ReactNode | null;
  // An Orbit update is available: Settings carries a dot.
  updateAvailable: boolean;
  openSearch: () => void;
  consoleBrowser: boolean;
  nodes: {
    notices: ReactNode;
    handoffNotices: ReactNode;
    sourceSetup: ReactNode;
    // Opening, unreachable or no games: shown instead of Discover and Browse.
    discoverEmpty: ReactNode;
    details: ReactNode;
    browseSearch: ReactNode;
    browseControls: ReactNode;
    browseEmpty: ReactNode;
    library: ReactNode;
    downloads: ReactNode;
  };
}

export function InterfaceShell(props: ShellProps) {
  if (props.iface === "panorama") return <PanoramaShell {...props} />;
  if (props.iface === "cinema") return <CinemaShell {...props} />;
  return <CrossbarShell {...props} />;
}

/* ------------------------------------------------------------------ */
/* Shared pieces                                                       */
/* ------------------------------------------------------------------ */

const sections: { tab: Tab | "Settings"; label: string; glyph: GlyphName }[] = [
  { tab: "Discover", label: /* i18n */ "Discover", glyph: "spark" },
  { tab: "Browse", label: /* i18n */ "Browse", glyph: "grid" },
  { tab: "Add-ons", label: /* i18n */ "Add-ons", glyph: "layers" },
  { tab: "Library", label: /* i18n */ "Library", glyph: "books" },
  { tab: "Downloads", label: /* i18n */ "Downloads", glyph: "down" },
  { tab: "Settings", label: /* i18n */ "Settings", glyph: "gear" },
];

// Live numbers for the one place each interface shows downloads.
function useDownloadSummary(orbit: ShellProps["orbit"]) {
  return useMemo(() => {
    const running = orbit.jobs.filter((j) =>
      ["queued", "downloading", "retrying", "verifying", "inspecting", "extracting"].includes(j.status),
    );
    const moving = running.filter((j) => j.status === "downloading");
    const total = running.reduce((n, j) => n + (j.total || 0), 0);
    const received = running.reduce((n, j) => n + Math.min(j.received || 0, j.total || 0), 0);
    const speed = moving.reduce((n, j) => n + (j.speed || 0), 0);
    return { count: running.length, downloading: moving.length, fraction: total ? received / total : 0, speed };
  }, [orbit.jobs]);
}

function speedLabel(bytesPerSecond: number) {
  const mb = bytesPerSecond / 1024 ** 2;
  return `${formatNumber(Math.round(mb * 10) / 10)} MB/s`;
}


// The main menu. It keeps the .main-nav class so L1 and R1 move along it.
function SectionNav({
  props,
  className,
  glyphs = false,
  downloadsBadge,
}: {
  props: ShellProps;
  className: string;
  glyphs?: boolean;
  downloadsBadge?: ReactNode;
}) {
  // A game page keeps the section it was opened from, as Store does.
  const current = props.settings ? "Settings" : props.tab;
  return (
    <nav className={`main-nav ${className}`} aria-label={t("Main navigation")}>
      {sections.map((s) => {
        const marks = (
          <>
            {s.tab === "Downloads" && downloadsBadge}
            {s.tab === "Settings" && props.updateAvailable && (
              <i className="nav-dot" role="img" aria-label={t("Update available")} />
            )}
          </>
        );
        return (
          <button
            key={s.tab}
            className={current === s.tab ? "active" : ""}
            aria-current={current === s.tab ? "page" : undefined}
            onClick={() => (s.tab === "Settings" ? props.openSettings() : props.go(s.tab))}
          >
            {glyphs ? (
              <span className="nav-icon">
                <Glyph name={s.glyph} size={26} stroke={1.5} />
                {marks}
              </span>
            ) : null}
            <span className="nav-label">{t(s.label)}</span>
            {!glyphs && marks}
          </button>
        );
      })}
    </nav>
  );
}

const isBrowse = (p: ShellProps) => p.tab === "Browse" || p.tab === "Add-ons";
const collectionLabel = (p: ShellProps) => t(p.tab === "Add-ons" ? "All add-ons" : "All games");
const countLabel = (p: ShellProps) => tn(p.filtered.length, p.tab === "Add-ons" ? "{count} add-on" : "{count} game", p.tab === "Add-ons" ? "{count} add-ons" : "{count} games");
function meta(game: Game) {
  if (game.addonId) return [game.baseGameTitle,t(addonTypes[game.addonType || "content-pack"]),game.platform?.toUpperCase()].filter(Boolean).join(" · ");
  return [game.genre, game.releaseDate?.slice(0, 4), game.platform === "ps4" ? "PS4" : "PS5"]
    .filter(Boolean)
    .join(" · ");
}

// The focused game's buttons, as every version labels them: Download · size
// (on the game's page), View game, and favourite. Crossbar leaves out View
// game: its Download opens the page too, as Cross on the row does.
function GameButtons({
  props,
  game,
  className,
  view = true,
}: {
  props: ShellProps;
  game: Game;
  className: string;
  view?: boolean;
}) {
  const size = game.releases[0]?.sizeBytes;
  const state = props.collectionStates.get(game.id);
  const library = state?.kind === "library" ? state.libraryGame : undefined;
  // The first button follows the game's state, as its page does: its
  // download, its Library copy, or its page to set a download up.
  const job = state?.job;
  return (
    <>
      {job ? (
        <button className={`${className} primary`} onClick={() => props.openDownload(job.id)}>
          <Glyph name="down" size={16} stroke={2} />
          {t("View download")}
        </button>
      ) : library ? (
        <button className={`${className} primary`} onClick={() => props.openLibrary(library)}>
          <Glyph name="books" size={16} />
          {t("View in Library")}
        </button>
      ) : (
        <button className={`${className} primary`} onClick={() => props.open(game)}>
          <Glyph name="down" size={16} stroke={2} />
          {size ? `${t("Download")} · ${bytes(size)}` : t("Download")}
        </button>
      )}
      {view && (
        <button className={className} onClick={() => props.open(game)}>
          <Glyph name="eye" size={16} />
          {t("View game")}
        </button>
      )}
      <FavoriteButton props={props} game={game} />
    </>
  );
}

function FavoriteButton({ props, game }: { props: ShellProps; game: Game }) {
  const [busy, setBusy] = useState(false);
  const on = props.favorites.has(game.id);
  if (game.addonId) return null;
  return (
    <button
      className="icon-button iface-favorite"
      aria-label={on ? t("Remove from favourites") : t("Add to favourites")}
      aria-pressed={on}
      disabled={busy}
      onClick={props.paired(async () => {
        setBusy(true);
        try {
          await props.orbit.favorite(game.id, !on);
        } finally {
          setBusy(false);
        }
      })}
    >
      <Glyph name={on ? "heartFill" : "heart"} />
    </button>
  );
}

// Long lists scroll on, as on the PS5 app (no pages): they mount 96 games at
// a time and add the next 96 when the focus nears the end, so the console
// browser never holds the whole catalogue at once. A new reset key (another
// filter or folder) starts again from the first 96.
function useGrowing(games: Game[], resetKey: string) {
  const [shown, setShown] = useState({ key: resetKey, count: CATALOG_STEP });
  const count = shown.key === resetKey ? shown.count : CATALOG_STEP;
  const more = useCallback(() => {
    if (count < games.length) setShown({ key: resetKey, count: count + CATALOG_STEP });
  }, [count, games.length, resetKey]);
  // The focus on a game this close to the end mounts the next 96.
  const reach = useCallback(
    (id: string | undefined) => {
      const at = id ? games.findIndex((g) => g.id === id) : -1;
      if (at >= 0 && at >= count - 12) more();
    },
    [games, count, more],
  );
  return { visible: games.slice(0, count), total: games.length, reach, more };
}

// A sideways scroll (wheel or trackpad) near the end of a page grows it too.
function growSideways(more: () => void) {
  return (e: UIEvent<HTMLDivElement>) => {
    const el = e.currentTarget;
    if (el.scrollLeft + el.clientWidth > el.scrollWidth - 1600) more();
  };
}

function Tiles({
  props,
  games,
  row,
  id,
  onFocus,
  showDate = false,
  firstPriority = 6,
  focusRow,
}: {
  props: ShellProps;
  games: Game[];
  row: string;
  id?: string;
  onFocus?: (id: string) => void;
  showDate?: boolean;
  firstPriority?: number;
  // A game can be in two rows: only the row with the focus lights its tile.
  focusRow?: string;
}) {
  return (
    <div className="iface-tiles browse-grid" data-row={row} id={id}>
      {games.map((g, i) => (
        <GameTile
          key={g.id}
          game={g}
          open={props.open}
          onFocus={onFocus}
          status={props.collectionStates.get(g.id)?.label}
          favorite={props.favorites.has(g.id)}
          priority={i < firstPriority}
          showDate={showDate}
          featured={props.focused?.id === g.id && (focusRow === undefined || focusRow === row)}
        />
      ))}
    </div>
  );
}

// Controller hints, shown in the PS5 browser like today's footer.
function Hints({ props }: { props: ShellProps }) {
  if (!props.consoleBrowser) return null;
  return (
    <footer className="iface-hints">
      <div className="controller-hints">
        <span>
          <b>×</b>
          {!props.game && !props.settings && (props.tab === "Discover" || isBrowse(props)) ? t("Open game") : t("Select")}
        </span>
        {!props.game && !props.settings && isBrowse(props) && (
          <button onClick={props.openSearch}>
            <b>△</b>
            {t("Search")}
          </button>
        )}
        {!props.game && (
          <span>
            <kbd>L1</kbd>
            <kbd>R1</kbd>
            {/* Cinema's Library and Downloads step their collections first. */}
            {props.iface === "cinema" && !props.settings && (props.tab === "Library" || props.tab === "Downloads") ? t("Collections") : t("Tabs")}
          </span>
        )}
        {props.game && (
          <button onClick={props.back}>
            <b>○</b>
            {t("Back")}
          </button>
        )}
      </div>
    </footer>
  );
}

// The page body every interface shares once its own header is drawn.
function Body({ props, home, browse }: { props: ShellProps; home: ReactNode; browse: ReactNode }) {
  const n = props.nodes;
  return (
    <main className="iface-main">
      {n.handoffNotices}
      {props.settings
        ? props.settings
        : n.sourceSetup
        ? n.sourceSetup
        : !props.orbit.games.length && (props.tab === "Discover" || isBrowse(props)) && !props.game
        ? n.discoverEmpty
        : props.game
          ? n.details
          : props.tab === "Discover"
            ? home
            : isBrowse(props)
              ? browse
              : props.tab === "Library"
                ? n.library
                : n.downloads}
    </main>
  );
}

function sectionTitle(props: ShellProps) {
  if (props.settings) return t("Settings");
  if (props.game) return props.game.addonId && props.game.baseGameTitle
    ? `${props.game.baseGameTitle} · ${props.game.title}` : props.game.title;
  return t(sections.find((s) => s.tab === props.tab)!.label);
}

/* ------------------------------------------------------------------ */
/* Panorama: big light titles, flat tiles, one long page per section   */
/* ------------------------------------------------------------------ */

function PanoramaShell(props: ShellProps) {
  const dl = useDownloadSummary(props.orbit);
  const browseList = useGrowing(props.filtered, JSON.stringify(props.filters));
  const allList = useGrowing(props.discoverGames, "all");
  // The group whose tile has the focus: the one last focused, else the first
  // holding the game (a game can be in New on Orbit and Latest releases).
  const [focusGroup, setFocusGroup] = useState("");
  const groups: [string, Game[]][] = [
    ["added", props.newlyAdded],
    ["latest", props.latestGames.slice(0, 9)],
    ["all", allList.visible],
  ];
  const holds = (key: string) => groups.some(([k, list]) => k === key && list.some((g) => g.id === props.focused?.id));
  const focusRow = holds(focusGroup) ? focusGroup : groups.find(([k]) => holds(k))?.[0] || "";
  const focusIn = (group: string) => (id: string) => {
    setFocusGroup(group);
    props.setFocusedId(id);
  };
  const home = props.focused ? (
    <div className="pn-page" data-scroll="x" onScroll={growSideways(allList.more)}>
      {props.newlyAdded.length > 0 && (
        <section className="pn-group pn-feature" aria-labelledby="pn-new">
          <h2 id="pn-new">{t("New on Orbit")}</h2>
          <Tiles props={props} games={props.newlyAdded} row="added" onFocus={focusIn("added")} focusRow={focusRow} />
        </section>
      )}
      {/* Without release dates there is no Latest releases group, as on the PS5 app and Zero. */}
      {props.latestGames.length > 0 && (
        <section className="pn-group pn-feature" aria-labelledby="pn-latest">
          <h2 id="pn-latest">{t("Latest releases")}</h2>
          <Tiles props={props} games={props.latestGames.slice(0, 9)} row="latest" onFocus={focusIn("latest")} focusRow={focusRow} showDate />
        </section>
      )}
      <section className="pn-group pn-all" aria-labelledby="pn-all">
        <h2 id="pn-all">
          {t("All games")} <span>{tn(props.discoverGames.length, "{count} game", "{count} games")}</span>
        </h2>
        <Tiles
          props={props}
          games={allList.visible}
          row="all"
          onFocus={(id) => {
            focusIn("all")(id);
            allList.reach(id);
          }}
          focusRow={focusRow}
          firstPriority={0}
        />
      </section>
    </div>
  ) : null;
  const browse = (
    <section className="pn-browse browse">
      <div className="pn-browse-bar">
        <div className="page-heading">{props.nodes.browseSearch}</div>
        {props.nodes.browseControls}
      </div>
      {props.filtered.length ? (
        <div className="pn-page" data-scroll="x" onScroll={growSideways(browseList.more)}>
          <section className="pn-group pn-all" aria-label={t(props.tab)}>
            <h2>
              {props.filters.collection === "favorites" ? t("Favourites") : collectionLabel(props)}{" "}
              <span>{countLabel(props)}</span>
            </h2>
            <Tiles
              props={props}
              games={browseList.visible}
              row="browse"
              id="browse-results"
              onFocus={(id) => {
                props.setFocusedId(id);
                browseList.reach(id);
              }}
            />
          </section>
        </div>
      ) : (
        props.nodes.browseEmpty
      )}
    </section>
  );
  return (
    <div className={`iface-shell pn-shell pn-tab-${props.settings ? "settings" : props.game ? "game" : (props.tab === "Add-ons" ? "browse" : props.tab.toLowerCase())}`}>
      <Backdrop games={[...props.orbit.games,...props.orbit.addons]} active={props.game?.id || (props.tab === "Discover" && props.focused?.id) || ""} settle={props.tab === "Discover"} preferPublisherHero={!!props.game} />
      <header className="pn-head">
        {/* A game page has Back in place of the sections, as designed. */}
        {props.game && (
          <button className="pn-back" onClick={props.back}>
            <Glyph name="back" size={16} stroke={2} />
            {t("Back")}
          </button>
        )}
        <SectionNav
          props={props}
          className={`pn-nav${props.game ? " pn-nav-hidden" : ""}`}
          downloadsBadge={
            dl.count > 0 ? (
              <span className="nav-live" aria-label={tn(dl.count, "{count} download in progress", "{count} downloads in progress")}>
                <Ring fraction={dl.fraction} size={16} />
                {formatNumber(dl.count)}
              </span>
            ) : null
          }
        />
        <div className="pn-tools">
          <button className="icon-button" aria-label={t("Search games")} data-command="search" onClick={props.openSearch}>
            <Glyph name="search" size={20} />
          </button>
        </div>
      </header>
      <div className="pn-titles" aria-hidden="true">
        <span className="pn-word">{sectionTitle(props)}</span>
      </div>
      {props.nodes.notices}
      <Body props={props} home={home} browse={browse} />
      {/* Discover's focused game: its buttons under the page, on the hints line. */}
      {props.tab === "Discover" && !props.game && !props.settings && props.focused && (
        <div className="pn-focusbar">
          <GameButtons props={props} game={props.focused} className="pn-focus-button" />
        </div>
      )}
      <Hints props={props} />
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Cinema: one game at a time, full screen, with a film strip          */
/* ------------------------------------------------------------------ */

function cinemaFrames(props: ShellProps, games: Game[]): CinemaFrame[] {
  return games.map((g) => {
    const state = props.collectionStates.get(g.id);
    const job = state?.job;
    return {
      key: g.id,
      favorite: props.favorites.has(g.id),
      label: g.title,
      subtitle: g.addonId ? `${g.baseGameTitle} · ${t(addonTypes[g.addonType || "content-pack"])}` : undefined,
      art: g.hero || g.cover,
      fallback: g.heroFallback || g.cover,
      badge: state?.label ? t(state.label) : undefined,
      progress: job && job.total > 0 && !["complete", "cancelled"].includes(job.status) ? job.received / job.total : undefined,
    };
  });
}

function CinemaShell(props: ShellProps) {
  const dl = useDownloadSummary(props.orbit);
  const [searching, setSearching] = useState(false);
  useEffect(() => {
    if (!isBrowse(props)) setSearching(false);
  }, [props.tab]);
  // Discover's rows, one below the other as the default has them: New on
  // Orbit and Latest releases, then All games.
  const allList = useGrowing(props.discoverGames, "all");
  const browseList = useGrowing(props.filtered, JSON.stringify(props.filters));
  const homeLists: { key: string; label: string; games: Game[]; grid?: boolean }[] = [
    { key: "new", label: /* i18n */ "New on Orbit", games: props.newlyAdded },
    { key: "latest", label: /* i18n */ "Latest releases", games: props.latestGames },
    { key: "all", label: /* i18n */ "All games", games: allList.visible, grid: true },
  ].filter((l) => l.games.length > 0);
  const homeSections: CinemaSection[] = homeLists.map((l) => ({
    key: l.key,
    label: t(l.label),
    frames: cinemaFrames(props, l.games),
    ...(l.grid ? { grid: true, total: allList.total, reach: allList.reach, more: allList.more } : {}),
  }));
  // The frame last chosen; the same game can be in two rows.
  const [homeAt, setHomeAt] = useState<{ section: string; key: string } | null>(null);
  const holds = (at: { section: string; key: string } | null) =>
    !!at && homeLists.some((l) => l.key === at.section && l.games.some((g) => g.id === at.key));
  const focusedList = homeLists.find((l) => l.games.some((g) => g.id === props.focused?.id));
  const homeCurrent = holds(homeAt)
    ? homeAt!
    : focusedList && props.focused
      ? { section: focusedList.key, key: props.focused.id }
      : homeLists[0] && { section: homeLists[0].key, key: homeLists[0].games[0].id };
  const homeGame = homeCurrent && homeLists.find((l) => l.key === homeCurrent.section)?.games.find((g) => g.id === homeCurrent.key);
  // The stage shows the focused result, else the first.
  const browseGame = browseList.visible.find((g) => g.id === props.focused?.id) || browseList.visible[0];
  const favourites = props.filters.collection === "favorites";
  const { platform, source, format, tags, hideInstalled, region, size, addonType } = props.filters;
  const narrowed = !!(platform || source.length || format.length || tags.length || hideInstalled || region || size || addonType);
  const stepBrowse = (delta: number) => {
    const next = !favourites && delta > 0 ? "favorites" : favourites && delta < 0 ? "all" : null;
    if (!next || props.tab === "Add-ons") return false;
    if (next === "favorites") props.paired(() => props.filter("collection", "favorites"))();
    else props.filter("collection", "all");
    return true;
  };
  const home = homeGame && (
    <div className="cn-page cn-rowspage">
      <CinemaStage kicker={meta(homeGame)} title={homeGame.title} fixed>
        <p className="cn-tagline">{homeGame.tagline}</p>
      </CinemaStage>
      <CinemaRows
        sections={homeSections}
        current={homeCurrent}
        choose={(section, key) => {
          setHomeAt({ section, key });
          props.setFocusedId(key);
        }}
        open={(section, key) => {
          const game = homeLists.find((l) => l.key === section)?.games.find((g) => g.id === key);
          if (game) props.open(game);
        }}
      />
    </div>
  );
  const browseLabel = narrowed ? t("Filtered") : favourites ? t("Favourites") : collectionLabel(props);
  const browse = (
    <section className="cn-page cn-rowspage cn-browse browse">
      <div className="cn-pagehead">
        <CinemaCollections
          label={
            narrowed
              ? `${favourites ? `${t("Favourites")} · ` : ""}${t("Filtered")} · ${countLabel(props)}`
              : `${favourites ? t("Favourites") : collectionLabel(props)} · ${formatNumber(props.filtered.length)}`
          }
          index={favourites ? 1 : 0}
          count={props.tab === "Add-ons" ? 1 : 2}
          step={stepBrowse}
        />
        {/* One row of filters; the count lives in the collection line above. */}
        <div className="cn-filterline">{props.nodes.browseControls}</div>
      </div>
      {props.filtered.length && browseGame ? (
        <>
          <CinemaStage
            kicker={
              props.filters.query
                ? `${t("Search")} · ${countLabel(props)} · “${props.filters.query}”`
                : meta(browseGame)
            }
            title={browseGame.title}
            fixed
          >
            <p className="cn-tagline">{browseGame.tagline}</p>
          </CinemaStage>
          <div id="browse-results" className="cn-results">
            <CinemaRows
              sections={[
                {
                  key: "results",
                  label: browseLabel,
                  frames: cinemaFrames(props, browseList.visible),
                  grid: true,
                  total: browseList.total,
                  reach: browseList.reach,
                  more: browseList.more,
                },
              ]}
              current={{ section: "results", key: browseGame.id }}
              choose={(_, key) => props.setFocusedId(key)}
              open={(_, key) => {
                const game = props.filtered.find((g) => g.id === key);
                if (game) props.open(game);
              }}
            />
          </div>
        </>
      ) : (
        props.nodes.browseEmpty
      )}
    </section>
  );
  const stageGame = isBrowse(props) ? browseGame : homeGame;
  // Settings and empty screens show the last game looked at, blurred (Art behind menus).
  const backdropGame =
    props.game?.id ||
    (props.settings ? props.focused?.id : (props.tab === "Discover" || isBrowse(props)) && stageGame?.id) ||
    "";
  return (
    <div className={`iface-shell cn-shell cn-tab-${props.settings ? "settings" : props.game ? "game" : (props.tab === "Add-ons" ? "browse" : props.tab.toLowerCase())}`}>
      <Backdrop games={[...props.orbit.games,...props.orbit.addons]} active={backdropGame} settle={!props.game} preferPublisherHero />
      <div className="cn-veil" aria-hidden="true" />
      <header className="cn-head">
        <span className="cn-wordmark" aria-hidden="true">
          <img src="/orbit.svg" alt="" />
          Orbit
        </span>
        <SectionNav props={props} className="cn-chapters" />
        <div className="cn-tools">
          {/* The search field opens from the icon and stays while there's a search. */}
          {isBrowse(props) && !props.game && !props.settings && (searching || props.filters.query) ? (
            props.nodes.browseSearch
          ) : (
            <button
              className="icon-button"
              aria-label={t("Search games")}
              data-command="search"
              onClick={() => {
                setSearching(true);
                props.openSearch();
              }}
            >
              <Glyph name="search" size={18} />
            </button>
          )}
          <button className="cn-capsule" onClick={() => props.go("Downloads")} aria-label={t("Downloads")}>
            <Ring fraction={dl.fraction} size={22} />
            <span>
              {dl.count
                ? tn(dl.count, "{count} downloading", "{count} downloading")
                : t("Nothing in progress")}
              {dl.speed > 0 && <span className="cn-speed"> · {speedLabel(dl.speed)}</span>}
            </span>
          </button>
        </div>
      </header>
      {props.nodes.notices}
      <Body props={props} home={home} browse={browse} />
      <Hints props={props} />
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Crossbar: categories across, games down                            */
/* ------------------------------------------------------------------ */

function Ribbons() {
  return (
    <svg className="xb-ribbons" viewBox="0 0 1440 900" preserveAspectRatio="none" aria-hidden="true">
      {[0, 1, 2, 3, 4].map((i) => (
        <path
          key={i}
          d={`M0 ${480 + i * 14} C 300 ${380 + i * 30}, 620 ${600 - i * 22}, 900 ${470 + i * 12} S 1300 ${420 + i * 26}, 1440 ${500 - i * 8}`}
          fill="none"
          stroke="currentColor"
          strokeOpacity={0.14 + i * 0.04}
          strokeWidth={1.2 + i * 0.5}
        />
      ))}
    </svg>
  );
}

function CrossbarPreview({ props, game }: { props: ShellProps; game: Game }) {
  const size = game.releases[0]?.sizeBytes;
  return (
    <aside className="xb-preview" aria-live="polite">
      <span className="xb-preview-art">
        <Artwork key={game.hero} src={game.hero} fallbackSrc={game.heroFallback || game.cover} alt="" decoding="async" referrerPolicy="no-referrer" />
      </span>
      {game.tagline && <h2>{game.tagline}</h2>}
      {game.description && <p className="xb-preview-text">{game.description}</p>}
      <div className="xb-chips">
        {[game.genre, game.releaseDate?.slice(0, 4), game.platform === "ps4" ? "PS4" : "PS5", size ? bytes(size) : ""]
          .filter(Boolean)
          .map((c) => (
            <span key={c as string}>{c}</span>
          ))}
      </div>
    </aside>
  );
}

// A game as a Crossbar row: the cover and name, larger with its buttons when focused.
function crossbarGameRow(props: ShellProps, game: Game, prefix: string, dated = false): XbRow {
  const state = props.collectionStates.get(game.id);
  const size = game.releases[0]?.sizeBytes;
  const platform = game.platform === "ps4" ? "PS4" : "PS5";
  // Latest releases: the platform and the date, as on the PS5 app.
  const line = game.addonId ? meta(game) :
    dated && game.releaseDate
      ? `${platform} · ${t("Released {date}", { date: formatDate(new Date(game.releaseDate), { dateStyle: "medium", timeZone: "UTC" }) })}`
      : [platform, game.genre, state?.label ? t(state.label) : size ? bytes(size) : ""].filter(Boolean).join(" · ");
  return {
    key: `${prefix}${game.id}`,
    activate: () => props.open(game),
    render: (focused) => (
      <>
        <span className="xb-row-art">
          <Artwork src={game.cover} fallbackSrc={game.coverFallback} alt="" decoding="async" referrerPolicy="no-referrer" />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{game.title}</span>
          <span className="xb-row-sub">{line}</span>
        </span>
        {!focused && props.favorites.has(game.id) && <Glyph name="heartFill" size={16} />}
      </>
    ),
    actions: <GameButtons props={props} game={game} className="xb-pill" view={false} />,
  };
}

function CrossbarShell(props: ShellProps) {
  const dl = useDownloadSummary(props.orbit);
  // Discover as the default has it: New on Orbit, then Latest releases and All
  // games, which open as folders.
  const [folder, setFolder] = useState<"latest" | "all" | null>(null);
  const [current, setCurrent] = useState<string | undefined>();
  const folderGames = folder === "latest" ? props.latestGames : folder === "all" ? props.discoverGames : [];
  const folderList = useGrowing(folderGames, folder || "");
  const browseList = useGrowing(props.filtered, JSON.stringify(props.filters));
  const folderRow = (id: "latest" | "all", label: string, sub: string): XbRow => ({
    key: `folder-${id}`,
    activate: () => {
      setFolder(id);
      setCurrent(undefined);
    },
    render: () => (
      <>
        <span className="xb-row-icon">
          <Glyph name="folder" size={22} />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{label}</span>
          <span className="xb-row-sub">{sub}</span>
        </span>
        <Glyph name="next" size={18} />
      </>
    ),
  });
  let rows: XbRow[];
  if (isBrowse(props)) {
    rows = browseList.visible.map((g) => crossbarGameRow(props, g, "b:"));
  } else if (folder) {
    rows = folderList.visible.map((g) => crossbarGameRow(props, g, `${folder}:`, folder === "latest"));
  } else {
    rows = [
      ...(props.newlyAdded.length ? [{ key: "label-new", label: t("New on Orbit"), render: () => null }] : []),
      ...props.newlyAdded.map((g) => crossbarGameRow(props, g, "n:")),
      ...(props.latestGames.length
        ? [folderRow("latest", t("Latest releases"), `${tn(props.latestGames.length, "{count} game", "{count} games")} · ${t("Newest first")}`)]
        : []),
      folderRow("all", t("All games"), tn(props.discoverGames.length, "{count} game", "{count} games")),
    ];
  }
  const currentRow = rows.find((r) => r.key === current) || rows.find((r) => !r.label);
  const previewId = currentRow?.key.includes(":") ? currentRow.key.slice(currentRow.key.indexOf(":") + 1) : undefined;
  // On a folder row the preview keeps the last game looked at, as on the PS5 app.
  const [lastPreview, setLastPreview] = useState<string>();
  const previewGame =
    [...props.orbit.games,...props.orbit.addons].find((g) => g.id === (previewId ?? lastPreview)) ||
    (isBrowse(props) ? browseList.visible[0] : folder ? folderList.visible[0] : props.newlyAdded[0] || props.latestGames[0]);
  const { setFocusedId } = props;
  useEffect(() => {
    if (previewGame) setFocusedId(previewGame.id);
  }, [previewGame, setFocusedId]);
  // Moving down near the end of what's mounted mounts the next 96.
  const reachList = isBrowse(props) ? browseList.reach : folderList.reach;
  useEffect(() => {
    reachList(previewId);
  }, [previewId, reachList]);
  useEffect(() => {
    if (previewId) setLastPreview(previewId);
  }, [previewId]);
  const list = (
    <div className="xb-split">
      <div className="xb-column">
        {folder && props.tab === "Discover" && (
          <p className="xb-breadcrumb">
            {t("Discover")} › {folder === "latest" ? t("Latest releases") : t("All games")}
          </p>
        )}
        <CrossbarList
          rows={rows}
          current={currentRow?.key}
          setCurrent={setCurrent}
          back={() => {
            if (!folder || props.tab !== "Discover") return false;
            setCurrent(`folder-${folder}`);
            setFolder(null);
            return true;
          }}
          ariaLabel={t(isBrowse(props) ? props.tab : "Discover")}
        />
      </div>
      {previewGame && <CrossbarPreview props={props} game={previewGame} />}
    </div>
  );
  const browse = (
    <section className="xb-browse browse">
      <div className="xb-browse-bar">
        {props.nodes.browseControls}
        {!props.filters.query && <span className="xb-filter-count">{countLabel(props)}</span>}
        {props.filters.query && (
          <p className="xb-search-summary">
            {props.tab === "Add-ons" ? `${countLabel(props)} · “${props.filters.query}”` : t("{count} games for “{query}” · titles and title IDs", {
              count: formatNumber(props.filtered.length),
              query: props.filters.query,
            })}
          </p>
        )}
      </div>
      {props.filtered.length ? list : props.nodes.browseEmpty}
    </section>
  );
  return (
    <div className={`iface-shell xb-shell xb-tab-${props.settings ? "settings" : props.game ? "game" : (props.tab === "Add-ons" ? "browse" : props.tab.toLowerCase())}`}>
      {/* The focused game's banner, heavily blurred and darkened, under the
          ribbons: the background takes its colours. */}
      <div className="xb-art" aria-hidden="true">
        <Backdrop
          games={[...props.orbit.games,...props.orbit.addons]}
          active={props.game?.id || (!props.settings && (props.tab === "Discover" || isBrowse(props)) ? previewGame?.id || "" : "")}
          settle={!props.game}
        />
      </div>
      <Ribbons />
      <header className="xb-head">
        <span className="xb-wordmark" aria-hidden="true">
          <img src="/orbit.svg" alt="" />
          Orbit
        </span>
        <SectionNav
          props={props}
          className="xb-bar"
          glyphs
          downloadsBadge={dl.count > 0 ? <span className="xb-badge">{formatNumber(dl.count)}</span> : null}
        />
        {/* Downloads show once, as the badge on Downloads (the design has no capsule). */}
        <div className="xb-tools" onFocusCapture={() => !isBrowse(props) && !props.game && !props.settings && props.go("Browse")}>
          {props.nodes.browseSearch}
        </div>
      </header>
      {props.nodes.notices}
      <Body props={props} home={list} browse={browse} />
      <Hints props={props} />
    </div>
  );
}
