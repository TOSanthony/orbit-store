import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "./api";
import type { FileEntry, FileListing, FileOperation } from "./types";

export type FileSort = "name" | "size" | "date";
// A copy or move waiting for its destination: open a folder, then Paste here.
export interface FileClip {
  action: "copy" | "move";
  storageId: string;
  path: string;
  name: string;
}
// The dialogs every look shares: they ask for a name, a password or a yes.
export type FileDialog =
  | { kind: "rename"; entry: FileEntry }
  | { kind: "folder" }
  | { kind: "delete"; entry: FileEntry }
  | { kind: "password" }
  | null;
const join = (path: string, name: string) => (path ? `${path}/${name}` : name);
const ended = (o: FileOperation | null) =>
  !o || ["complete", "failed", "cancelled"].includes(o.state);

// One drive and folder at a time; the list, the running operation and the
// clipboard are shared by every look. storageId "" lists the drives.
export function useFiles(paired: boolean, active: boolean) {
  const [storageId, setStorageId] = useState("");
  const [path, setPath] = useState("");
  const [listing, setListing] = useState<FileListing | null>(null);
  const [operation, setOperation] = useState<FileOperation | null>(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [sort, setSort] = useState<FileSort>("name");
  const [query, setQuery] = useState("");
  const [clip, setClip] = useState<FileClip | null>(null);
  const [dialog, setDialog] = useState<FileDialog>(null);
  const [sizes, setSizes] = useState<Record<string, number>>({});
  // The operation dialog: open for one this device started, until hidden or done.
  const [shownId, setShownId] = useState("");
  const [doneId, setDoneId] = useState("");
  const polling = useRef(false);
  const operationRef = useRef(operation);
  operationRef.current = operation;
  const finished = ended(operation);
  const place = useRef({ storageId, path });
  place.current = { storageId, path };
  const list = useCallback(async (drive: string, folder: string) => {
    if (!drive) {
      setListing(null);
      return;
    }
    setLoading(true);
    try {
      const next = await api<FileListing>("/files/list", { storageId: drive, path: folder });
      if (place.current.storageId === drive && place.current.path === folder) {
        setListing(next);
        setError("");
      }
    } catch (e) {
      if (place.current.storageId === drive && place.current.path === folder)
        setError((e as Error).message);
    } finally {
      if (place.current.storageId === drive && place.current.path === folder) setLoading(false);
    }
  }, []);
  const open = useCallback(
    (drive: string, folder = "") => {
      setStorageId(drive);
      setPath(folder);
      setQuery("");
      setListing(null);
      setError("");
      place.current = { storageId: drive, path: folder };
      void list(drive, folder);
    },
    [list],
  );
  const up = useCallback(() => {
    if (!storageId) return false;
    if (!path) open("");
    else open(storageId, path.includes("/") ? path.slice(0, path.lastIndexOf("/")) : "");
    return true;
  }, [open, path, storageId]);
  const refresh = useCallback(() => void list(place.current.storageId, place.current.path), [list]);
  const status = useCallback(async () => {
    if (polling.current) return;
    polling.current = true;
    try {
      const next = await api<{ operation: FileOperation | null }>("/files/status");
      const previous = operationRef.current;
      const o = next.operation;
      operationRef.current = o;
      setOperation(o);
      if (previous && !ended(previous) && ended(o)) refresh();
      if (o?.action === "measure" && o.state === "complete" && o.sizeBytes !== null) {
        const key = `${o.storageId}:${o.path}`;
        const size = o.sizeBytes;
        setSizes((known) => (known[key] === size ? known : { ...known, [key]: size }));
      }
    } catch {
      /* The list shows the error; a missed poll is retried. */
    } finally {
      polling.current = false;
    }
  }, [refresh]);
  useEffect(() => {
    if (!paired || !active) return;
    void status();
    const timer = setInterval(() => void status(), finished ? 4000 : 1000);
    return () => clearInterval(timer);
  }, [paired, active, finished, status]);
  const run = useCallback(
    async (action: string, fields: Record<string, unknown> = {}) => {
      setSubmitting(true);
      try {
        const next = await api<{ operation: FileOperation | null }>("/files/action", {
          storageId: place.current.storageId,
          path: place.current.path,
          ...fields,
          action,
        });
        operationRef.current = next.operation;
        setOperation(next.operation);
        if (next.operation && next.operation.action !== "measure" && ["copy", "move", "delete", "unpack", "install"].includes(action))
          setShownId(next.operation.id);
        if (["rename", "folder"].includes(action)) refresh();
      } finally {
        setSubmitting(false);
      }
    },
    [refresh],
  );
  // A one-time link the browser saves the file from.
  const save = useCallback(async (entry: FileEntry) => {
    const next = await api<{ url: string }>("/files/link", {
      storageId: place.current.storageId,
      path: join(place.current.path, entry.name),
    });
    const link = document.createElement("a");
    link.href = next.url;
    link.download = entry.name;
    document.body.append(link);
    link.click();
    link.remove();
  }, []);
  const entries = (listing?.entries || [])
    .filter((e) => !query.trim() || e.name.toLowerCase().includes(query.trim().toLowerCase()))
    .sort(
      (a, b) =>
        Number(b.folder) - Number(a.folder) ||
        (sort === "size"
          ? (b.sizeBytes ?? -1) - (a.sizeBytes ?? -1)
          : sort === "date"
            ? b.modifiedAt - a.modifiedAt
            : 0) ||
        a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }),
    );
  // A folder's measuring is quick and shows only as its size.
  const activity = operation && operation.action !== "measure" && operation.id !== doneId ? operation : null;
  return {
    storageId,
    path,
    listing,
    entries,
    operation,
    activity,
    activityOpen: !!activity && shownId === activity.id,
    showActivity: () => activity && setShownId(activity.id),
    // Hide keeps a running operation's status line; closing a finished one clears it.
    hideActivity: () => {
      if (activity && ended(activity)) setDoneId(activity.id);
      setShownId("");
    },
    running: !ended(operation),
    error,
    loading,
    submitting,
    sort,
    setSort,
    query,
    setQuery,
    clip,
    setClip,
    dialog,
    setDialog,
    open,
    up,
    refresh,
    run,
    save,
    // The entry's own path, and a folder's measured size.
    pathOf: (entry: FileEntry) => join(path, entry.name),
    sizeOf: (entry: FileEntry) =>
      entry.sizeBytes ?? (entry.folder ? (sizes[`${storageId}:${join(path, entry.name)}`] ?? null) : null),
  };
}
export type FilesModel = ReturnType<typeof useFiles>;
