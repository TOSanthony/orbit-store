// Cinema's building blocks: one thing at a time on a full-screen stage, a
// strip of frames to choose from, and collections you step through.
import { useEffect, useLayoutEffect, useRef, type KeyboardEvent, type ReactNode, type UIEvent } from "react";
import { Artwork } from "../Artwork";
import { formatNumber, t } from "../i18n";
import { Glyph } from "./Glyph";
import { menuKeys } from "./menuKeys";

// The art behind the stage: the chosen item's picture, full bleed, with
// veils so the title and buttons stay readable.
export function CinemaArt({ src, fallback }: { src?: string; fallback?: string }) {
  if (!src) return <div className="cn-art cn-art-empty" aria-hidden="true" />;
  return (
    <div className="cn-art" aria-hidden="true">
      <Artwork key={src} src={src} fallbackSrc={fallback} alt="" decoding="async" referrerPolicy="no-referrer" />
    </div>
  );
}

// "‹ NEW ON ORBIT ›" and a dot per collection.
export function CinemaCollections({
  label,
  index,
  count,
  step,
  children,
}: {
  label: string;
  index: number;
  count: number;
  step: (delta: number) => void;
  children?: ReactNode;
}) {
  return (
    <div className="cn-collections" role="group" aria-label={t("Collections")}>
      {count > 1 && (
        <button className="cn-step" aria-label={t("Previous")} disabled={index <= 0} onClick={() => step(-1)}>
          <Glyph name="back" size={16} stroke={2} />
        </button>
      )}
      <span className="cn-collection-name" aria-live="polite">
        {label}
      </span>
      {count > 1 && (
        <button className="cn-step" aria-label={t("Next")} disabled={index >= count - 1} onClick={() => step(1)}>
          <Glyph name="next" size={16} stroke={2} />
        </button>
      )}
      {count > 1 && (
        <span className="cn-dots" aria-hidden="true">
          {Array.from({ length: count }, (_, i) => (
            <i key={i} className={i === index ? "on" : ""} />
          ))}
        </span>
      )}
      {children}
    </div>
  );
}

// A long name takes two lines at most, as on the PS5 app: the type steps down
// (to about two thirds of the design's size) until it fits.
function useTwoLines(title: string) {
  const ref = useRef<HTMLHeadingElement>(null);
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const fit = () => {
      el.style.fontSize = "";
      const base = parseFloat(getComputedStyle(el).fontSize);
      const line = () => parseFloat(getComputedStyle(el).lineHeight) || parseFloat(getComputedStyle(el).fontSize);
      let size = base;
      while (size > base * 0.64 && el.getBoundingClientRect().height > line() * 2.5) {
        size -= 2;
        el.style.fontSize = `${size}px`;
      }
    };
    fit();
    void document.fonts?.ready.then(fit);
    window.addEventListener("resize", fit);
    return () => window.removeEventListener("resize", fit);
  }, [title]);
  return ref;
}

// Over rows, the stage never changes height, so the rows under it stay put:
// a name takes one line, its type stepping down to half the design's size;
// a longer one takes two lines at half size, in the same height.
function useOneLine(title: string, on: boolean) {
  const ref = useRef<HTMLHeadingElement>(null);
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el || !on) return;
    const fit = () => {
      el.style.fontSize = "";
      el.classList.remove("two");
      const base = parseFloat(getComputedStyle(el).fontSize);
      let size = base;
      while (size > base * 0.5 && el.scrollWidth > el.clientWidth + 1) {
        size -= 2;
        el.style.fontSize = `${size}px`;
      }
      if (el.scrollWidth > el.clientWidth + 1) {
        el.classList.add("two");
        el.style.fontSize = `${base * 0.5}px`;
      }
    };
    fit();
    void document.fonts?.ready.then(fit);
    window.addEventListener("resize", fit);
    return () => window.removeEventListener("resize", fit);
  }, [title, on]);
  return ref;
}

