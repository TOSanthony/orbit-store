import { VikingAccount } from "../VikingAccount";
// Settings as a page (Crossbar, Cinema and Panorama): the same panels as
// Store's App settings, Sources, Storage and Pairing dialogs, grouped into
// sections. Store keeps its dialogs.
import { useEffect, useRef, useState, type ReactNode } from "react";
import type { SourceSettings, System } from "../types";
import { languages, savedLanguage, t, tn } from "../i18n";
import { interfaces, useInterfacePrefs } from "./choice";
import { Appearance } from "./Appearance";
import { LanguageSetting } from "../LanguageSetting";
import { CatalogueUpdates } from "../CatalogueUpdates";
import { Updates } from "../Updates";
import { TvApp } from "../TvApp";
import { Debrid } from "../Debrid";
import { ArchiveAccount } from "../ArchiveAccount";
import { Diagnostics } from "../Diagnostics";
import { PayloadManagers, StartAutomatically, useAutoStart } from "../AutoStart";
import { Glyph, type GlyphName } from "./Glyph";

export type SettingsSection =
  | "appearance"
  | "language"
  | "sources"
  | "storage"
  | "notifications"
  | "pair"
  | "catalogue"
  | "updates"
  | "tvapp"
  | "zero"
  | "torbox"
  | "archive"
  | "viking"
  | "payload"
  | "autostart"
  | "diagnostics";

export const settingsGroups: { label: string; sections: { id: SettingsSection; label: string; glyph: GlyphName }[] }[] = [
  {
    label: /* i18n */ "On this device",
    sections: [
      { id: "appearance", label: /* i18n */ "Appearance", glyph: "palette" },
      { id: "language", label: /* i18n */ "Language", glyph: "globe" },
    ],
  },
  {
    label: /* i18n */ "Orbit on your PS5",
    sections: [
      { id: "sources", label: /* i18n */ "Download sources", glyph: "cloud" },
      { id: "storage", label: /* i18n */ "Storage and downloads", glyph: "drive" },
      { id: "notifications", label: /* i18n */ "Notifications", glyph: "bell" },
      { id: "pair", label: /* i18n */ "Pair devices", glyph: "link" },
      { id: "catalogue", label: /* i18n */ "Game catalogue", glyph: "refresh" },
      { id: "updates", label: /* i18n */ "Updates", glyph: "update" },
      { id: "tvapp", label: /* i18n */ "TV app", glyph: "tv" },
      { id: "zero", label: "Orbit Zero", glyph: "laptop" },
      { id: "torbox", label: "TorBox", glyph: "bolt" },
      { id: "archive", label: "Archive.org", glyph: "key" },
      { id: "viking", label: "Vikingfile", glyph: "key" },
    ],
  },
  {
    label: /* i18n */ "Maintenance",
    sections: [
      { id: "payload", label: /* i18n */ "Payload managers", glyph: "box" },
      { id: "autostart", label: /* i18n */ "Start automatically", glyph: "power" },
      { id: "diagnostics", label: /* i18n */ "Diagnostics", glyph: "bug" },
    ],
  },
];
export const settingsSections = settingsGroups.flatMap((g) => g.sections);

