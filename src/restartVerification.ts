import type { UpdateStatus } from "./updateStatus";

const key = "orbit-service-restart-v1";
const lifetime = 2 * 60 * 60 * 1000;
export interface RestartExpectation {
  version: string;
  instance: string;
  requestedAt: number;
}
type StorageAccess = Pick<Storage, "getItem" | "setItem" | "removeItem">;
export function restartStorage(): StorageAccess {
  try { return sessionStorage; }
  catch { return { getItem: () => null, setItem: () => {}, removeItem: () => {} }; }
}

export function rememberRestart(status: UpdateStatus, storage: StorageAccess, now = Date.now()) {
  const expected: RestartExpectation = {
    version: status.savedVersion || status.runningVersion,
    instance: status.instanceId || "",
    requestedAt: now,
  };
  try { storage.setItem(key, JSON.stringify(expected)); } catch { /* Private browsing may block storage. */ }
  return expected;
}

export function readRestart(storage: StorageAccess, now = Date.now()): RestartExpectation | null {
  try {
    const value = JSON.parse(storage.getItem(key) || "null");
    if (value && typeof value.version === "string" && value.version.length <= 64 &&
        typeof value.instance === "string" && value.instance.length <= 64 &&
        Number.isFinite(value.requestedAt) && now >= value.requestedAt && now - value.requestedAt < lifetime)
      return value;
    storage.removeItem(key);
  } catch { /* Missing, corrupt or unavailable session storage is not a restart. */ }
  return null;
}

export function checkRestart(expected: RestartExpectation, status: UpdateStatus) {
  if (status.phase === "stopping" || (expected.instance && expected.instance === status.instanceId))
    return "previous";
  if (status.runningVersion !== expected.version) return "wrong-version";
  // Older servers have no process identity. A matching version alone cannot
  // prove that the stopped service was replaced (including same-version repair).
  if (!status.instanceId || !expected.instance) return "unverified";
  return "verified";
}

export function clearRestart(storage: StorageAccess) {
  try { storage.removeItem(key); } catch { /* Optional browser state. */ }
}
