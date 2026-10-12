import type { Game } from "./types";

// A game's "tagline" doubles as its menu: "DLC" and "UPDATES" rows (packs found in the NAS
// folders dlc/ and updates/) get their own tabs and stay out of Discover and Browse.
export type Category = "game" | "dlc" | "updates";

export function gameCategory(game: Pick<Game, "tagline">): Category {
  const tag = (game.tagline || "").trim().toUpperCase();
  return tag === "DLC" ? "dlc" : tag === "UPDATES" ? "updates" : "game";
}

export const isPack = (game: Pick<Game, "tagline">) =>
  gameCategory(game) !== "game";
