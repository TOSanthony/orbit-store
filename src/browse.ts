import type { Game } from "./types";
import { gamePlatform } from "./platform";
// The tags Browse can filter by, as the catalogue names them (tools/catalog.py
// KNOWN_TAGS). Tags this build has no label for are ignored.
export const TAGS: Record<string, string> = {
  exclusive: /* i18n */ "PlayStation exclusive",
  "local-coop": /* i18n */ "Local co-op",
  "split-screen": /* i18n */ "Split-screen",
  "open-world": /* i18n */ "Open world",
  "story-driven": /* i18n */ "Story-driven",
  soulslike: /* i18n */ "Soulslike",
  "psvr-supported": /* i18n */ "PSVR supported",
  "psvr2-supported": /* i18n */ "PSVR2 supported",
};
// The labelled tags the given games carry, in TAGS order.
export function catalogueTags(games: Game[]): string[] {
  const present = new Set(games.flatMap((g) => g.releases.flatMap((r) => r.tags || [])));
  return Object.keys(TAGS).filter((tag) => present.has(tag));
}
// Tiles mounted per step of an infinitely scrolling catalogue grid.
export const CATALOG_STEP = 96;
export interface BrowseFilters {
  query: string;
  addonType?: string;
  // Any of these; none for all.
  source: string[];
  format: string[];
  tags: string[]; // any of them
  region: string;
  platform: string;
  size: string;
  collection: "all" | "favorites";
  // Leaves out games installed on the PS5 or on one of its drives (Library).
  hideInstalled: boolean;
  sort: string;
}
// Browse's saved filters, kept on the PS5 (docs/api.md#saved-filters).
export interface SavedFilter {
  id: string;
  name: string;
  page: "games" | "addons";
  filters: BrowseFilters;
  savedAt: number;
}
// Anything set beyond a page's starting point, the search included.
export function narrowed(f: BrowseFilters, defaultSort: string) {
  return !!(
    f.query.trim() ||
    f.source.length ||
    f.format.length ||
    f.tags.length ||
    f.hideInstalled ||
    f.region ||
    f.platform ||
    f.size ||
    f.collection !== "all" ||
    f.addonType ||
    f.sort !== defaultSort
  );
}
export const defaultFilters: BrowseFilters = {
  query: "",
  source: [],
  format: [],
  tags: [],
  region: "",
  platform: "",
  size: "",
  collection: "all",
  hideInstalled: false,
  sort: "release",
};
// Catalogue picks lead the row; fill the remaining places with recent additions.
export function newlyAddedGames(games: Game[]): Game[] {
  const pinned = games
    .filter((game) => Number.isInteger(game.newOnOrbitRank) && game.newOnOrbitRank! > 0 && game.newOnOrbitRank! <= 100)
    .sort((a, b) => a.newOnOrbitRank! - b.newOnOrbitRank! || a.title.localeCompare(b.title))
    .slice(0, 40);
  const pinnedIds = new Set(pinned.map((game) => game.id));
  const recent = games
    .filter((game) => !pinnedIds.has(game.id))
    .map((game) => ({ game, added: Date.parse(game.addedAt || "") }))
    .filter(({ added }) => Number.isFinite(added))
    .sort((a, b) => b.added - a.added || a.game.title.localeCompare(b.game.title))
    .slice(0, 40 - pinned.length)
    .map(({ game }) => game)
    .sort((a, b) => (b.releaseDate || "").localeCompare(a.releaseDate || "") || a.title.localeCompare(b.title));
  return [...pinned, ...recent];
}
// installed: the IDs of games in the PS5's Library, for hideInstalled.
export function browseGames(
  games: Game[],
  filters: BrowseFilters,
  favorites: ReadonlySet<string>,
  installed: ReadonlySet<string> = new Set(),
): Game[] {
  const query = filters.query.trim().toLowerCase();
  const matches = games.flatMap((game) => {
    if (
      query &&
      !`${game.title} ${game.baseGameTitle || ""} ${game.releases.map((r) => r.titleId).join(" ")}`
        .toLowerCase()
        .includes(query)
    )
      return [];
    if (filters.collection === "favorites" && !favorites.has(game.id))
      return [];
    if (filters.hideInstalled && installed.has(game.id)) return [];
    if (filters.tags.length && !game.releases.some((r) => r.tags?.some((tag) => filters.tags.includes(tag)))) return [];
    if (filters.addonType && game.addonType !== filters.addonType) return [];
    const releases = game.releases.filter((r) => {
      if (filters.platform && gamePlatform(r) !== filters.platform) return false;
      if (filters.source.length && !filters.source.includes(r.sourceId)) return false;
      if (filters.format.length && !filters.format.some((f) => r.format === f || r.compression?.toUpperCase() === f.toUpperCase()))
        return false;
      if (filters.region && (r.region || "unknown") !== filters.region) return false;
      const gb = r.sizeBytes / 1e9;
      return (
        !filters.size ||
        (filters.size === "small"
          ? gb < 10
          : filters.size === "medium"
            ? gb >= 10 && gb < 50
            : filters.size === "large"
              ? gb >= 50 && gb < 100
              : gb >= 100)
      );
    });
    return releases.length
      ? [{ game, size: Math.min(...releases.map((r) => r.sizeBytes)) }]
      : [];
  });
  matches.sort((a, b) => {
    const title = a.game.title.localeCompare(b.game.title);
    switch (filters.sort) {
      case "added":
        return (b.game.addedAt || "").localeCompare(a.game.addedAt || "") || title;
      case "title-desc":
        return -title;
      case "release":
        return (
          (b.game.releaseDate || "").localeCompare(a.game.releaseDate || "") ||
          title
        );
      case "size":
        return a.size - b.size || title;
      case "size-desc":
        return b.size - a.size || title;
      default:
        return title;
    }
  });
  return matches.map((x) => x.game);
}
