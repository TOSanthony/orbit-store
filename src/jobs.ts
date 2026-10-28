// What a download is doing and what can be done with it, shared by every
// layout of Downloads (Store's list and the other interfaces' views).
import type { Job, Storage } from "./types";

export const statusLabels: Record<Job["status"], string> = {
  queued: /* i18n */ "Queued",
  downloading: /* i18n */ "Downloading",
  retrying: /* i18n */ "Retrying",
  verifying: /* i18n */ "Verifying",
  inspecting: /* i18n */ "Inspecting archive",
  extracting: /* i18n */ "Extracting",
  paused: /* i18n */ "Paused",
  error: /* i18n */ "Needs attention",
  complete: /* i18n */ "Complete",
  cancelled: /* i18n */ "Cancelled",
};
export type DownloadView = "Active" | "Finished" | "Failed" | "Cancelled";
export const downloadViews: DownloadView[] = [
  /* i18n */ "Active",
  /* i18n */ "Finished",
  /* i18n */ "Failed",
  /* i18n */ "Cancelled",
];
export const inactive = (job: Job) => ["complete", "cancelled"].includes(job.status);
export const waiting = (job: Job) => ["queued", "paused", "retrying"].includes(job.status);
export const running = (job: Job) =>
  ["queued", "downloading", "retrying", "verifying", "inspecting", "extracting"].includes(job.status);

// While extracting, progress is the extraction's.
export function progressOf(job: Job) {
  const extracting = job.status === "extracting";
  const done = extracting ? job.extracted || 0 : job.received;
  const total = extracting ? job.extractTotal || 0 : job.total;
  return { extracting, done, total, percent: total > 0 ? Math.min(100, Math.max(0, (100 * done) / total)) : 0 };
}

export type JobCommand =
  | "pause"
  | "resume"
  | "retry"
  | "cancel-dialog"
  | "password"
  | "refresh-link"
  | "move-up"
  | "move-down"
  | "remove"
  | "packages";
export interface JobAction {
  command: JobCommand;
  label: string; // untranslated, for t()
  disabled: boolean;
  primary?: boolean;
}

// The same buttons, in the same order and with the same conditions, as a row
// in Store's Downloads list.
export function jobActions(job: Job, { busy, drives, queued }: { busy: boolean; drives: Storage[]; queued: Job[] }): JobAction[] {
  const actions: JobAction[] = [];
  if (job.status === "complete") actions.push({ command: "packages", label: job.installation?.packages.some(p => ["ready", "error"].includes(p.state)) ? /* i18n */ "Install on PS5" : /* i18n */ "Downloaded files", disabled: busy, primary: true });
  const active = running(job);
  const drive = drives.find((d) => d.id === job.storageId);
  if (job.canEnterArchivePassword)
    actions.push({ command: "password", label: /* i18n */ "Enter archive password", disabled: busy || !drive });
  if (job.status !== "complete") {
    actions.push({
      command: active ? "pause" : job.status === "error" ? "retry" : "resume",
      label: active
        ? /* i18n */ "Pause"
        : job.status === "error"
          ? /* i18n */ "Retry download"
          : job.status === "cancelled" && !job.received
            ? /* i18n */ "Start again"
            : /* i18n */ "Resume",
      disabled: busy || (!active && !drive),
      primary: true,
    });
    actions.push({
      command: "cancel-dialog",
      label: job.status === "cancelled" ? /* i18n */ "Delete download" : /* i18n */ "Cancel",
      disabled: busy,
    });
  }
  if (job.canRefreshLink) actions.push({ command: "refresh-link", label: /* i18n */ "Refresh download link", disabled: busy });
  if (waiting(job)) {
    const position = queued.findIndex((q) => q.id === job.id);
    actions.push({ command: "move-up", label: /* i18n */ "↑ Move up", disabled: busy || position <= 0 });
    actions.push({ command: "move-down", label: /* i18n */ "↓ Move down", disabled: busy || position === queued.length - 1 });
  }
  if (inactive(job) && (job.status === "complete" || job.received === 0))
    actions.push({ command: "remove", label: /* i18n */ "Remove from history", disabled: busy });
  return actions;
}
