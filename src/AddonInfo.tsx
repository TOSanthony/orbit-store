import { t, tn } from "./i18n";
import type { AddonIdentity, LibraryGame } from "./types";
export const addonTypes = { expansion: /* i18n */ "Expansion", "content-pack": /* i18n */ "Content pack", activation: /* i18n */ "Activation package", bundle: /* i18n */ "Bundle" } as const;
export function AddonInfo({ item, library, contents = false }: { item: AddonIdentity; library?: LibraryGame[]; contents?: boolean }) {
  if (!item.addonId) return null;
  const r = item.requirements;
  const matching = library?.some(g => r?.titleIds.includes(g.titleId) && (g.installed || g.onDrive));
  return <section className="addon-requirements" aria-label={t("Base-game requirements")}>
    <p><strong>{item.baseGameTitle}</strong> · {t(addonTypes[item.addonType || "content-pack"])}</p>
    {item.addonType === "activation" && <p>{t("This package activates content already included in the base game. It does not contain that content.")}</p>}
    {contents && item.contents?.length ? <ul>{item.contents.map(text => <li key={text}>{text}</li>)}</ul> : null}
    <dl><dt>{t("Required title ID")}</dt><dd>{r?.titleIds.join(", ") || t("Unknown")}</dd>
      <dt>{t("Base-game edition")}</dt><dd>{r?.edition || t("Unknown")}</dd>
      <dt>{t("Minimum game version")}</dt><dd>{r?.minimumVersion || t("Unknown")}</dd></dl>
    {r?.notes && <p>{r.notes}</p>}
    <p className="fine">{t(matching ? /* i18n */ "A matching base-game title ID was found. This does not confirm edition, version or DLC compatibility." : /* i18n */ "The required base game could not be confirmed. Check its title ID, edition and version before installing this add-on.")}</p>
    <p className="fine">{t("You can still download this add-on. Unknown requirements are not a compatibility guarantee.")}</p>
  </section>;
}

export const addonCountLabel = (count: number) => tn(count, "{count} add-on", "{count} add-ons");
