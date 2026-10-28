// Crossbar's list: the focused row stays put under the category bar and the
// list moves around it, as on the PS5 (the same on the PS5 app and Orbit
// Zero). Rows above the focused one fade; the page itself never scrolls.
import { useEffect, useLayoutEffect, useRef, useState, type KeyboardEvent, type ReactNode, type WheelEvent } from "react";

export interface XbRow {
  key: string;
  // A small label that heads a run of rows ("NEW ON ORBIT"); not focusable.
  label?: string;
  // The row's content, inside its focus target; focused rows show more.
  render: (focused: boolean) => ReactNode;
  // Buttons beside the content, shown while the row is focused.
  actions?: ReactNode;
  // Cross on the row itself (rows can also hold their own buttons).
  activate?: () => void;
  activateOnClick?: boolean;
  disabled?: boolean;
  // A setting-like row: focused, it stays one line instead of growing into a card.
  compact?: boolean;
}

// The focused row's top, in rem below the top of the track.
const kSlot = 6;

export function CrossbarList({
  rows,
  current,
  setCurrent,
  back,
  ariaLabel,
}: {
  rows: XbRow[];
  current: string | undefined;
  setCurrent: (key: string) => void;
  // Circle inside a folder: leave it (return false to let the app go back).
  back?: () => boolean;
  ariaLabel: string;
}) {
  const rail = useRef<HTMLDivElement>(null);
  const [shift, setShift] = useState(0);
  const focusable = rows.filter((r) => !r.label);
  const at = Math.max(0, focusable.findIndex((r) => r.key === current));
  const focusedKey = focusable[at]?.key;
  // The focused row always sits in the same place, the first row too; above
  // it there is room for the row before it (faded) or its run's label.
  useLayoutEffect(() => {
    const row = rail.current?.querySelector<HTMLElement>(`[data-xb-row="${CSS.escape(focusedKey || "")}"]`);
    if (!row) return;
    const rem = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
    setShift(row.offsetTop - kSlot * rem);
  }, [focusedKey, rows.length]);
  // A row that disappears (a finished download) hands the focus to its neighbour.
  useEffect(() => {
    if (focusable.length && !focusable.some((r) => r.key === current)) setCurrent(focusable[Math.min(at, focusable.length - 1)].key);
  }, [focusable, current, at, setCurrent]);
  const focusRow = (key: string) =>
    requestAnimationFrame(() =>
      rail.current?.querySelector<HTMLElement>(`[data-xb-row="${CSS.escape(key)}"] .xb-row-main`)?.focus({ preventScroll: true }),
    );
  // A folder opening or closing replaces the rows; when that takes the focus
  // with it, the focused row gets it, so the next press still moves the list.
  const keys = rows.map((r) => r.key).join("\n");
  useEffect(() => {
    if (focusedKey && document.activeElement === document.body) focusRow(focusedKey);
  }, [keys, focusedKey]);
  const move = (delta: number) => {
    const next = focusable[at + delta];
    if (!next) return false;
    setCurrent(next.key);
    focusRow(next.key);
    return true;
  };
  function key(e: KeyboardEvent<HTMLDivElement>) {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      // Up and down always step a whole row, from its buttons too.
      if (move(e.key === "ArrowDown" ? 1 : -1) || e.key === "ArrowDown") e.preventDefault();
    } else if ((e.key === "Escape" || e.key === "BrowserBack") && back?.()) {
      e.preventDefault();
    }
  }
  function wheel(e: WheelEvent<HTMLDivElement>) {
    if (Math.abs(e.deltaY) < 8) return;
    move(e.deltaY > 0 ? 1 : -1);
  }
  return (
    <div className="xb-track" onKeyDown={key} onWheel={wheel}>
      <div className="xb-rail" ref={rail} role="list" aria-label={ariaLabel} style={{ transform: `translateY(${-shift}px)` }}>
        {rows.map((row) => {
          if (row.label)
            return (
              <p className="xb-label-row" key={row.key} role="presentation">
                {row.label}
              </p>
            );
          const index = focusable.indexOf(row);
          const focused = row.key === focusedKey;
          return (
            <div
              key={row.key}
              role="listitem"
              data-xb-row={row.key}
              className={`xb-row${focused ? " on" : ""}${index < at ? " above" : ""}${row.disabled ? " off" : ""}${row.compact ? " compact" : ""}`}
              onFocus={() => row.key !== current && setCurrent(row.key)}
            >
              <button
                className="xb-row-main"
                aria-current={focused || undefined}
                onMouseDown={(e) => { if (row.activateOnClick) e.preventDefault(); }}
                onClick={() => ((focused || row.activateOnClick) && row.activate ? row.activate() : setCurrent(row.key))}
              >
                {row.render(focused)}
              </button>
              {focused && row.actions && <div className="xb-row-actions">{row.actions}</div>}
            </div>
          );
        })}
      </div>
    </div>
  );
}
