// A one-time notice in Store pointing to App settings → Appearance.
import { useState } from "react";
import { t } from "../i18n";
import { Icon } from "../components";

const dismissedKey = "orbit.interface-notice.v1";

export function InterfaceNotice({ openSettings }: { openSettings: () => void }) {
  const [hidden, setHidden] = useState(() => {
    try {
      return localStorage.getItem(dismissedKey) === "dismissed";
    } catch {
      return false;
    }
  });
  if (hidden) return null;
  const dismiss = () => {
    setHidden(true);
    try {
      localStorage.setItem(dismissedKey, "dismissed");
    } catch {
      // Shown again next visit.
    }
  };
  return (
    <aside className="update-notice interface-notice" role="status">
      <div>
        <strong>{t("New: choose how Orbit looks")}</strong>
        <p>{t("Crossbar, Cinema and Panorama are new interfaces for Orbit. Store stays as it is unless you switch.")}</p>
      </div>
      <div className="update-notice-actions">
        <button
          className="primary"
          onClick={() => {
            dismiss();
            openSettings();
          }}
        >
          {t("Choose interface")}
        </button>
        <button className="icon-button" aria-label={t("Not now")} onClick={dismiss}>
          <Icon name="close" />
        </button>
      </div>
    </aside>
  );
}