// The stage: a small gold line, the title in large italic serif, then whatever
// the page puts under it. Over rows (`fixed`) it keeps one height for every game.
export function CinemaStage({
  kicker,
  title,
  children,
  fixed = false,
}: {
  kicker?: ReactNode;
  title: string;
  children?: ReactNode;
  fixed?: boolean;
}) {
  const twoLines = useTwoLines(fixed ? "" : title);
  const oneLine = useOneLine(title, fixed);
  // Down from the stage's buttons goes back to the item's frame in the strip.
  function key(e: KeyboardEvent<HTMLElement>) {
    if (e.key !== "ArrowDown" || !(e.target as HTMLElement).closest(".cn-actions, .cn-game-actions") || (e.target as HTMLElement).closest('[role="menu"]')) return;
    const page = e.currentTarget.closest(".cn-page") || document;
    const frame = page.querySelector<HTMLElement>(".cn-strip .cn-frame.on") || page.querySelector<HTMLElement>(".cn-strip .cn-frame");
    if (frame) {
      e.preventDefault();
      frame.focus();
    }
  }
  return (
    <section className={`cn-stage${fixed ? " cn-stage-fixed" : ""}`} aria-live="polite" onKeyDown={key} data-own-arrows>
      {(kicker || fixed) && <p className="cn-kicker">{kicker}</p>}
      <h1 className="cn-title" ref={fixed ? oneLine : twoLines}>
        {title}
      </h1>
      {children}
    </section>
  );
}

export interface CinemaSection {
  key: string;
  label: string;
  frames: CinemaFrame[];
  // All games and Browse's results: lines of frames that grow as you go down.
  grid?: boolean;
  total?: number;
  // A frame this close to the end, or a scroll near the bottom, mounts more.
  reach?: (key: string) => void;
  more?: () => void;
}

