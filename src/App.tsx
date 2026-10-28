import { addonTypes, addonCountLabel } from "./AddonInfo";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useOrbit } from "./useOrbit";
import { useController } from "./controller";
import { Backdrop, GameTile, Icon, Modal, StorageRows } from "./components";
import { GameRail, type RailMemory } from "./GameRail";
import { Details } from "./Details";
import { Downloads } from "./Downloads";
import { GameUpdates } from "./GameUpdates";
import { Library } from "./Library";
import { useLibrary } from "./useLibrary";
import { StorageOverview } from "./LibraryActions";
import { gameState, inLibrary } from "./collection";
import { Select } from "./Select";
import { BrowserNotice } from "./BrowserNotice";
import { UpdateNotice } from "./UpdateNotice";
import { NativeAppNotice } from "./NativeAppNotice";
import { AutoStart } from "./AutoStart";
import { Loader } from "./Loader";
import { DownloadRetrySettings } from "./DownloadRetrySettings";
import { DownloadConnections, DownloadLimit } from "./DownloadLimit";
import { NotificationSettings } from "./NotificationSetting";
import { DefaultDrive } from "./DefaultDrive";
import { OrbitZeroSetting } from "./OrbitZeroSetting";
import { ArchiveSetting } from "./ArchiveSetting";
import { PackageSetting } from "./PackageSetting";
import { Sources } from "./Sources";
import { Pairing } from "./Pairing";
import type { Game, LibraryTarget } from "./types";
import { browseGames, catalogueTags, newlyAddedGames, defaultFilters, CATALOG_STEP, TAGS, type BrowseFilters } from "./browse";
import { gameCategory, inDiscover } from "./categories";
import { SavedFilters } from "./SavedFilters";
import { useInfiniteList } from "./infiniteList";
import {
  parseBrowserHandoff,
  handoffGame,
  type BrowserSelection,
} from "./browserHandoff";
import { t, tn, formatNumber } from "./i18n";
import { useInterfacePrefs } from "./interfaces/choice";
import { KeepInterface } from "./interfaces/Appearance";
import { InterfaceNotice } from "./interfaces/InterfaceNotice";
import { FavoriteNotice } from "./FavoriteNotice";
import { InterfaceShell } from "./interfaces/Shell";
import { SettingsPage, type SettingsSection } from "./interfaces/SettingsPage";
import { readInterfacePrefs } from "./interfaces/choice";
import { Glyph } from "./interfaces/Glyph";
export default function App() {
  const orbit = useOrbit();
  const iface = useInterfacePrefs().prefs.name;
  const [handoffHash, setHandoffHash] = useState(() => window.location.hash);
  const [handoffReady, setHandoffReady] = useState("");
  const [handoff, setHandoff] = useState<BrowserSelection | null>(null);
  const [handoffError, setHandoffError] = useState("");
  useEffect(() => {
    const changed = () => {
      setHandoffReady("");
      setHandoffHash(window.location.hash);
    };
    window.addEventListener("hashchange", changed);
    return () => window.removeEventListener("hashchange", changed);
  }, []);
  useEffect(() => {
    if (!handoffHash.startsWith("#download?")) return;
    let current = true;
    void orbit.refresh().then(() => {
      if (current) setHandoffReady(handoffHash);
    });
    return () => {
      current = false;
    };
  }, [handoffHash, orbit.refresh]);
  const [tab, setTab] = useState<
      "Discover" | "Browse" | "Add-ons" | "Apps" | "Updates" | "DLC" | "Library" | "Downloads"
    >("Discover"),
    [gameId, setGameId] = useState<string | null>(() =>
      new URLSearchParams(window.location.search).get("game"),
    ),
    [focusedId, setFocusedId] = useState("");
  const [addonBase, setAddonBase] = useState("");
  const [focusedRail, setFocusedRail] = useState<"added" | "latest">("added");
  const selectAdded = useCallback((id: string) => {
    setFocusedRail("added");
    setFocusedId(id);
  }, []);
  const selectLatest = useCallback((id: string) => {
    setFocusedRail("latest");
    setFocusedId(id);
  }, []);
  useEffect(() => {
    if (
      !handoffHash.startsWith("#download?") ||
      handoffReady !== handoffHash ||
      orbit.error ||
      !orbit.ready ||
      !orbit.system?.paired ||
      !orbit.sources?.acknowledged
    )
      return;
    const selection = parseBrowserHandoff(handoffHash);
    const selected = selection && handoffGame(selection, [...orbit.games, ...orbit.addons]);
    setHandoffError(
      selected
        ? ""
        : t(
            "This download option is no longer available. Check your enabled sources, then choose a game and source in Browse.",
          ),
    );
    setHandoff(selected ? selection : null);
    setGameId(selected?.id || null);
    setTab("Browse");
    // Consumed once; polling and reloads must not override later user choices.
    window.history.replaceState(
      null,
      "",
      window.location.pathname + window.location.search,
    );
    setHandoffHash("");
    setHandoffReady("");
  }, [
    handoffHash,
    handoffReady,
    orbit.error,
    orbit.ready,
    orbit.system?.paired,
    orbit.sources?.acknowledged,
    orbit.games,
  ]);
  const [storageOpen, setStorageOpen] = useState(false),
    [notificationsOpen, setNotificationsOpen] = useState(false),
    [zeroOpen, setZeroOpen] = useState(false),
    [sourcesOpen, setSourcesOpen] = useState(false),
    [autoOpen, setAutoOpen] = useState(false),
    [pairOpen, setPairOpen] = useState(false),
    [savedOpen, setSavedOpen] = useState(false);
  const [settingsSection, setSettingsSection] = useState<"debrid" | undefined>();
  // Crossbar, Cinema and Panorama show settings as a page of sections.
  const [settingsPage, setSettingsPage] = useState<SettingsSection | null>(null);
  const settingsReturn = useRef<(() => void) | null>(null);
  const closeSettings = useCallback((restoreSelection = true) => {
    setAutoOpen(false);
    setSettingsPage(null);
    setSettingsSection(undefined);
    const onReturn = settingsReturn.current;
    settingsReturn.current = null;
    if (restoreSelection) onReturn?.();
  }, []);
  const [libraryTarget, setLibraryTarget] = useState<LibraryTarget | null>(
    null,
  );
  const [downloadTarget, setDownloadTarget] = useState<string | null>(null);
  const [heroFavoriteBusy, setHeroFavoriteBusy] = useState(false);
  const [heroFavoriteError, setHeroFavoriteError] = useState("");
  const library = useLibrary(
    !!orbit.system?.paired,
    tab === "Library" || storageOpen || settingsPage === "storage",
  );
  const collectionStates = useMemo(
    () =>
      new Map(
        [...orbit.games,...orbit.addons].map((g) => [
          g.id,
          gameState(
            g,
            orbit.jobs,
            library.stale ? [] : library.snapshot?.games || [],
          ),
        ]),
      ),
    [orbit.games, orbit.addons, orbit.jobs, library.snapshot?.games, library.stale],
  );
  const [gameFilters, setGameFilters] = useState<BrowseFilters>(defaultFilters);
  const [addonFilters, setAddonFilters] = useState<BrowseFilters>({...defaultFilters, sort: "added"});
  const addonsPage = tab === "Add-ons";
  const appsPage = tab === "Apps";
  // Apps, DLC and update packs from the NAS folders never sit among the games. Discover shows games
  // and apps; DLC and update packs only appear in their own list.
  const storeGames = useMemo(() => orbit.games.filter((g) => inDiscover(g)), [orbit.games]);
  const regularGames = useMemo(() => orbit.games.filter((g) => gameCategory(g) === "game"), [orbit.games]);
  const appGames = useMemo(() => orbit.games.filter((g) => gameCategory(g) === "apps"), [orbit.games]);
  // Updates shows only while an installed game has an update waiting, DLC only while the NAS has a DLC
  // an installed game lacks, each with its number beside its name; both go away once all is done.
  const gameUpdates = orbit.gameUpdates;
  const gameDlc = orbit.gameDlc;
  useEffect(() => {
    if ((tab === "DLC" && gameDlc <= 0) || (tab === "Updates" && gameUpdates <= 0)) setTab("Browse");
    if (tab === "Apps" && orbit.games.length && !appGames.length) setTab("Browse");
  }, [tab, gameUpdates, gameDlc, appGames.length, orbit.games.length]);
  const filters = addonsPage ? addonFilters : gameFilters;
  const setFilters = addonsPage ? setAddonFilters : setGameFilters;
  const catalogueGames = addonsPage ? orbit.addons : appsPage ? appGames : regularGames;
  const gameTags = useMemo(() => catalogueTags(orbit.games), [orbit.games]);
  const sourceNames = useMemo(
    () => new Map(catalogueGames.flatMap((g) => g.releases.map((r) => [r.sourceId, r.provider] as const))),
    [catalogueGames],
  );
  const savedCount = orbit.savedFilters.filter((s) => s.page === (addonsPage ? "addons" : "games")).length;
  const defaultSort = addonsPage ? "added" : "release";
  const catalogueCount = (count: number) => addonsPage ? addonCountLabel(count) : tn(count, "{count} game", "{count} games");
  // The other interfaces' designs show the filters open under "Filters and sort".
  const [filtersOpen, setFiltersOpen] = useState(() => readInterfacePrefs().name !== "store");
  const [updateAvailable, setUpdateAvailable] = useState(false);
  const query = filters.query;
  const game = [...orbit.games, ...orbit.addons].find((entry) => entry.id === gameId);
  const detailsOpen = !!game;
  useEffect(() => {
    // Details have denser controls than the storefront. Keep their scale local
    // to this screen, including combobox menus rendered in a portal.
    document.documentElement.classList.toggle("details-open", detailsOpen);
    return () => document.documentElement.classList.remove("details-open");
  }, [detailsOpen]);
  const search = useRef<HTMLInputElement>(null),
    returnTo = useRef("");
  const rails = useRef<Record<string, RailMemory>>({
    latest: { left: 0, focusId: "", height: 0 },
    added: { left: 0, focusId: "", height: 0 },
  });
  const favorites = useMemo(() => new Set(orbit.favorites), [orbit.favorites]);
  function filter<K extends keyof BrowseFilters>(
    key: K,
    value: BrowseFilters[K],
  ) {
    setFilters((current) => ({ ...current, [key]: value }));
  }
  // A multiple-choice filter: the option ticked or unticked; "" (All …) clears it.
  function toggle(key: "source" | "format" | "tags", value: string) {
    setFilters((current) => ({
      ...current,
      [key]: !value ? [] : current[key].includes(value) ? current[key].filter((v) => v !== value) : [...current[key], value],
    }));
  }
  function resetFilters() {
    setFilters({...defaultFilters, sort: defaultSort});
    if (addonsPage) setAddonBase("");
  }
  const back = useCallback(() => {
    if (pairOpen) setPairOpen(false);
    else if (savedOpen) setSavedOpen(false);
    else if (sourcesOpen) setSourcesOpen(false);
    else if (storageOpen) setStorageOpen(false);
    else if (notificationsOpen) setNotificationsOpen(false);
    else if (zeroOpen) setZeroOpen(false);
    else if (autoOpen || settingsPage) closeSettings();
    else if (game) setGameId(null);
    else if (tab === "Add-ons" && addonBase) { setGameId(addonBase); setTab("Browse"); setAddonBase(""); }
    else if (tab !== "Discover") {
      setTab("Discover");
      requestAnimationFrame(() => {
        const first = document.querySelector<HTMLElement>(".hero .primary");
        first?.focus({ preventScroll: true });
      });
    }
  }, [game, tab, addonBase, storageOpen, notificationsOpen, zeroOpen, autoOpen, settingsPage, pairOpen, savedOpen, sourcesOpen, closeSettings]);
  const consoleBrowser =
    orbit.system?.platform === "ps5" && !!orbit.system.localSessionAvailable;
  useController(back, consoleBrowser);
  // Games in the PS5's Library, for Hide installed games. Like the TV app, the
  // last scan still counts while the Library rescans.
  const installedGames = useMemo(() => {
    const games = library.snapshot?.status === "ready" ? library.snapshot.games : [];
    return new Set(games.length ? orbit.games.filter((g) => inLibrary(g, games)).map((g) => g.id) : []);
  }, [orbit.games, library.snapshot?.status, library.snapshot?.games]);
  const filtered = useMemo(() => {
    const list = browseGames(catalogueGames.filter(g => !addonsPage || !addonBase || g.baseGameId === addonBase), filters, favorites, installedGames);
    // Add-ons stay together under their game, in the game's alphabetical order; the chosen sort orders them within it.
    if (!addonsPage) return list;
    const key = (g: { baseGameTitle?: string; title: string }) => (g.baseGameTitle || g.title).toLowerCase();
    return [...list].sort((a, b) => key(a) < key(b) ? -1 : key(a) > key(b) ? 1 : 0);
  }, [catalogueGames, addonsPage, addonBase, filters, favorites, installedGames]);
  const browseReset = useMemo(() => ({filters, addonBase, tab}), [filters, addonBase, tab]);
  const browseList = useInfiniteList(filtered.length, CATALOG_STEP, browseReset);
  const visibleGames = filtered.slice(0, browseList.visible);
  const discoverGames = useMemo(
    () =>
      browseGames(
        storeGames,
        { ...defaultFilters, sort: "size-desc" },
        new Set(),
      ),
    [storeGames],
  );
  const discoverList = useInfiniteList(discoverGames.length, CATALOG_STEP);
  const discoverVisible = discoverGames.slice(0, discoverList.visible);
  const latestGames = useMemo(
    () =>
      storeGames
        .filter(
          (g) =>
            g.releaseDate &&
            g.releaseDate <= new Date().toISOString().slice(0, 10),
        )
        .sort(
          (a, b) =>
            b.releaseDate!.localeCompare(a.releaseDate!) ||
            a.title.localeCompare(b.title),
        )
        .slice(0, 20),
    [storeGames],
  );
  const newlyAdded = useMemo(() => newlyAddedGames(storeGames), [storeGames]);
  const active = orbit.jobs.filter((j) =>
    ["queued", "downloading", "retrying", "verifying", "inspecting", "extracting"].includes(j.status),
  ).length;
  // Start with the first rail so the hero and highlighted tile agree.
  const focused =
    catalogueGames.find((g) => g.id === focusedId) ||
    (addonsPage ? filtered[0] : undefined) ||
    newlyAdded[0] ||
    latestGames[0] ||
    orbit.games[0];
  const firstCovers = window.innerWidth < 700 ? 3 : 6;
  const discover = tab === "Discover" && !game;
  useEffect(() => {
    if (
      orbit.games.length &&
      document.activeElement === document.body &&
      !document.querySelector('[role="dialog"]')
    )
      document
        .querySelector<HTMLElement>(".browse-grid .game-tile, .hero .primary")
        ?.focus({ preventScroll: true });
  }, [orbit.games.length]);
  useEffect(() => {
    if (orbit.ready && gameId && !game) setGameId(null);
  }, [gameId, game, orbit.ready]);
  useEffect(() => {
    // Returning from a game page puts focus back on the tile that opened it.
    if (game || !returnTo.current) return;
    document
      .querySelector<HTMLElement>(returnTo.current)
      ?.focus({ preventScroll: true });
    returnTo.current = "";
  }, [game]);
  const open = useCallback((g: Game) => {
    const row = document.activeElement
      ?.closest("[data-row]")
      ?.getAttribute("data-row");
    returnTo.current = `${row ? `[data-row="${row}"] ` : ""}[data-game="${g.id}"]`;
    setHandoff(null);
    setGameId(g.id);
  }, []);
  function go(next: typeof tab) {
    setHandoff(null);
    setGameId(null);
    setSettingsPage(null);
    setTab(next);
    setDownloadTarget(null);
    setLibraryTarget(null);
    if (next === "Browse" || next === "Add-ons" || next === "Apps")
      requestAnimationFrame(() => {
        const first = document.querySelector<HTMLElement>(
          ".browse-grid .game-tile",
        );
        (first || search.current)?.focus({ preventScroll: true });
      });
  }
  function openLibrary(target: Omit<LibraryTarget, "request">) {
    go("Library");
    setStorageOpen(false);
    setLibraryTarget({ ...target, request: Date.now() });
  }
  function openDownload(id?: string) {
    go("Downloads");
    setDownloadTarget(id || null);
  }
  function showSearch() {
    go(addonsPage ? "Add-ons" : appsPage ? "Apps" : "Browse");
    requestAnimationFrame(() => search.current?.focus());
  }
  function paired(action: () => void) {
    return () => (orbit.system?.paired ? action() : setPairOpen(true));
  }
  // Pieces every interface draws the same way: the notices, the screens
  // with their own state (a game, Library, Downloads) and today's Browse
  // controls. Each interface only arranges them.
  const noticesNode = (
    <>
      <NativeAppNotice
        version={orbit.system?.version}
        hidden={autoOpen || pairOpen || sourcesOpen || storageOpen || notificationsOpen || zeroOpen}
      />
      <UpdateNotice
        enabled={!!orbit.system?.paired && orbit.system.platform !== "desktop"}
        hidden={autoOpen || pairOpen || sourcesOpen || storageOpen || notificationsOpen || zeroOpen}
        onViewUpdate={() => setAutoOpen(true)}
        onAvailable={setUpdateAvailable}
      />
      {orbit.error && (
        <div className="connection-error" role="status">
          {orbit.error}
          <button onClick={() => location.reload()}>{t("Reconnect")}</button>
        </div>
      )}
      {orbit.system && !orbit.system.stateHealthy && (
        <div className="connection-error" role="alert">
          {t(
            "Orbit cannot save its queue. Check free space on the console’s internal storage.",
          )}
        </div>
      )}
      <BrowserNotice
        session={orbit.browser}
        cancel={orbit.cancelProvider}
        openQueue={openDownload}
        hidden={autoOpen || pairOpen || sourcesOpen || storageOpen || notificationsOpen || zeroOpen}
      />
    </>
  );
  const handoffNoticesNode = (
    <>
      {handoffError && (
        <div className="notice" role="alert">
          {handoffError}
          <button onClick={() => setHandoffError("")}>{t("Dismiss")}</button>
        </div>
      )}
      {handoffHash.startsWith("#download?") &&
        orbit.ready &&
        !orbit.system?.paired && (
          <div className="notice" role="status">
            {t(
              "Pair with your PS5 to continue with the game selected in the app.",
            )}
            <button onClick={() => setPairOpen(true)}>
              {t("Pair with your PS5")}
            </button>
          </div>
        )}
    </>
  );
  const sourceSetupNode =
    tab !== "Downloads" &&
    tab !== "Library" &&
    orbit.sources &&
    !orbit.sources.acknowledged ? (
      <section className="page source-setup">
        <Sources
          settings={orbit.sources}
          paired={!!orbit.system?.paired}
          pair={() => setPairOpen(true)}
          save={orbit.saveSources}
        />
      </section>
    ) : null;
  const detailsNode = game ? (
    <Details
      key={`${game.id}:${handoff?.releaseId || ""}:${handoff?.storageId || ""}`}
      game={game}
      addonCount={orbit.addons.filter(a => a.baseGameId === game.id).length}
      openAddons={() => { setAddonFilters({...defaultFilters, sort: "added"}); setAddonBase(game.id); go("Add-ons"); }}
      back={back}
      initialSelection={handoff?.gameId === game.id ? handoff : undefined}
      drives={orbit.storage}
      system={orbit.system}
      jobs={orbit.jobs}
      pair={() => setPairOpen(true)}
      download={orbit.download}
      browser={orbit.browser}
      openProvider={orbit.openProvider}
      openQueue={openDownload}
      library={library.stale ? [] : library.snapshot?.games || []}
      openLibrary={openLibrary}
      favorite={favorites.has(game.id)}
      setFavorite={(value) => orbit.favorite(game.id, value)}
      configureDebrid={(onReturn) => {
        settingsReturn.current = onReturn;
        if (iface === "store") {
          setSettingsSection("debrid");
          setAutoOpen(true);
        } else setSettingsPage("torbox");
      }}
      section={t(tab)}
    />
  ) : null;
  const browseSearchNode = (
    <label className="search-field">
      <Icon name="search" />
      <input
        ref={search}
        type="search"
        placeholder={t(addonsPage ? /* i18n */ "Search add-ons or base games" : "Search games or title ID")}
        aria-label={t(addonsPage ? /* i18n */ "Search add-ons or base games" : "Search games or title ID")}
        aria-controls="browse-results"
        value={query}
        onChange={(e) => {
          filter("query", e.target.value);
        }}
      />
    </label>
  );
  const browseControlsNode = (
    <div className="browse-controls">
      {addonsPage ? <div className="collection-tabs" role="group" aria-label={t("Add-ons")}>
        <button aria-pressed={!addonBase} onClick={()=>setAddonBase("")}>{t("All add-ons")}</button>
        {addonBase && <span className="browse-base-name">{orbit.games.find(g=>g.id===addonBase)?.title}</span>}
      </div> : (
      <div
        className="collection-tabs"
        role="group"
        aria-label={t("Browse collection")}
      >
        <button
          aria-pressed={filters.collection === "all"}
          onClick={() => filter("collection", "all")}
        >
          {t("All games")}
        </button>
        <button
          aria-pressed={filters.collection === "favorites"}
          onClick={() =>
            orbit.system?.paired
              ? filter("collection", "favorites")
              : setPairOpen(true)
          }
        >
          {t("Favourites")}
        </button>
      </div>
      )}
      <button
        className="filter-toggle"
        aria-expanded={filtersOpen}
        aria-controls="browse-filters"
        onClick={() => setFiltersOpen((value) => !value)}
      >
        {t("Filters and sort")}
        {filters.source.length ||
        filters.format.length ||
        filters.tags.length ||
        filters.hideInstalled ||
        filters.region ||
        filters.platform ||
        filters.size ||
        filters.addonType || filters.sort !== defaultSort
          ? ` · ${t("Applied")}`
          : ""}{" "}
        {filtersOpen ? "▴" : "▾"}
      </button>
      <div
        id="browse-filters"
        className={`browse-selects ${filtersOpen ? "expanded" : ""}`}
      >
        <Select
          inlineLabel={iface !== "store"}
          label={t("Platform")}
          value={filters.platform}
          onChange={(value) => filter("platform", value)}
          options={[
            { value: "", label: t("All platforms") },
            { value: "ps5", label: "PS5" },
            { value: "ps4", label: "PS4" },
          ]}
        />
        <Select
          inlineLabel={iface !== "store"}
          label={t("Source")}
          value=""
          values={filters.source}
          onChange={(value) => toggle("source", value)}
          options={[
            { value: "", label: t("All sources") },
            ...Array.from(sourceNames).map(([value, label]) => ({ value, label })),
          ]}
        />
        {addonsPage ? <Select inlineLabel={iface !== "store"} label={t("Type")} value={filters.addonType || ""}
          onChange={value=>filter("addonType",value)} options={[{value:"",label:t("All types")},...Object.entries(addonTypes).map(([value,label])=>({value,label:t(label)}))]} /> : (
        <Select
          inlineLabel={iface !== "store"}
          label={t("Format")}
          value=""
          values={filters.format}
          onChange={(value) => toggle("format", value)}
          options={[
            { value: "", label: t("All formats") },
            ...Array.from(
              new Set(
                catalogueGames.flatMap((g) =>
                  g.releases.flatMap((r) => [r.format !== "Unknown" ? r.format : "", r.compression ? (r.compression === "7z" ? "7z" : r.compression.toUpperCase()) : ""].filter(Boolean)),
                ),
              ),
            ).map((value) => ({ value, label: value })),
          ]}
        />
        )}
        {!addonsPage && gameTags.length > 0 && (
          <Select
            inlineLabel={iface !== "store"}
            label={t("Tags")}
            value=""
            values={filters.tags}
            onChange={(value) => toggle("tags", value)}
            options={[{ value: "", label: t("All tags") }, ...gameTags.map((tag) => ({ value: tag, label: t(TAGS[tag]) }))]}
          />
        )}
        {!addonsPage && orbit.system?.paired && (
          <Select
            inlineLabel={iface !== "store"}
            label={t("Installed games")}
            value={filters.hideInstalled ? "hide" : ""}
            onChange={(value) => filter("hideInstalled", value === "hide")}
            options={[
              { value: "", label: t("Show installed") },
              { value: "hide", label: t("Hide installed") },
            ]}
          />
        )}
        <Select
          inlineLabel={iface !== "store"}
          label={t("Region")}
          value={filters.region}
          onChange={(value) => filter("region", value)}
          options={[
            { value: "", label: t("All regions") },
            ...["EUR", "USA", "JPN", "ASIA"].map((value) => ({ value, label: value })),
            { value: "unknown", label: t("Unknown region") },
          ]}
        />
        <Select
          inlineLabel={iface !== "store"}
          label={t("Download size")}
          value={filters.size}
          onChange={(value) => filter("size", value)}
          options={[
            { value: "", label: t("Any size") },
            { value: "small", label: t("Under 10 GB") },
            { value: "medium", label: t("10–50 GB") },
            { value: "large", label: t("50–100 GB") },
            { value: "huge", label: t("100 GB or more") },
          ]}
        />
        <Select
          inlineLabel={iface !== "store"}
          label={t("Sort by")}
          value={filters.sort}
          onChange={(value) => filter("sort", value)}
          options={[
            { value: defaultSort, label: t(addonsPage ? "Recently added" : "Newest first") },
            { value: "title", label: t("Title A–Z") },
            { value: "title-desc", label: t("Title Z–A") },
            { value: "size", label: t("Smallest download") },
            { value: "size-desc", label: t("Largest download") },
          ]}
        />
        {(filters.source.length ||
          filters.format.length ||
          filters.tags.length ||
          filters.hideInstalled ||
          filters.region ||
          filters.platform ||
          filters.size ||
          query ||
          filters.collection !== "all" ||
          filters.addonType || filters.sort !== defaultSort) && (
          <button className="reset-filters" onClick={resetFilters}>
            {iface !== "store" && <Glyph name="refresh" size={14} stroke={2} />}
            {t("Reset filters")}
          </button>
        )}
        <button
          className="saved-filters-open"
          onClick={() => (orbit.system?.paired ? setSavedOpen(true) : setPairOpen(true))}
        >
          {iface !== "store" && <Glyph name="star" size={14} stroke={2} />}
          {t("Saved filters")}
          {savedCount ? ` · ${formatNumber(savedCount)}` : ""}
        </button>
      </div>
      {(filters.sort === "size" || filters.sort === "size-desc") && (
        <p className="fine">
          {t(
            "Sorted by the smallest download option that matches your filters.",
          )}
        </p>
      )}
    </div>
  );
  const browseEmptyNode = (
    <div className="empty compact">
      <h2>{t(addonsPage ? /* i18n */ "No add-ons match these filters." : "No games found")}</h2>
      <p>
        {catalogueGames.length
          ? filters.collection === "favorites"
            ? t(
                "Save a game from its details page, or adjust your filters.",
              )
            : t("Try another title, title ID or filter.")
          : t("Your selected sources have no games in this build.")}
      </p>
      <button
        onClick={() => {
          if (!catalogueGames.length) setSourcesOpen(true);
          else {
            resetFilters();
          }
        }}
      >
        {catalogueGames.length
          ? t("Reset filters")
          : t("Choose sources")}
      </button>
    </div>
  );
  const libraryNode = (
    <Library
      model={library}
      target={libraryTarget}
      paired={!!orbit.system?.paired}
      pair={() => setPairOpen(true)}
      drives={orbit.storage}
      consoleBrowser={consoleBrowser}
    />
  );
  const downloadsNode = !orbit.system?.paired ? (
    <section className="page">
      <h1>{t("Downloads")}</h1>
      <div className="empty">
        <Icon name="download" />
        <h2>{t("See your PS5’s downloads")}</h2>
        <p>{t("Pair this device using the code shown on your PS5.")}</p>
        <button className="primary" onClick={() => setPairOpen(true)}>
          {t("Pair with your PS5")}
        </button>
      </div>
    </section>
  ) : (
    <Downloads
      jobs={orbit.jobs}
      games={orbit.games}
      drives={orbit.storage}
      action={orbit.action}
      clearHistory={orbit.clearHistory}
      browse={() => go("Browse")}
      targetId={downloadTarget}
      openLibrary={(job) => openLibrary({ titleId: job.titleId, path: job.path, title: job.title })}
      concurrent={orbit.system?.paired ? orbit.system.concurrentDownloads : undefined}
      deletePackages={orbit.system?.deletePackages === true}
    />
  );
  // Before games arrive (opening, unreachable, no sources), in every interface.
  const discoverEmptyNode = (
    <div className="empty">
      {orbit.sources?.acknowledged ? (
        <Icon name="sources" />
      ) : (
        <Loader />
      )}
      <h1>
        {orbit.error
          ? t("Orbit isn’t reachable")
          : orbit.sources?.acknowledged
            ? orbit.sources.enabled.length
              ? t("No games from your selected sources")
              : t("No sources enabled")
            : t("Opening Orbit Store…")}
      </h1>
      {orbit.error ? (
        <p>
          {t(
            "Check that Orbit is running on your PS5, then reconnect.",
          )}
        </p>
      ) : (
        orbit.sources?.acknowledged && (
          <>
            <p>
              {orbit.sources.enabled.length
                ? t(
                    "There are no compatible releases from these sources in this build. Choose another source to browse.",
                  )
                : t("Choose a download source to see its games.")}
            </p>
            <button
              className="primary"
              onClick={() => setSourcesOpen(true)}
            >
              {t("Choose sources")}
            </button>
          </>
        )
      )}
    </div>
  );
  // Storage and downloads, the same in Store's dialog and the settings page.
  const storagePanel = (
    <>
          <StorageOverview
            feedback
            model={library}
            openGame={(g) =>
              openLibrary({ titleId: g.titleId, path: g.path, title: g.title })
            }
          />
          <h2 className="download-storage-heading">
            {t("Download destinations")}
          </h2>
          <p>
            {t("Downloads save inside the selected drive’s homebrew folder.")}
          </p>
          <StorageRows drives={orbit.storage} />
          {orbit.system?.paired && (
            <DefaultDrive drives={orbit.storage} preferred={orbit.system.preferredStorage} refresh={() => orbit.refresh()} />
          )}
          {orbit.system && <DownloadLimit value={orbit.system.concurrentDownloads ?? 1} refresh={() => orbit.refresh()} />}
          {typeof orbit.system?.downloadConnections === "number" && (
            <DownloadConnections value={orbit.system.downloadConnections} refresh={() => orbit.refresh()} />
          )}
          {orbit.system && <DownloadRetrySettings settings={orbit.system} refresh={() => orbit.refresh()} />}
          {orbit.system && (
            <ArchiveSetting
              enabled={orbit.system.deleteArchives !== false}
              refresh={() => orbit.refresh()}
            />
          )}
          {typeof orbit.system?.deletePackages === "boolean" && (
            <PackageSetting
              enabled={orbit.system.deletePackages}
              refresh={() => orbit.refresh()}
            />
          )}
          <div className="dialog-actions">
            <button
              onClick={() => {
                setStorageOpen(false);
                setPairOpen(true);
              }}
            >
              {t("Pair another device")}
            </button>
          </div>
    </>
  );
  // Orbit Zero, the same in Store's dialog and the settings page.
  const zeroPanel = orbit.system?.paired ? <OrbitZeroSetting onConsole={consoleBrowser} /> : null;
  // Notifications, the same in Store's dialog and the settings page.
  const notificationsPanel = orbit.system?.notificationEvents ? (
    <NotificationSettings system={orbit.system} refresh={() => orbit.refresh()} />
  ) : null;
  const settingsNode = settingsPage ? (
    <SettingsPage
      section={settingsPage}
      choose={setSettingsPage}
      system={orbit.system}
      updateAvailable={updateAvailable}
      sourceSettings={orbit.sources}
      sources={
        orbit.sources ? (
          <Sources
            rows
            settings={orbit.sources}
            paired={!!orbit.system?.paired}
            pair={() => setSettingsPage("pair")}
            save={orbit.saveSources}
            saved={() => go("Browse")}
          />
        ) : null
      }
      storage={storagePanel}
      notifications={notificationsPanel}
      zero={zeroPanel}
      pairing={<Pairing inline system={orbit.system} pair={orbit.pair} close={() => closeSettings()} />}
    />
  ) : null;
  return (
    <div
      className={`app screen-${game ? "details" : tab === "Apps" ? "browse" : tab.toLowerCase()} ${discover ? "discover" : ""} ${consoleBrowser ? "console-browser" : ""} iface-${iface}`}
      data-game={game?.id}
    >
      {iface !== "store" ? (
        <InterfaceShell
          iface={iface}
          orbit={orbit}
          tab={tab === "Updates" || tab === "DLC" || tab === "Apps" ? "Browse" : tab}
          go={go}
          game={game}
          back={back}
          open={open}
          focused={focused}
          setFocusedId={setFocusedId}
          newlyAdded={newlyAdded}
          latestGames={latestGames}
          discoverGames={discoverGames}
          filtered={filtered}
          filters={filters}
          filter={filter}
          favorites={favorites}
          collectionStates={collectionStates}
          openDownload={openDownload}
          openLibrary={(g) => openLibrary({ titleId: g.titleId, path: g.path, title: g.title })}
          active={active}
          paired={paired}
          openSettings={paired(() => setSettingsPage("appearance"))}
          settings={settingsNode}
          updateAvailable={updateAvailable}
          openSearch={showSearch}
          consoleBrowser={consoleBrowser}
          nodes={{
            notices: noticesNode,
            handoffNotices: handoffNoticesNode,
            sourceSetup: sourceSetupNode,
            discoverEmpty: discoverEmptyNode,
            details: detailsNode,
            browseSearch: browseSearchNode,
            browseControls: browseControlsNode,
            browseEmpty: browseEmptyNode,
            library: libraryNode,
            downloads: downloadsNode,
          }}
        />
      ) : (
        <>
      <InterfaceNotice openSettings={paired(() => setAutoOpen(true))} />
      <Backdrop
        games={[...orbit.games, ...orbit.addons]}
        active={game?.id || (discover && focused?.id) || ""}
        settle={discover}
        preferPublisherHero={!!game}
      />
      <header className="header">
        {game && (
          <button
            className="top-icon details-back"
            data-label={t("Back")}
            aria-label={t("Back")}
            onClick={back}
          >
            <Icon name="back" />
            <span>{t("Back")}</span>
          </button>
        )}
        <button
          className="brand"
          onClick={() => go("Discover")}
          aria-label={t("Orbit Store home")}
        >
          <img src="/orbit.svg" alt="" />
          Orbit Store
          <span className="beta-label">{t("Beta")}</span>
        </button>
        {/* A game page is its own screen: no tabs or tools, Circle or Back returns. */}
        {!game && (
          <>
            <nav className="main-nav" aria-label={t("Main navigation")}>
              {(["Discover", "Browse", "Apps", "Add-ons", "Updates", "Library", "Downloads"] as const)
                .filter((name) => name !== "Apps" || appGames.length > 0)
                .filter((name) => name !== "Updates" || gameUpdates > 0)
                .map(
                (name) => (
                  <button
                    key={name}
                    className={tab === name ? "active" : ""}
                    aria-current={tab === name ? "page" : undefined}
                    onClick={() => go(name)}
                  >
                    {t(name)}
                    {name === "Downloads" && active > 0 && (
                      <span className="queue-count">{active}</span>
                    )}
                    {name === "Updates" && gameUpdates > 0 && (
                      <span className="queue-count">{gameUpdates}</span>
                    )}
                  </button>
                ),
              )}
            </nav>
            <div className="utilities">
              <button
                className="top-icon"
                data-label={t("Search")}
                data-command="search"
                aria-label={t("Search games")}
                onClick={showSearch}
              >
                <Icon name="search" />
              </button>
              <button
                className="top-icon"
                data-label={t("App settings")}
                aria-label={t("App settings")}
                onClick={paired(() => setAutoOpen(true))}
              >
                <Icon name="settings" />
              </button>
            </div>
          </>
        )}
      </header>
      {noticesNode}
      <main>
        {handoffNoticesNode}
        {sourceSetupNode ? (
          sourceSetupNode
        ) : game ? (
          detailsNode
        ) : tab === "Discover" ? (
          focused ? (
            <>
              <section className="hero" aria-live="polite">
                <p className="hero-eyebrow">{t("Discover")}</p>
                <h1>{focused.title}</h1>
                <p
                  className="tagline"
                  aria-hidden={!focused.tagline || undefined}
                >
                  {focused.tagline}
                </p>
                <div className="hero-actions">
                  <button className="primary" onClick={() => open(focused)}>
                    {t("View game")}
                  </button>
                  <button
                    className="hero-favorite icon-button"
                    aria-label={
                      favorites.has(focused.id)
                        ? t("Remove from favourites")
                        : t("Add to favourites")
                    }
                    aria-pressed={favorites.has(focused.id)}
                    disabled={heroFavoriteBusy}
                    onClick={paired(async () => {
                      setHeroFavoriteBusy(true);
                      setHeroFavoriteError("");
                      try {
                        await orbit.favorite(
                          focused.id,
                          !favorites.has(focused.id),
                        );
                      } catch (error) {
                        setHeroFavoriteError((error as Error).message);
                      } finally {
                        setHeroFavoriteBusy(false);
                      }
                    })}
                  >
                    <Icon name="heart" />
                  </button>
                </div>
                {heroFavoriteError && (
                  <p className="hero-action-error" role="alert">
                    {heroFavoriteError}
                  </p>
                )}
              </section>
              {newlyAdded.length > 0 && (
                <section className="collection" data-row="added" aria-labelledby="added-title">
                  <div className="section-heading">
                    <h2 id="added-title">{t("New on Orbit")}</h2>
                  </div>
                  <GameRail
                    games={newlyAdded}
                    focusedId={focusedRail === "added" ? focused.id : ""}
                    showDate={false}
                    firstCovers={firstCovers}
                    memory={rails.current.added}
                    states={collectionStates}
                    favorites={favorites}
                    open={open}
                    select={selectAdded}
                    instantScroll={consoleBrowser}
                  />
                </section>
              )}
              {/* Without release dates there is no Latest releases row, as in
                  the other versions and interfaces. */}
              {latestGames.length > 0 && (
                <section
                  className="collection"
                  data-row="latest"
                  aria-labelledby="latest-title"
                >
                  <div className="section-heading">
                    <h2 id="latest-title">{t("Latest releases")}</h2>
                  </div>
                  <GameRail
                    games={latestGames}
                    focusedId={focusedRail === "latest" ? focused.id : ""}
                    showDate={false}
                    firstCovers={newlyAdded.length ? 0 : firstCovers}
                    memory={rails.current.latest}
                    states={collectionStates}
                    favorites={favorites}
                    open={open}
                    select={selectLatest}
                    instantScroll={consoleBrowser}
                  />
                </section>
              )}
              <section
                className="collection discover-all"
                aria-labelledby="all-title"
              >
                <div className="section-heading">
                  <h2 id="all-title">{t("All games")}</h2>
                  <span>
                    {tn(discoverGames.length, "{count} game", "{count} games")}
                  </span>
                </div>
                <div className="browse-grid discover-grid" data-row="all">
                  {discoverVisible.map((g) => (
                    <GameTile
                      key={g.id}
                      game={g}
                      status={collectionStates.get(g.id)?.label}
                      favorite={favorites.has(g.id)}
                      open={open}
                      onFocus={setFocusedId}
                    />
                  ))}
                </div>
                <div ref={discoverList.end} aria-hidden="true" />
              </section>
            </>
          ) : (
            discoverEmptyNode
          )
        ) : tab === "Add-ons" || tab === "Browse" || tab === "Apps" ? (
          <section className="page browse">
            <div className="page-heading">
              <h1>{t(addonsPage ? "Add-ons" : appsPage ? "Apps" : "Browse")}</h1>
              <span>
                {catalogueCount(filtered.length)}
              </span>
              {browseSearchNode}
            </div>
            {browseControlsNode}
            {filtered.length ? (
              <>
                <div
                  id="browse-results"
                  className="game-rail browse-grid"
                  data-row="browse"
                >
                  {visibleGames.map((g, index) => (
                    <GameTile
                      key={g.id}
                      game={g}
                      status={collectionStates.get(g.id)?.label}
                      favorite={favorites.has(g.id)}
                      priority={index < firstCovers}
                      open={open}
                    />
                  ))}
                </div>
                <div ref={browseList.end} aria-hidden="true" />
              </>
            ) : (
              browseEmptyNode
            )}
          </section>
        ) : tab === "DLC" ? (
          <GameUpdates
            dlc
            games={orbit.gameDlcList}
            checking={orbit.gameChecking}
            check={orbit.checkGameUpdates}
            job={orbit.gameDlcJob}
            queue={orbit.gameQueue}
            catalogue={orbit.games}
            act={orbit.gameUpdateAction}
          />
        ) : tab === "Updates" ? (
          <GameUpdates
            games={orbit.gameUpdateList}
            checking={orbit.gameChecking}
            check={orbit.checkGameUpdates}
            job={orbit.gameJob}
            queue={orbit.gameQueue}
            catalogue={orbit.games}
            act={orbit.gameUpdateAction}
          />
        ) : tab === "Library" ? (
          libraryNode
        ) : (
          downloadsNode
        )}
      </main>
      <footer>
        {orbit.system?.platform === "fixture" && (
          <span>{t("Local test fixture. No console connected.")}</span>
        )}
        <div className="controller-hints">
          <span>
            <b>×</b>
            {!game && tab !== "Downloads" && tab !== "Library" ? t("Open game") : t("Select")}
          </span>
          {!game && (tab === "Browse" || addonsPage || appsPage) && (
            <button onClick={showSearch}>
              <b>△</b>
              {t("Search")}
            </button>
          )}
          {!game && (
            <span>
              <kbd>L1</kbd>
              <kbd>R1</kbd>
              {t("Tabs")}
            </span>
          )}
          {game && (
            <button onClick={back}>
              <b>○</b>
              {t("Back")}
            </button>
          )}
        </div>
      </footer>
        </>
      )}
      {storageOpen && (
        <Modal
          title={t("Storage")}
          close={() => setStorageOpen(false)}
          className="storage-modal"
        >
          <h1>{t("Storage")}</h1>
          {storagePanel}
        </Modal>
      )}
      {zeroOpen && (
        <Modal title="Orbit Zero" close={() => setZeroOpen(false)} className="storage-modal orbit-zero-modal">
          {zeroPanel}
        </Modal>
      )}
      {notificationsOpen && (
        <Modal
          title={t("Notifications")}
          close={() => setNotificationsOpen(false)}
          className="storage-modal notifications-modal"
        >
          <h1>{t("Notifications")}</h1>
          {notificationsPanel}
        </Modal>
      )}
      {autoOpen && (
        <AutoStart system={orbit.system} close={() => closeSettings()} initialSection={settingsSection}>
          <div className="settings-shortcuts">
            <button
              onClick={() => {
                closeSettings(false);
                setPairOpen(true);
              }}
            >
              {t("Pair devices")}
            </button>
            <button
              onClick={() => {
                closeSettings(false);
                setSourcesOpen(true);
              }}
            >
              {t("Sources")}
            </button>
            <button
              onClick={() => {
                closeSettings(false);
                setStorageOpen(true);
              }}
            >
              {t("Storage")}
            </button>
            {notificationsPanel && (
              <button
                onClick={() => {
                  closeSettings(false);
                  setNotificationsOpen(true);
                }}
              >
                {t("Notifications")}
              </button>
            )}
            {zeroPanel && (
              <button
                onClick={() => {
                  closeSettings(false);
                  setZeroOpen(true);
                }}
              >
                Orbit Zero
              </button>
            )}
          </div>
        </AutoStart>
      )}
      {sourcesOpen && orbit.sources && (
        <Modal
          title={t("Download sources")}
          close={() => setSourcesOpen(false)}
          className="sources-modal"
        >
          <Sources
            settings={orbit.sources}
            paired={!!orbit.system?.paired}
            pair={() => {
              setSourcesOpen(false);
              setPairOpen(true);
            }}
            save={orbit.saveSources}
            saved={() => {
              setSourcesOpen(false);
              go("Browse");
            }}
          />
        </Modal>
      )}
      {savedOpen && (
        <SavedFilters
          page={addonsPage ? "addons" : "games"}
          filters={filters}
          defaultSort={defaultSort}
          saved={orbit.savedFilters}
          sources={sourceNames}
          change={orbit.changeSavedFilters}
          apply={(entry) => {
            setFilters({ ...defaultFilters, ...entry.filters, addonType: entry.filters.addonType || undefined });
            setSavedOpen(false);
          }}
          close={() => setSavedOpen(false)}
        />
      )}
      {pairOpen && (
        <Pairing
          system={orbit.system}
          pair={orbit.pair}
          close={() => setPairOpen(false)}
        />
      )}
      <FavoriteNotice notice={orbit.favoriteNotice} />
      <KeepInterface />
    </div>
  );
}
