import { useEffect, useState } from "react";
import { t } from "./i18n";
import { Glyph } from "./interfaces/Glyph";

// "Added to favourites" or "Removed from favourites" for a moment after a
// change, in every look, as the PS5 app says it. Each look styles it as its
// design draws notices.
export function FavoriteNotice({ notice }: { notice: { added: boolean; at: number } | null }) {
  const [shown, setShown] = useState<{ added: boolean; at: number } | null>(null);
  useEffect(() => {
    if (!notice) return;
    setShown(notice);
    const timer = setTimeout(() => setShown(null), 2800);
    return () => clearTimeout(timer);
  }, [notice]);
  if (!shown) return null;
  return (
    <p className="favorite-notice" role="status">
      <span className="favorite-notice-icon">
        <Glyph name="heartFill" size={16} />
      </span>
      {shown.added ? t("Added to favourites") : t("Removed from favourites")}
    </p>
  );
}
