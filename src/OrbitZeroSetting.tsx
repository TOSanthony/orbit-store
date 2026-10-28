import { useCallback, useEffect, useState } from "react";
import { api } from "./api";
import { t } from "./i18n";

type Companion = { configured: boolean; address?: string; enabled?: boolean };

// Orbit Zero, as the TV app's settings show it: the computer paired as a companion and
// whether Orbit Zero mode is on. Only Orbit on the PS5 itself may turn the mode on or off.
export function OrbitZeroSetting({ onConsole }: { onConsole: boolean }) {
  const [companion, setCompanion] = useState<Companion | null>(null),
    [pending, setPending] = useState(false),
    [error, setError] = useState("");
  const load = useCallback(async () => {
    try {
      setCompanion(await api<Companion>("/zero/companion"));
      setError("");
    } catch (e) {
      setError((e as Error).message);
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);
  const toggle = async () => {
    if (!companion) return;
    setPending(true);
    setError("");
    try {
      setCompanion(await api<Companion>("/zero/companion", { action: "enable", enabled: !companion.enabled }));
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setPending(false);
    }
  };
  return (
    <section className="archive-setting orbit-zero-setting">
      <h2 className="download-storage-heading">Orbit Zero</h2>
      <p>{t("Use your computer to download games and transfer them to this PS5 over Wi-Fi or Ethernet.")}</p>
      {onConsole && (
        <p className="fine">
          {t("Open Orbit Zero on your computer, connect to this console and enter the PS5 pairing code. Then enable it here.")}
        </p>
      )}
      {companion?.configured ? (
        <>
          <dl className="update-versions">
            <div>
              <dt>{t("Computer")}</dt>
              <dd>{companion.address}</dd>
            </div>
            <div>
              <dt>Orbit Zero</dt>
              <dd>{companion.enabled ? t("Enabled") : t("Off")}</dd>
            </div>
          </dl>
          {onConsole ? (
            <div className="dialog-actions">
              <button type="button" className="primary" disabled={pending} onClick={() => void toggle()}>
                {companion.enabled ? t("Turn off Orbit Zero") : t("Enable Orbit Zero")}
              </button>
            </div>
          ) : (
            <p className="fine">{t("Turn Orbit Zero on or off from Orbit on your PS5.")}</p>
          )}
        </>
      ) : (
        companion && <p>{t("No computer paired. Use Console setup in the desktop app to connect.")}</p>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