export function SettingsPage({
  section,
  choose,
  system,
  sources,
  storage,
  notifications,
  zero,
  pairing,
  updateAvailable,
  sourceSettings,
}: {
  section: SettingsSection;
  choose: (section: SettingsSection) => void;
  system: System | null;
  sources: ReactNode;
  storage: ReactNode;
  notifications: ReactNode;
  zero: ReactNode;
  pairing: ReactNode;
  updateAvailable: boolean;
  sourceSettings: SourceSettings | null;
}) {
  const auto = useAutoStart();
  const { prefs } = useInterfacePrefs();
  // Each section's current value, where the page already knows it.
  const language = savedLanguage();
  const values: Partial<Record<SettingsSection, string>> = {
    appearance: t(interfaces.find((i) => i.name === prefs.name)?.title || ""),
    ...(sourceSettings
      ? {
          sources: sourceSettings.options
            .filter((o) => sourceSettings.enabled.includes(o.id))
            .map((o) => o.label)
            .join(", "),
        }
      : {}),
    language: language ? languages.find((l) => l.code === language)?.name || "" : t("Automatic"),
    ...(system?.paired && system.concurrentDownloads
      ? { storage: tn(system.concurrentDownloads, "{count} at a time", "{count} at a time") }
      : {}),
    ...(system?.notificationEvents
      ? {
          notifications:
            [system.downloadNotifications !== false && "PS5", system.ntfyEnabled && "ntfy"].filter(Boolean).join(", ") ||
            t("Off"),
        }
      : {}),
    ...(system?.catalogueRevision ? { catalogue: t("Catalogue {revision}", { revision: system.catalogueRevision }) } : {}),
    ...(system?.version ? { updates: system.version } : {}),
  };
  // Stopping Orbit for an update leaves only Updates usable, as in App settings.
  const [stopped, setStopped] = useState(false);
  const current = settingsSections.find((s) => s.id === section) || settingsSections[0];
  // Panorama's pivots run off the page: keep the chosen one in view.
  const nav = useRef<HTMLElement>(null);
  useEffect(() => {
    nav.current?.querySelector('[aria-current="page"]')?.scrollIntoView({ block: "nearest", inline: "nearest" });
  }, [current.id]);
  // A newly chosen section opens at its top: a long one before it may have
  // left the page scrolled past where the next one starts.
  const panel = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const el = panel.current;
    let box = el?.parentElement;
    while (box && !(box.scrollHeight > box.clientHeight && /auto|scroll/.test(getComputedStyle(box).overflowY))) box = box.parentElement;
    if (!el || !box) return;
    const above = el.getBoundingClientRect().top - box.getBoundingClientRect().top;
    if (above < 0) box.scrollTop += above;
  }, [current.id]);
  const content: Record<SettingsSection, ReactNode> = {
    appearance: <Appearance page />,
    language: <LanguageSetting />,
    sources,
    storage,
    notifications,
    pair: pairing,
    catalogue: <CatalogueUpdates />,
    updates: <Updates onStopped={() => setStopped(true)} />,
    tvapp: <TvApp />,
    zero,
    torbox: <Debrid />,
    archive: <ArchiveAccount />,
    viking: <VikingAccount />,
    payload: (
      <>
        <PayloadManagers system={system} auto={auto} />
        {auto.error && (
          <p className="form-error" role="alert">
            {auto.error}
          </p>
        )}
      </>
    ),
    autostart: (
      <>
        <StartAutomatically auto={auto} />
        {!auto.status?.available && auto.status && (
          <p className="notice">{t("Manager integration is set up from Orbit on your PS5.")}</p>
        )}
        {auto.error && (
          <p className="form-error" role="alert">
            {auto.error}
          </p>
        )}
      </>
    ),
    diagnostics: <Diagnostics />,
  };
  return (
    <section className="settings-page" aria-label={t("Settings")}>
      <nav className="settings-sections" ref={nav} aria-label={t("Settings sections")}>
        {settingsGroups.map((group) => (
          <div className={`settings-group${group.sections.some((s) => s.id === current.id) ? " current" : ""}`} key={group.label}>
            <p className="settings-group-label">{t(group.label)}</p>
            {group.sections.map((s) => (
              <button
                key={s.id}
                className="settings-section"
                aria-current={s.id === current.id ? "page" : undefined}
                disabled={stopped && s.id !== "updates"}
                onClick={() => choose(s.id)}
              >
                <span className="settings-section-icon">
                  <Glyph name={s.glyph} size={18} />
                </span>
                <span className="settings-section-label">{["TorBox", "Archive.org", "Orbit Zero"].includes(s.label) ? s.label : t(s.label)}</span>
                {values[s.id] && <span className="settings-section-value">{values[s.id]}</span>}
                {s.id === "updates" && updateAvailable && (
                  <i className="nav-dot" role="img" aria-label={t("Update available")} />
                )}
              </button>
            ))}
          </div>
        ))}
      </nav>
      <div ref={panel} className={`settings-panel settings-panel-${current.id}`} key={current.id}>
        {/* The other panels open with their own heading. */}
        {(current.id === "storage" || current.id === "notifications") && <h1 className="settings-title">{t(current.label)}</h1>}
        {stopped && current.id !== "updates" ? content.updates : content[current.id]}
      </div>
    </section>
  );
}
