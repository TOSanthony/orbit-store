// App settings → Appearance: the interface cards, then Motion and (for
// Panorama) the accent colour. Saved on this device only.
import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from "react";
import { t } from "../i18n";
import { Select } from "../Select";
import {
  chooseInterface,
  interfaces,
  keepInterface,
  revertInterface,
  ribbonTitles,
  setInterfacePrefs,
  useInterfacePrefs,
  type Accent,
  type InterfaceName,
  type Ribbons,
} from "./choice";

const accents: { value: Accent; label: string }[] = [
  { value: "blue", label: /* i18n */ "Blue" },
  { value: "magenta", label: /* i18n */ "Magenta" },
  { value: "green", label: /* i18n */ "Green" },
  { value: "orange", label: /* i18n */ "Orange" },
];

// A small drawing of each interface, so the cards can be compared at a glance.
function Thumb({ name }: { name: InterfaceName }) {
  return (
    <span className={`iface-thumb iface-thumb-${name}`} aria-hidden="true">
      {name === "store" && (
        <>
          <i className="t-hero" />
          <i className="t-row" />
        </>
      )}
      {name === "crossbar" && (
        <>
          <i className="t-bar" />
          <i className="t-list" />
        </>
      )}
      {name === "cinema" && (
        <>
          <i className="t-title" />
          <i className="t-strip" />
        </>
      )}
      {name === "panorama" && (
        <>
          <i className="t-word" />
          <i className="t-tiles" />
        </>
      )}
    </span>
  );
}

// One setting as the designs draw it on the Settings page: its name and what it
// does on the left, the value (a menu) on the right.
function OptionRow({ label, note, value, options, change }: {
  label: string;
  note?: string;
  value: string;
  options: { value: string; label: string }[];
  change: (value: string) => void;
}) {
  return (
    <div className="appearance-row">
      <span className="appearance-row-text">
        <strong>{label}</strong>
        {note && <small>{note}</small>}
      </span>
      <Select label={label} value={value} options={options} onChange={change} />
    </div>
  );
}

export function Appearance({ page = false }: { page?: boolean }) {
  const { prefs } = useInterfacePrefs();
  if (page) return <AppearancePage />;
  return (
    <section
      className="storage-row appearance-settings"
      aria-labelledby="appearance-title"
    >
      <div>
        <strong id="appearance-title">{t("Appearance")}</strong>
        <p className="fine">
          {t(
            "Choose how Orbit looks and moves on this device. Your queue, favourites and settings stay the same.",
          )}
        </p>
      </div>
      <div
        className="iface-cards"
        role="radiogroup"
        aria-label={t("Interface")}
      >
        {interfaces.map((i) => (
          <button
            key={i.name}
            role="radio"
            aria-checked={prefs.name === i.name}
            className={`iface-card ${prefs.name === i.name ? "selected" : ""}`}
            onClick={() => chooseInterface(i.name)}
          >
            <Thumb name={i.name} />
            <span className="iface-name">
              {t(i.title)}
              {prefs.name === i.name && (
                <span className="iface-in-use">{t("In use")}</span>
              )}
            </span>
          </button>
        ))}
      </div>
      <div className="appearance-options">
        <div
          className="appearance-option"
          role="radiogroup"
          aria-label={t("Motion")}
        >
          <span>{t("Motion")}</span>
          {(["full", "reduced"] as const).map((motion) => (
            <button
              key={motion}
              role="radio"
              aria-checked={prefs.motion === motion}
              aria-pressed={prefs.motion === motion}
              onClick={() => setInterfacePrefs({ motion })}
            >
              {motion === "full" ? t("Full") : t("Reduced")}
            </button>
          ))}
        </div>
        {prefs.name === "crossbar" && (
          <div className="appearance-option" role="radiogroup" aria-label={t("Ribbons")}>
            <span>{t("Ribbons")}</span>
            {(Object.keys(ribbonTitles) as Ribbons[]).map((value) => (
              <button
                key={value}
                role="radio"
                aria-checked={prefs.ribbons === value}
                aria-pressed={prefs.ribbons === value}
                onClick={() => setInterfacePrefs({ ribbons: value })}
              >
                {t(ribbonTitles[value])}
              </button>
            ))}
          </div>
        )}
        {(prefs.name === "cinema" || prefs.name === "panorama") && (
          <div
            className="appearance-option"
            role="radiogroup"
            aria-label={prefs.name === "cinema" ? t("Art behind menus") : t("Art behind pages")}
          >
            <span>{prefs.name === "cinema" ? t("Art behind menus") : t("Art behind pages")}</span>
            {[true, false].map((on) => (
              <button
                key={String(on)}
                role="radio"
                aria-checked={prefs.artBehind === on}
                aria-pressed={prefs.artBehind === on}
                onClick={() => setInterfacePrefs({ artBehind: on })}
              >
                {on ? (prefs.name === "cinema" ? t("Blurred game art") : t("Faint game art")) : t("Plain black")}
              </button>
            ))}
          </div>
        )}
        {prefs.name === "panorama" && (
          <div
            className="appearance-option"
            role="radiogroup"
            aria-label={t("Accent colour")}
          >
            <span>{t("Accent colour")}</span>
            {accents.map((a) => (
              <button
                key={a.value}
                role="radio"
                aria-checked={prefs.accent === a.value}
                aria-pressed={prefs.accent === a.value}
                className={`accent-${a.value}`}
                onClick={() => setInterfacePrefs({ accent: a.value })}
              >
                <i aria-hidden="true" />
                {t(a.label)}
              </button>
            ))}
          </div>
        )}
      </div>
    </section>
  );
}

