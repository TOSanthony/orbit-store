import type { Installation } from "./Installation";
import { formatNumber, t } from "./i18n";
export interface AddonRequirements {
  titleIds: string[];
  edition?: string | null;
  minimumVersion?: string | null;
  notes?: string | null;
}
export interface AddonIdentity {
  addonId?: string;
  baseGameId?: string;
  baseGameTitle?: string;
  addonType?: "expansion" | "content-pack" | "activation" | "bundle";
  contentId?: string;
  requirements?: AddonRequirements;
  contents?: string[];
}
export interface Release extends AddonIdentity {
  platform?: "ps4" | "ps5";
  id: string;
  gameId: string;
  titleId: string;
  title: string;
  genre: string | null;
  tagline: string | null;
  description: string | null;
  sizeBytes: number;
  provider: string;
  sourceId: SourceId;
  format: "FFPFSC" | "exFAT" | "FPKG" | "PKG" | "Game folder" | "Unknown";
  // Downloaded as an archive and extracted on the drive; format is what's inside.
  compression?: "zip" | "7z" | "rar";
  archivePassword?: string;
  extractedBytes?: number;
  archiveContents?: "unverified";
  notes?: string;
  region?: "EUR" | "USA" | "JPN" | "ASIA" | null;
  version: string | null;
  filename: string;
  cover: string;
  hero: string;
  coverFallback?: string | null;
  heroFallback?: string | null;
  artworkLayout: "wide" | "ambient";
  publisher: string | null;
  releaseDate: string | null;
  addedAt?: string | null;
  newOnOrbitRank?: number | null;
  tags?: string[]; // Browse tags, as browse.ts TAGS names them
  consoleVerified: boolean;
  browserAvailable?: boolean;
  directAvailable?: boolean;
  /* The firmware an option needs, from the console's service; see src/firmware.ts. */
  requiredFirmware?: string;
  maxFirmware?: string;
  firmwareIssue?: "ps5-package" | "newer-firmware";
}
export interface Game extends AddonIdentity {
  platform?: "ps4" | "ps5";
  id: string;
  title: string;
  genre: string | null;
  tagline: string | null;
  description: string | null;
  cover: string;
  hero: string;
  coverFallback?: string | null;
  heroFallback?: string | null;
  artworkLayout: "wide" | "ambient";
  publisher: string | null;
  releaseDate: string | null;
  addedAt: string | null;
  newOnOrbitRank?: number | null;
  releases: Release[];
}
export type SourceId =
  "vikingfile" | "archive" | "akirabox" | "datanodes" | "1fichier" | "nas";
export interface SourceSettings {
  enabled: SourceId[];
  acknowledged: boolean;
  noticeVersion: number;
  nasConfigured?: boolean;
  nasHost?: string;
  nasCount?: number;
  nasCheckedAt?: number;
  nasBusy?: boolean;
  nasError?: string;
  options: {
    id: SourceId;
    label: string;
    releaseCount: number;
    downloadReady: boolean;
  }[];
}
// The one game update being downloaded and installed (the `job` of GET /api/v1/game-updates).
export interface GameUpdateJob {
  titleId: string;
  name: string;
  version: string;
  phase:
    | "preparing"
    | "downloading"
    | "verifying"
    | "paused"
    | "installing"
    | "installed"
    | "downloaded"
    | "failed"
    | "cancelled";
  active: boolean;
  done: number;
  total: number;
  speed: number;
  installPercent: number;
  waiting?: boolean; // installing: the console has the package and is finishing
  error: string;
  detail: string;
}
// An installed game with a newer patch on Sony's servers (GET /api/v1/game-updates).
export interface GameUpdate {
  titleId: string;
  name: string;
  installed: string;
  version: string;
  state: "available" | "needs-firmware";
  firmware?: string;
  size?: number; // bytes, when Sony's list gave it
  // A DLC (the `dlc` list): titleId is its key "<game>/<label>"; these name the game and the pack.
  baseId?: string;
  gameName?: string;
  label?: string;
}

