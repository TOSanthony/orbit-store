import type { Game } from "./types";

// A game's "tagline" doubles as its menu: rows from the NAS folders dlc/, updates/ and apps/
// ("DLC", "UPDATES", "APPS") get their own tabs and stay out of the Games tab. Apps also show in
// Discover; DLC and updates only in their own tab.
export type Category = "game" | "apps" | "dlc" | "updates";

export function gameCategory(game: Pick<Game, "tagline">): Category {
  const tag = (game.tagline || "").trim().toUpperCase();
  return tag === "DLC"
    ? "dlc"
    : tag === "UPDATES"
      ? "updates"
      : tag === "APPS"
        ? "apps"
        : "game";
}

// DLC and update packs: one card per game, one file per pack (a list to choose from).
export const isPack = (game: Pick<Game, "tagline">) => {
  const category = gameCategory(game);
  return category === "dlc" || category === "updates";
};

// What Discover shows: games and apps.
export const inDiscover = (game: Pick<Game, "tagline">) => {
  const category = gameCategory(game);
  return category === "game" || category === "apps";
};