// Settings → Appearance in Crossbar, Cinema and Panorama: the page's title,
// the interface cards, then each option as a row.
function AppearancePage() {
  const { prefs } = useInterfacePrefs();
  const motion = (
    <OptionRow
      key="motion"
      label={t("Motion")}
      note={
        prefs.name === "crossbar"
          ? t("Reduce motion keeps the ribbons still")
          : prefs.name === "cinema"
            ? t("Reduced keeps the art still")
            : t("Reduced uses fades instead of slides")
      }
      value={prefs.motion}
      options={[
        { value: "full", label: t("Full") },
        { value: "reduced", label: t("Reduced") },
      ]}
      change={(value) => setInterfacePrefs({ motion: value === "reduced" ? "reduced" : "full" })}
    />
  );
  const art = (
    <OptionRow
      key="art"
      label={prefs.name === "cinema" ? t("Art behind menus") : t("Art behind pages")}
      note={prefs.name === "cinema" ? t("The last game you looked at, blurred") : undefined}
      value={prefs.artBehind ? "on" : "off"}
      options={[
        { value: "on", label: prefs.name === "cinema" ? t("Blurred game art") : t("Faint game art") },
        { value: "off", label: t("Plain black") },
      ]}
      change={(value) => setInterfacePrefs({ artBehind: value === "on" })}
    />
  );
  // Each interface's options in its design's order.
  const rows: ReactNode[] =
    prefs.name === "crossbar"
      ? [
          <OptionRow
            key="ribbons"
            label={t("Ribbons")}
            note={t("Crossbar's background")}
            value={prefs.ribbons}
            options={(Object.keys(ribbonTitles) as Ribbons[]).map((value) => ({ value, label: t(ribbonTitles[value]) }))}
            change={(value) => setInterfacePrefs({ ribbons: value as Ribbons })}
          />,
          motion,
        ]
      : prefs.name === "cinema"
        ? [motion, art]
        : [
            <OptionRow
              key="accent"
              label={t("Accent colour")}
              value={prefs.accent}
              options={accents.map((a) => ({ value: a.value, label: t(a.label) }))}
              change={(value) => setInterfacePrefs({ accent: value as Accent })}
            />,
            motion,
            art,
          ];
  return (
    <section className="appearance-page" aria-labelledby="appearance-title">
      <h1 className="settings-title" id="appearance-title">
        {t("Appearance")}
      </h1>
      <p className="appearance-intro">
        {t("Choose how Orbit looks and moves on this device. Your queue, favourites and settings stay the same.")}
      </p>
      <div className="iface-cards" role="radiogroup" aria-label={t("Interface")}>
        {interfaces.map((i) => (
          <button
            key={i.name}
            role="radio"
            aria-checked={prefs.name === i.name}
            className={`iface-card ${prefs.name === i.name ? "selected" : ""}`}
            onClick={() => chooseInterface(i.name)}
          >
            <Thumb name={i.name} />
            <span className="iface-name">
              {t(i.title)}
              {i.name === "store" && prefs.name !== "store" && <span className="iface-default">{t("Default")}</span>}
              {prefs.name === i.name && <span className="iface-in-use">{t("In use")}</span>}
            </span>
          </button>
        ))}
      </div>
      <div className="appearance-rows">{rows}</div>
    </section>
  );
}