export interface Storage {
  id: string;
  label: string;
  path: string;
  freeBytes: number;
  totalBytes: number;
  pendingBytes: number;
  projectedFreeBytes: number;
  external: boolean;
}
export interface Job extends AddonIdentity {
  installation?: Installation | null;
  delivery?: "direct" | "torbox";
  deliveryPhase?: string;
  titleId: string;
  gameId: string;
  format: string;
  path: string;
  title: string;
  order: number;
  id: string;
  releaseId: string;
  storageId: string;
  filename: string;
  status:
    | "queued"
    | "downloading"
    | "paused"
    | "retrying"
    | "verifying"
    | "inspecting"
    | "extracting"
    | "complete"
    | "cancelled"
    | "error";
  received: number;
  total: number;
  speed: number;
  retryAt: number;
  error: string;
  verification: string;
  canRefreshLink?: boolean;
  installState?: string; // an update or DLC: "installing" while the console installs it
  compression?: string;
  canEnterArchivePassword?: boolean;
  extracted?: number;
  extractTotal?: number;
  detectedFormat?: string;
  output?: string;
  archiveKept?: boolean;
}
export interface LibraryGame {
  titleId: string;
  title: string;
  platform: "ps5" | "ps4" | "unknown";
  format: string;
  path: string;
  runtimePath: string;
  sourceKey: string;
  location: string;
  installed: boolean;
  mounted: boolean;
  onDrive: boolean;
  managed: boolean;
  canManageSource: boolean;
  installedSizeBytes: number | null;
  sizeBytes: number | null;
  sizeStatus: "ready" | "unknown" | "unavailable";
  cover?: string;
  coverFallback?: string | null;
  /** Listed by Orbit because the running ShadowMount doesn't list installed PKGs. */
  foundByOrbit?: boolean;
}
export interface LibraryAction {
  id: string;
  action: string;
  titleId: string;
  state: "queued" | "running" | "complete" | "error";
  message: string;
}
export interface StorageJob {
  id: number;
  operation: string;
  state: string;
  titleId: string;
  source: string;
  destination: string;
  error: string;
  result: number;
  totalBytes: number;
  processedBytes: number;
  speed: number;
  active: boolean;
  cancellable: boolean;
  cancelRequested: boolean;
}
export interface LibrarySnapshot {
  status: "idle" | "ready" | "unavailable" | "unsupported" | "error";
  message: string;
  provider: string;
  providerVersion: string;
  busy: boolean;
  stale: boolean;
  checkedAt: number;
  updatedAt: number;
  refreshAfter: number;
  games: LibraryGame[];
  capabilities: string[];
  action: LibraryAction | null;
  storageJob: StorageJob | null;
  storageError: string;
  storageBusy: boolean;
  // Automatic work: a scan ShadowMount is running, game sizes being measured.
  scanning?: boolean;
  measuring?: boolean;
}
export interface LibraryDrive {
  id: string;
  path: string;
  mountPoint: string;
  label: string;
  filesystem: string;
  readOnly: boolean;
  totalBytes: number;
  freeBytes: number;
  usedBytes: number;
}
// Files: what is on the PS5's drives, by drive and path relative to its top.
export type FileType = "folder" | "game" | "archive" | "package" | "image" | "file";
export interface FileEntry {
  name: string;
  folder: boolean;
  type: FileType;
  // Files only; a folder's size is measured on request.
  sizeBytes: number | null;
  modifiedAt: number;
  // Why it can't be changed: "", or drive, orbit, download, game, install or busy.
  locked: string;
}
export interface FileListing {
  storageId: string;
  path: string;
  entries: FileEntry[];
  // More entries than Orbit lists in one folder.
  truncated: boolean;
}
export interface FileOperation {
  id: string;
  action: "copy" | "move" | "delete" | "unpack" | "install" | "measure";
  // password: an archive needs one that Orbit doesn't have.
  state: "measuring" | "running" | "paused" | "password" | "complete" | "failed" | "cancelled";
  name: string;
  storageId: string;
  path: string;
  toStorageId: string;
  toPath: string;
  processedBytes: number;
  totalBytes: number;
  speed: number;
  error: string;
  // Measure: the folder's size.
  sizeBytes: number | null;
}
export interface LibraryStorage {
  drives: LibraryDrive[];
  destinations: LibraryDrive[];
  busy: boolean;
  stale: boolean;
  updatedAt: number;
  error: string;
}
export interface LibraryTarget {
  titleId?: string;
  path?: string;
  title: string;
  request: number;
}
export interface System {
  catalogueRevision: number;
  name: string;
  version: string;
  platform: "desktop" | "ps5" | "fixture";
  paired: boolean;
  stateHealthy: boolean;
  preferredStorage: string;
  deleteArchives?: boolean;
  downloadNotifications?: boolean;
  notificationEvents?: Record<NotificationEvent, boolean>;
  deletePackages?: boolean;
  autoInstallPackages?: boolean;
  ntfyEnabled?: boolean;
  concurrentDownloads?: number;
  downloadConnections?: number;
  downloadRetries?: number;
  downloadRetryDelay?: number;
  downloadStallTimeout?: number;
  downloadMinSpeed?: number;

