// Which interface Orbit draws on this device. Store is the default; the others
// are different layouts over the same screens, actions and settings. The
// choice is saved in this browser only, so changing it never touches Orbit's
// service or another device.
import { useEffect, useState } from "react";

export type InterfaceName = "store" | "crossbar" | "cinema" | "panorama";
export type Motion = "full" | "reduced";
export type Accent = "blue" | "magenta" | "green" | "orange";
// Crossbar's background: the time of day, or one of its colours.
export type Ribbons = "time" | "dawn" | "day" | "dusk" | "night";

export interface InterfacePrefs {
  name: InterfaceName;
  motion: Motion;
  accent: Accent; // Panorama
  ribbons: Ribbons; // Crossbar
  artBehind: boolean; // Cinema and Panorama: the last game's art behind pages
}

export const interfaces: {
  name: InterfaceName;
  title: string;
}[] = [
  {
    name: "store",
    title: /* i18n */ "Store",
  },
  {
    name: "crossbar",
    title: /* i18n */ "Crossbar",
  },
  {
    name: "cinema",
    title: /* i18n */ "Cinema",
  },
  {
    name: "panorama",
    title: /* i18n */ "Panorama",
  },
];

const storageKey = "orbit.interface.v1";
const defaults: InterfacePrefs = {
  name: "store",
  motion: "full",
  accent: "blue",
  ribbons: "time",
  artBehind: true,
};
const ribbonChoices: Ribbons[] = ["time", "dawn", "day", "dusk", "night"];

function read(): InterfacePrefs {
  try {
    const saved = JSON.parse(
      localStorage.getItem(storageKey) || "{}",
    ) as Partial<InterfacePrefs>;
    return {
      name: interfaces.some((i) => i.name === saved.name)
        ? saved.name!
        : defaults.name,
      motion: saved.motion === "reduced" ? "reduced" : "full",
      accent: (["blue", "magenta", "green", "orange"] as const).includes(
        saved.accent as Accent,
      )
        ? (saved.accent as Accent)
        : "blue",
      ribbons: ribbonChoices.includes(saved.ribbons as Ribbons) ? (saved.ribbons as Ribbons) : "time",
      artBehind: saved.artBehind !== false,
    };
  } catch {
    return defaults;
  }
}

function write(prefs: InterfacePrefs) {
  try {
    localStorage.setItem(storageKey, JSON.stringify(prefs));
  } catch {
    // Private browsing or a full store: the choice lasts for this visit.
  }
}

// After switching, the previous interface is kept until the person confirms.
export interface PendingSwitch {
  previous: InterfaceName;
  deadline: number;
}

interface State {
  prefs: InterfacePrefs;
  pending: PendingSwitch | null;
}
const listeners = new Set<(state: State) => void>();
let state: State = { prefs: read(), pending: null };

function publish(next: State) {
  state = next;
  for (const listen of listeners) listen(state);
}

// The current choice, outside React (initial state that depends on it).
export function readInterfacePrefs(): InterfacePrefs {
  return state.prefs;
}

export function setInterfacePrefs(next: Partial<InterfacePrefs>) {
  const prefs = { ...state.prefs, ...next };
  write(prefs);
  publish({ ...state, prefs });
}

export function chooseInterface(name: InterfaceName) {
  if (name === state.prefs.name) return;
  const previous = state.prefs.name;
  const prefs = { ...state.prefs, name };
  write(prefs);
  publish({ prefs, pending: { previous, deadline: Date.now() + 15_000 } });
}

export function keepInterface() {
  publish({ ...state, pending: null });
}

export function revertInterface() {
  if (!state.pending) return;
  const prefs = { ...state.prefs, name: state.pending.previous };
  write(prefs);
  publish({ prefs, pending: null });
}

export function useInterfacePrefs() {
  const [current, setCurrent] = useState(state);
  useEffect(() => {
    listeners.add(setCurrent);
    setCurrent(state);
    return () => {
      listeners.delete(setCurrent);
    };
  }, []);
  useEffect(() => {
    const root = document.documentElement;
    root.dataset.interface = current.prefs.name;
    root.dataset.motion = current.prefs.motion;
    root.dataset.accent = current.prefs.accent;
    root.dataset.ribbons = ribbonsNow(current.prefs.ribbons);
    root.dataset.artBehind = current.prefs.artBehind ? "on" : "off";
  }, [current.prefs]);
  return current;
}

// The colour Crossbar's ribbons use now: dawn 5–9, day 9–17, dusk 17–21, night.
export function ribbonsNow(choice: Ribbons, hour = new Date().getHours()): Exclude<Ribbons, "time"> {
  if (choice !== "time") return choice;
  return hour >= 5 && hour < 9 ? "dawn" : hour >= 9 && hour < 17 ? "day" : hour >= 17 && hour < 21 ? "dusk" : "night";
}
export const ribbonTitles: Record<Ribbons, string> = {
  time: /* i18n */ "Follow the time of day",
  dawn: /* i18n */ "Dawn",
  day: /* i18n */ "Day",
  dusk: /* i18n */ "Dusk",
  night: /* i18n */ "Night",
};
