import type { Release, System } from "./types";
import { t } from "./i18n";

type Facts = Pick<Release, "firmwareIssue" | "maxFirmware" | "requiredFirmware">;

// What a download option needs from the console's firmware, for its row in a list.
// Labels only: an option that won't run on this PS5 stays available, listed last.
export function firmwareLabel(r: Facts) {
  if (r.firmwareIssue === "ps5-package") return t("Needs firmware {version} or lower", { version: r.maxFirmware || "" });
  if (r.firmwareIssue === "newer-firmware") return t("Needs firmware {version} or later", { version: r.requiredFirmware || "" });
  return [
    r.requiredFirmware && t("Firmware {version} or later", { version: r.requiredFirmware }),
    r.maxFirmware && t("Firmware {version} or lower", { version: r.maxFirmware }),
  ]
    .filter(Boolean)
    .join(" · ");
}

// The sentence for the chosen option when it won't run on this PS5.
export function firmwareWarning(r: Facts, system: Pick<System, "firmware"> | null) {
  const current = system?.firmware || "";
  if (r.firmwareIssue === "ps5-package")
    return t("This download needs firmware {version} or lower. This PS5 is on {current}.", { version: r.maxFirmware || "", current });
  if (r.firmwareIssue === "newer-firmware")
    return t("This download needs firmware {version} or later. This PS5 is on {current}.", { version: r.requiredFirmware || "", current });
  return "";
}

// Options that run on this PS5 first, each group in catalogue order.
export function compatibleFirst<T extends Facts>(releases: T[]) {
  return [...releases.filter((r) => !r.firmwareIssue), ...releases.filter((r) => r.firmwareIssue)];
}
