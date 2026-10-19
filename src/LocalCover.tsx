import { useEffect, useRef, useState, type RefObject } from "react";
import { apiImage } from "./api";
import { Artwork } from "./Artwork";
import { watchArtwork } from "./artworkVisibility";

// A library game's picture: the icon the console keeps for it when Orbit has one, otherwise the
// catalogue's cover. Orbit picks (GET /api/v1/art/local-<title id>/cover), as it does for the TV app.
// The icon Orbit has for an installed game as an object URL (null until found, or when there is none).
// With a ref, nothing is asked until that element is near the screen.
export function useLocalIcon(
  titleId: string,
  ref?: RefObject<Element | null>,
): string | null {
  const [visible, setVisible] = useState(!ref);
  const [local, setLocal] = useState<string | null>(null);
  useEffect(() => {
    if (!visible && ref?.current)
      return watchArtwork(ref.current, () => setVisible(true));
  }, [visible, ref]);
  useEffect(() => {
    if (!visible) return;
    let alive = true;
    let url: string | null = null;
    void apiImage(`/art/local-${titleId}/cover`).then((made) => {
      if (!alive) {
        if (made) URL.revokeObjectURL(made);
        return;
      }
      url = made;
      setLocal(made);
    });
    return () => {
      alive = false;
      if (url) URL.revokeObjectURL(url);
    };
  }, [visible, titleId]);
  return local;
}

// A library game's picture: the icon the console keeps for it when Orbit has one, otherwise the
// catalogue's cover. Orbit picks (GET /api/v1/art/local-<title id>/cover), as it does for the TV app.
export function LocalCover({
  titleId,
  cover,
  fallback,
}: {
  titleId: string;
  cover?: string | null;
  fallback?: string | null;
}) {
  const ref = useRef<HTMLSpanElement>(null);
  const local = useLocalIcon(titleId, ref);
  return (
    <span className="library-cover" ref={ref}>
      <Artwork
        key={local || cover || "mark"}
        src={local || cover || "/orbit.svg"}
        fallbackSrc={fallback}
        alt=""
        decoding="async"
        referrerPolicy="no-referrer"
      />
    </span>
  );
}

// The same picture as an <img> with a class of its own (the list of game updates).
export function GameIcon({
  titleId,
  cover,
  fallback,
  className,
}: {
  titleId: string;
  cover?: string | null;
  fallback?: string | null;
  className?: string;
}) {
  const local = useLocalIcon(titleId);
  return (
    <Artwork
      className={className}
      key={local || cover || "mark"}
      src={local || cover || "/orbit.svg"}
      fallbackSrc={fallback}
      alt=""
      decoding="async"
      referrerPolicy="no-referrer"
    />
  );
}