// Rows under the stage, as the live Store has them: one strip per collection
// (New on Orbit, Latest releases) and a grid (All games). The focused row
// slides to the top of the rows; the stage above never moves.
export function CinemaRows({
  sections,
  current,
  choose,
  open,
}: {
  sections: CinemaSection[];
  current: { section: string; key: string } | undefined;
  choose: (section: string, key: string) => void;
  open: (section: string, key: string) => void;
}) {
  const view = useRef<HTMLDivElement>(null);
  const placed = useRef(false);
  // Put the focused frame's row (or grid line) at the top of the rows, and the
  // frame inside the gutters of its strip. The first time, with no motion.
  useLayoutEffect(() => {
    const box = view.current;
    if (!box || !current) return;
    const row = box.querySelector<HTMLElement>(`[data-row="${CSS.escape(current.section)}"]`);
    const frame = row?.querySelector<HTMLElement>(`[data-game="${CSS.escape(current.key)}"]`);
    if (!row || !frame) return;
    const behavior: ScrollBehavior = placed.current ? "smooth" : "auto";
    placed.current = true;
    const pad = parseFloat(getComputedStyle(box).paddingTop) || 0;
    const boxTop = box.getBoundingClientRect().top - box.scrollTop;
    const first = frame.parentElement?.firstElementChild as HTMLElement | null;
    const firstLine =
      !row.classList.contains("cn-row-grid") || !first || frame.getBoundingClientRect().top - first.getBoundingClientRect().top < 4;
    const top = (firstLine ? row.getBoundingClientRect() : frame.getBoundingClientRect()).top - boxTop - pad;
    // After the document's own move, which only brings the frame into view.
    requestAnimationFrame(() => box.scrollTo({ top: Math.max(0, top), behavior }));
    const strip = frame.parentElement;
    if (strip && row.classList.contains("cn-row-strip")) {
      const gutter = parseFloat(getComputedStyle(strip).paddingRight) || 0;
      const room = strip.clientWidth - gutter;
      const left = frame.offsetLeft - (parseFloat(getComputedStyle(strip).paddingLeft) || 0);
      const right = frame.offsetLeft + frame.offsetWidth;
      const to = left < strip.scrollLeft ? left : right > strip.scrollLeft + room ? right - room : null;
      if (to !== null) requestAnimationFrame(() => strip.scrollTo({ left: to, behavior }));
    }
  }, [current?.section, current?.key]);
  // The arrows move within the rows: along a line, or to the frame below or
  // above in the next line. Up from the first line goes on to the stage.
  function key(e: KeyboardEvent<HTMLDivElement>) {
    const box = view.current;
    const frame = (e.target as HTMLElement).closest<HTMLElement>(".cn-frame");
    if (!box || !frame || !e.key.startsWith("Arrow")) return;
    const lines: HTMLElement[][] = [];
    box.querySelectorAll<HTMLElement>(".cn-row").forEach((row) => {
      const frames = [...row.querySelectorAll<HTMLElement>(".cn-frame")];
      if (row.classList.contains("cn-row-strip")) lines.push(frames);
      else
        frames.forEach((f, i) => {
          if (i === 0 || f.offsetTop !== frames[i - 1].offsetTop) lines.push([]);
          lines[lines.length - 1].push(f);
        });
    });
    const at = lines.findIndex((l) => l.includes(frame));
    if (at < 0) return;
    let next: HTMLElement | undefined;
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      next = lines[at][lines[at].indexOf(frame) + (e.key === "ArrowRight" ? 1 : -1)];
    } else {
      const to = at + (e.key === "ArrowDown" ? 1 : -1);
      if (to < 0) return;
      const x = frame.getBoundingClientRect().left;
      next = lines[to]?.reduce((a, b) =>
        Math.abs(b.getBoundingClientRect().left - x) < Math.abs(a.getBoundingClientRect().left - x) ? b : a,
      );
    }
    e.preventDefault();
    next?.focus({ preventScroll: true });
  }
  const grow = (e: UIEvent<HTMLDivElement>) => {
    const el = e.currentTarget;
    if (el.scrollTop + el.clientHeight > el.scrollHeight - 600) sections.forEach((s) => s.more?.());
  };
  return (
    <div className="cn-rows" ref={view} onScroll={grow} onKeyDown={key} data-own-arrows>
      {sections.map((s) => {
        const here = current?.section === s.key;
        const at = here ? s.frames.findIndex((f) => f.key === current?.key) : -1;
        const count = s.total ?? s.frames.length;
        return (
          <section key={s.key} className={`cn-row ${s.grid ? "cn-row-grid" : "cn-row-strip"}`} data-row={s.key}>
            <p className="cn-strip-label">
              {s.label} · {at >= 0 ? t("{n} of {count}", { n: formatNumber(at + 1), count: formatNumber(count) }) : formatNumber(count)}
            </p>
            <div className="cn-row-frames" role="listbox" aria-label={s.label}>
              {s.frames.map((f) => {
                const on = here && f.key === current?.key;
                return (
                  <button
                    key={f.key}
                    role="option"
                    aria-selected={on}
                    aria-label={f.label}
                    data-game={f.key}
                    className={`cn-frame ${on ? "on" : ""}`}
                    onFocus={() => {
                      choose(s.key, f.key);
                      s.reach?.(f.key);
                    }}
                    onClick={() => open(s.key, f.key)}
                  >
                    {f.art ? (
                      <Artwork src={f.art} fallbackSrc={f.fallback} alt="" decoding="async" referrerPolicy="no-referrer" />
                    ) : (
                      <span className="cn-frame-text">{f.text || f.label}</span>
                    )}
                    {f.subtitle && <span className="cn-addon-caption"><strong>{f.label}</strong><span>{f.subtitle}</span></span>}
                    {f.favorite && <span className="cn-favorite" aria-label={t("Favourite")}><Glyph name="heartFill" size={18} /></span>}
                    {f.badge && <span className="cn-badge">{f.badge}</span>}
                    {f.progress !== undefined && (
                      <span className="cn-frame-bar" aria-hidden="true">
                        <i style={{ width: `${Math.round(Math.max(0, Math.min(1, f.progress)) * 100)}%` }} />
                      </span>
                    )}
                  </button>
                );
              })}
            </div>
          </section>
        );
      })}
    </div>
  );
}

export interface CinemaFrame {
  favorite?: boolean;
  subtitle?: string;
  key: string;
  label: string; // spoken name
  art?: string;
  fallback?: string;
  badge?: string;
  progress?: number; // 0..1
  text?: string; // shown instead of art (no picture)
}