  consoleValidated: boolean;
  targetFirmware: string;
  firmware?: string;
  ps5PackageFirmware?: string;
  localSessionAvailable: boolean;
  pairNotificationAvailable: boolean;
  httpPort: number;
  launcherStatus: "checking" | "ready" | "error" | "not-included" | "tv-app";
}
export interface AutoStart {
  available: boolean;
  savedPath?: string;
  saved?: boolean;
  preferencesError?: string;
  payloadManager?: {
    installed: boolean;
    listed: boolean;
    managed: boolean;
    enabled: boolean;
    switchOn: boolean;
    otherEntries: number;
  };
  autoloadTxt?: { files: { path: string; enabled: boolean }[] };
  homebrewLauncher?: { installed: boolean; listed: boolean; managed: boolean };
  etaHEN?: { installed: boolean };
}
// "7z · exFAT" for a compressed disk image, "ZIP · Game" for an archive of a game
// folder (an archive can only hold one), otherwise the format.
export function formatLabel(r: Pick<Release, "format" | "compression">) {
  if (!r.compression) return r.format;
  const kind = r.compression === "7z" ? "7z" : r.compression.toUpperCase();
  if (r.format === "Unknown") return kind;
  return `${kind} · ${r.format === "Game folder" ? t("Game") : r.format}`;
}
const tenths = { minimumFractionDigits: 1, maximumFractionDigits: 1 };
// 49.4 GB in English, 49,4 GB in German, 49,4 ГБ in Russian, 49,4 Go in French.
export const bytes = (n: number) => {
  const giga = n >= 1e9;
  const value = giga ? n / 1e9 : n / 1e6;
  try {
    return formatNumber(value, {
      style: "unit",
      unit: giga ? "gigabyte" : "megabyte",
      unitDisplay: "short",
      ...tenths,
    });
  } catch {
    // Browsers without unit formatting keep the English abbreviation.
    return `${formatNumber(value, tenths)} ${giga ? "GB" : "MB"}`;
  }
};

export interface BrowserSession {
  accountPage?: boolean;
  vikingAccountAvailable?: boolean;
  vikingBrowserState?: "signed-in" | "signed-out" | "unknown";
  vikingNativeState?: "signed-in" | "signed-out" | "unknown";
  available: boolean;
  active: boolean;
  id: string;
  title: string;
  state:
    | "idle"
    | "opening"
    | "waiting"
    | "waiting-link"
    | "validating"
    | "queued"
    | "failed"
    | "cancelled";
  message: string;
  jobId: string;
}
// What Orbit can notify about, on the PS5 and through ntfy (/settings/notifications).
export type NotificationEvent = "downloadFinished" | "downloadFailed" | "installFinished" | "installFailed" | "updateAvailable";
