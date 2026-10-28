export interface UpdateStatus {
  available: boolean;
  updateAvailable: boolean;
  automaticCheckAfter: number;
  runningVersion: string;
  instanceId?: string;
  savedVersion: string;
  latestVersion: string;
  checksum: string;
  phase: string;
  busy: boolean;
  error: string;
  restartRequired: boolean;
  received: number;
  retryAt: number;
  checkedAt: number;
  checkAfter: number;
  /* Orbit restarts itself through the ELF loader and installs both parts in one step. */
  selfRestart?: boolean;
  updateAll?: boolean;
  tvAppUpdated?: string;
}
// The TV app's installer, as /tv-app reports it.
export interface TvAppStatus {
  available: boolean;
  installed: "none" | "orbit" | "manual" | "folder" | "blocked";
  installedVersion: string;
  latestVersion: string;
  checksum: string;
  size: number;
  updateAvailable: boolean;
  phase: string;
  error: string;
  busy: boolean;
  received: number;
  retryAt: number;
  checkedAt: number;
  checkAfter: number;
}
