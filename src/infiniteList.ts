import { useEffect, useRef, useState } from "react";

// Mount the next tiles while the end of the list is still this far below the
// viewport, so neither scrolling nor controller focus reaches an empty edge.
const AHEAD = 1600;

/* Long grids render in steps instead of pages. A new `reset` value (such as a
 * changed filter) starts again from the first step. */
export function useInfiniteList(total: number, step: number, reset: unknown = null) {
  const [shown, setShown] = useState({ reset, count: step });
  if (shown.reset !== reset) setShown({ reset, count: step });
  const visible = Math.min(total, shown.reset === reset ? shown.count : step);
  const end = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (visible >= total) return;
    let frame = 0;
    // Geometry instead of IntersectionObserver: it also works in older console
    // browsers, and the margin applies even when an ancestor clips the grid.
    const check = () => {
      frame = 0;
      const element = end.current;
      if (!element?.getClientRects().length) return;
      if (element.getBoundingClientRect().top < window.innerHeight + AHEAD)
        setShown((current) => ({ ...current, count: current.count + step }));
    };
    const schedule = () => {
      if (!frame) frame = requestAnimationFrame(check);
    };
    window.addEventListener("scroll", schedule, { capture: true, passive: true });
    window.addEventListener("resize", schedule, { passive: true });
    schedule();
    return () => {
      cancelAnimationFrame(frame);
      window.removeEventListener("scroll", schedule, { capture: true });
      window.removeEventListener("resize", schedule);
    };
  }, [visible, total, step]);
  return { visible, end };
}