// The strip: frames to choose from, bottom right. Focusing a frame puts it on
// the stage; Cross opens it. Up and Down step to the collection above or below
// (Up from the first returns to the stage's buttons), as rows do in Store.
export function CinemaStrip({
  label,
  frames,
  current,
  choose,
  open,
  stepCollection,
  canStep,
  footer,
  offset = 0,
  total,
}: {
  label: string;
  frames: CinemaFrame[];
  // A page of a longer list: where it starts and how long the list is.
  offset?: number;
  total?: number;
  current: string | undefined;
  choose: (key: string) => void;
  open?: (key: string) => void;
  stepCollection?: (delta: number) => boolean;
  canStep?: (delta: number) => boolean;
  footer?: ReactNode;
}) {
  const strip = useRef<HTMLDivElement>(null);
  const at = Math.max(0, frames.findIndex((f) => f.key === current));
  // Keep the chosen frame in view as the strip moves.
  useEffect(() => {
    const el = strip.current?.querySelector<HTMLElement>(`[data-frame="${CSS.escape(current || "")}"]`);
    el?.scrollIntoView({ block: "nearest", inline: "center", behavior: "smooth" });
  }, [current]);
  // L1 and R1 step through the collections, then on to the next tab, as on
  // the PS5 app (the controller asks the page first).
  const stepping = useRef({ stepCollection, canStep });
  stepping.current = { stepCollection, canStep };
  useEffect(() => {
    const onStep = (e: Event) => {
      const delta = (e as CustomEvent<number>).detail;
      const { stepCollection: step, canStep: can } = stepping.current;
      if (!step || !can?.(delta)) return;
      e.preventDefault();
      step(delta);
      requestAnimationFrame(() => strip.current?.querySelector<HTMLElement>(".cn-frame")?.focus());
    };
    document.addEventListener("orbit-step", onStep);
    return () => document.removeEventListener("orbit-step", onStep);
  }, []);
  // Up goes to the focused item's buttons on the stage above; Down stays.
  function key(e: KeyboardEvent<HTMLDivElement>) {
    if (e.key === "ArrowDown") e.preventDefault();
    if (e.key !== "ArrowUp") return;
    const page = strip.current?.closest(".cn-page") || document;
    const button = page.querySelector<HTMLElement>(".cn-stage :is(.cn-actions, .cn-game-actions) :is(button:not([disabled]), summary)");
    if (button) {
      e.preventDefault();
      button.focus();
    }
  }
  return (
    <div className="cn-strip" onKeyDown={key} data-own-arrows>
      <p className="cn-strip-label">
        {frames.length ? `${label} · ${t("{n} of {count}", { n: formatNumber(offset + at + 1), count: formatNumber(total ?? frames.length) })}` : label}
      </p>
      <div className="cn-frames" ref={strip} role="listbox" aria-label={label}>
        {frames.map((f) => (
          <button
            key={f.key}
            role="option"
            aria-selected={f.key === current}
            aria-label={f.label}
            data-frame={f.key}
            className={`cn-frame ${f.key === current ? "on" : ""}`}
            onFocus={() => choose(f.key)}
            onClick={() => (open ? open(f.key) : choose(f.key))}
          >
            {f.art ? (
              <Artwork src={f.art} fallbackSrc={f.fallback} alt="" decoding="async" referrerPolicy="no-referrer" />
            ) : (
              <span className="cn-frame-text">{f.text || f.label}</span>
            )}
            {f.subtitle && <span className="cn-addon-caption"><strong>{f.label}</strong><span>{f.subtitle}</span></span>}
            {f.favorite && <span className="cn-favorite" aria-label={t("Favourite")}><Glyph name="heartFill" size={18} /></span>}
            {f.badge && <span className="cn-badge">{f.badge}</span>}
            {f.progress !== undefined && (
              <span className="cn-frame-bar" aria-hidden="true">
                <i style={{ width: `${Math.round(Math.max(0, Math.min(1, f.progress)) * 100)}%` }} />
              </span>
            )}
          </button>
        ))}
      </div>
      {footer}
    </div>
  );
}

// A menu under a "More" button: the rest of an item's actions.
export function CinemaMore({ actions }: { actions: { key: string; label: string; run: () => void; disabled?: boolean }[] }) {
  const box = useRef<HTMLDetailsElement>(null);
  if (!actions.length) return null;
  return (
    <details className="cn-more" ref={box}>
      <summary className="cn-button">
        <Glyph name="more" size={18} />
        {t("More")}
      </summary>
      <div className="cn-menu" role="menu" data-own-arrows onKeyDown={(e) => menuKeys(e, () => box.current?.removeAttribute("open"))}>
        {actions.map((a) => (
          <button
            key={a.key}
            role="menuitem"
            disabled={a.disabled}
            onClick={() => {
              box.current?.removeAttribute("open");
              a.run();
            }}
          >
            {a.label}
          </button>
        ))}
      </div>
    </details>
  );
}
