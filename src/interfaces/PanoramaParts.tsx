// Panorama's building blocks: a group of tiles under a lowercase heading, and
// tiles of three sizes (large 2×2, wide 2×1, small 1×1) that can carry art, a
// kicker, a title, progress, a live number, or their own buttons.
import { useId, type ReactNode } from "react";
import { Artwork } from "../Artwork";
import { Glyph, type GlyphName } from "./Glyph";

export function PnGroup({
  title,
  count,
  className = "",
  foot,
  children,
}: {
  title: string;
  count?: string;
  className?: string;
  // Text under the tiles (a game's description, a note).
  foot?: ReactNode;
  children: ReactNode;
}) {
  return (
    <section className={`pn-group ${className}`} aria-label={title}>
      <h2>
        {title}
        {count && <span> {count}</span>}
      </h2>
      <div className="pn-tiles">{children}</div>
      {foot && <div className="pn-group-foot">{foot}</div>}
    </section>
  );
}

export function PnTile({
  size = "small",
  tone,
  art,
  fallback,
  glyph,
  kicker,
  title,
  sub,
  number,
  progress,
  label,
  current,
  check,
  disabled,
  className = "",
  onClick,
  onFocus,
  children,
}: {
  size?: "large" | "wide" | "small";
  // Tiles without art: the accent, Metro red for problems, or dark.
  tone?: "accent" | "red" | "dark";
  art?: string;
  fallback?: string;
  glyph?: GlyphName;
  kicker?: string;
  title?: ReactNode;
  sub?: ReactNode;
  // A live tile's big number, top right.
  number?: string;
  // 0–100; a bar along the bottom edge.
  progress?: number;
  // The tile's accessible name, when its title isn't enough.
  label?: string;
  current?: boolean;
  // A chosen option: a check in the top-right corner.
  check?: boolean;
  disabled?: boolean;
  className?: string;
  onClick?: () => void;
  onFocus?: () => void;
  // Buttons inside the tile (a focused download's Pause and Cancel).
  children?: ReactNode;
}) {
  const textId = useId();
  return (
    <div className={`pn-tile ${size} ${tone || ""}${art ? " has-art" : ""}${current ? " current" : ""}${disabled ? " off" : ""} ${className}`}>
      {art && <Artwork className="pn-tile-art" src={art} fallbackSrc={fallback} alt="" decoding="async" referrerPolicy="no-referrer" />}
      {/* Art and live numbers alone aren't buttons. */}
      {(onClick || onFocus) && (
        <button
          className="pn-tile-hit"
          aria-label={label}
          aria-labelledby={label ? undefined : textId}
          aria-current={current || undefined}
          aria-pressed={check === undefined ? undefined : check}
          disabled={disabled}
          onClick={onClick}
          onFocus={onFocus}
        />
      )}
      {check && <span className="pn-tile-check" aria-hidden="true" />}
      {glyph && (
        <span className="pn-tile-glyph" aria-hidden="true">
          <Glyph name={glyph} size={22} stroke={1.6} />
        </span>
      )}
      {number && (
        <span className="pn-tile-number" aria-hidden="true">
          {number}
        </span>
      )}
      {kicker && <span className="pn-tile-kicker">{kicker}</span>}
      {children && <div className="pn-tile-actions">{children}</div>}
      {(title || sub) && (
        <span className="pn-tile-text" id={textId}>
          {title && <span className="pn-tile-title">{title}</span>}
          {sub && <span className="pn-tile-sub">{sub}</span>}
        </span>
      )}
      {progress !== undefined && (
        <span className="pn-tile-bar" aria-hidden="true">
          <i style={{ width: `${Math.max(0, Math.min(100, progress))}%` }} />
        </span>
      )}
    </div>
  );
}