// After switching, Orbit asks to keep the new interface and goes back to the
// previous one if there is no answer in 15 seconds.
export function KeepInterface() {
  const { pending, prefs } = useInterfacePrefs();
  const [now, setNow] = useState(Date.now());
  const dialog = useRef<HTMLDivElement>(null);
  const open = !!pending;
  useLayoutEffect(() => {
    const panel = dialog.current;
    if (!open || !panel) return;
    const previous = document.activeElement as HTMLElement | null;
    const overflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    // Isolate the prompt even when it is above another settings dialog.
    const siblings: [Element, string | null][] = [];
    for (let node: Element | null = panel; node?.parentElement; node = node.parentElement) {
      for (const sibling of node.parentElement.children) {
        if (sibling === node) continue;
        siblings.push([sibling, sibling.getAttribute("inert")]);
        sibling.setAttribute("inert", "");
      }
      if (node.parentElement === document.body) break;
    }
    const buttons = () => [...panel.querySelectorAll<HTMLButtonElement>("button:not([disabled])")];
    const focus = () => buttons()[0]?.focus({ preventScroll: true });
    const contain = () => { if (!panel.contains(document.activeElement)) focus(); };
    const trap = (event: KeyboardEvent) => {
      if (event.key !== "Tab") return;
      event.preventDefault();
      event.stopPropagation();
      const items = buttons();
      const index = items.indexOf(document.activeElement as HTMLButtonElement);
      items[(index + (event.shiftKey ? -1 : 1) + items.length) % items.length]?.focus();
    };
    focus();
    document.addEventListener("focusin", contain);
    document.addEventListener("keydown", trap, true);
    return () => {
      document.removeEventListener("focusin", contain);
      document.removeEventListener("keydown", trap, true);
      document.body.style.overflow = overflow;
      for (const [sibling, value] of siblings) {
        if (value === null) sibling.removeAttribute("inert");
        else sibling.setAttribute("inert", value);
      }
      requestAnimationFrame(() => {
        if (document.querySelector('[role="alertdialog"]')) return;
        const target = previous?.isConnected ? previous : document.querySelector<HTMLElement>('.iface-card.selected, .appearance-trigger, .main-nav [aria-current="page"]');
        target?.focus({ preventScroll: true });
      });
    };
  }, [open]);
  useEffect(() => {
    if (!pending) return;
    const tick = setInterval(() => setNow(Date.now()), 250);
    return () => clearInterval(tick);
  }, [pending]);
  useEffect(() => {
    if (pending && now >= pending.deadline) revertInterface();
  }, [pending, now]);
  if (!pending) return null;
  // The clock can be stale while this prompt is closed. Never show more than
  // its 15-second confirmation window before the first timer tick.
  const seconds = Math.max(0, Math.min(15, Math.ceil((pending.deadline - now) / 1000)));
  const name = interfaces.find((i) => i.name === prefs.name)?.title || "";
  const previous =
    interfaces.find((i) => i.name === pending.previous)?.title || "";
  return (
    <div
      ref={dialog}
      className="keep-interface"
      role="alertdialog"
      aria-modal="true"
      aria-labelledby="keep-title"
    >
      <div className="keep-interface-band">
        <h2 id="keep-title">{t("Keep this interface?")}</h2>
        <p>
          {t(
            "Orbit switches back to {name} in {seconds} s if you don’t answer.",
            {
              name: t(previous),
              seconds,
            },
          )}
        </p>
        <div className="dialog-actions">
          <button
            className="primary"
            data-initial-focus
            autoFocus
            onClick={keepInterface}
          >
            {t("Keep {name}", { name: t(name) })}
          </button>
          <button data-dialog-back onClick={revertInterface}>{t("Switch back")}</button>
        </div>
      </div>
    </div>
  );
}
