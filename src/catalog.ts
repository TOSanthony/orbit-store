import type { Game, Release } from "./types";
import { gamePlatform } from "./platform";
import { compatibleFirst } from "./firmware";

// A curated identity joins regions, sources and formats without guessing from titles.
export function groupGames(releases: Release[]): Game[] {
  const games = new Map<string, Game>();
  for (const release of releases) {
    let game = games.get(release.gameId);
    if (!game) {
      game = {
        id: release.gameId,
        addonId: release.addonId, baseGameId: release.baseGameId,
        baseGameTitle: release.baseGameTitle, addonType: release.addonType,
        contents: release.contents,

        platform: gamePlatform(release),
        title: release.title,
        genre: release.genre,
        tagline: release.tagline,
        description: release.description,
        cover: release.cover,
        hero: release.hero,
        coverFallback: release.coverFallback,
        heroFallback: release.heroFallback,
        artworkLayout: release.artworkLayout,
        publisher: release.publisher,
        releaseDate: release.releaseDate,
        addedAt: release.addedAt ?? null,
        newOnOrbitRank: release.newOnOrbitRank,
        releases: [],
      };
      games.set(game.id, game);
    }
    game.releases.push(release);
  }
  for (const game of games.values()) game.releases = compatibleFirst(game.releases);
  return [...games.values()];
}
