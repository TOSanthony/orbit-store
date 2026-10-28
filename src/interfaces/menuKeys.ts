// A popup menu ("More", a game's choices) keeps the keys while it's open, on a
// pad and on a keyboard: Up and Down move among its items that can be used,
// no arrow leaves it, and Escape (Circle) closes it and hands the focus back
// to the button that opened it.
import type { KeyboardEvent } from "react";

export function menuKeys(e: KeyboardEvent<HTMLElement>, close: () => void) {
  const box = e.currentTarget;
  if (e.key === "Escape" || e.key === "BrowserBack") {
    e.preventDefault();
    e.stopPropagation();
    const trigger = box
      .closest(".xb-menu-anchor, .pn-flyout-anchor, .cn-more")
      ?.querySelector<HTMLElement>('[aria-haspopup="menu"], summary');
    close();
    requestAnimationFrame(() => trigger?.focus({ preventScroll: true }));
    return;
  }
  if (!e.key.startsWith("Arrow")) return;
  e.preventDefault();
  e.stopPropagation();
  if (e.key !== "ArrowUp" && e.key !== "ArrowDown") return;
  const items = [...box.querySelectorAll<HTMLElement>("button:not([disabled])")];
  const at = items.indexOf(document.activeElement as HTMLElement);
  items[at < 0 ? 0 : at + (e.key === "ArrowDown" ? 1 : -1)]?.focus();
}

// The item a menu opens on: the first one that can be used.
export function firstEnabled(items: { disabled?: boolean }[]) {
  return Math.max(0, items.findIndex((a) => !a.disabled));
}
