import { useEffect, useState } from 'react';

/**
 * Bundles created in this browser session that are still being collected, by universe.
 *
 * The ready banner must outlive the Support Bundles modal (users are told they can close it), so
 * this lives at module scope rather than in the modal's state.
 */
const pendingByUniverse = new Map<string, Set<string>>();
const listeners = new Set<() => void>();

const notify = () => listeners.forEach((listener) => listener());

export const addPendingSupportBundle = (universeUuid: string, bundleUuid: string) => {
  pendingByUniverse.set(
    universeUuid,
    new Set([...(pendingByUniverse.get(universeUuid) ?? []), bundleUuid])
  );
  notify();
};

export const removePendingSupportBundle = (universeUuid: string, bundleUuid: string) => {
  const pending = new Set(pendingByUniverse.get(universeUuid));
  pending.delete(bundleUuid);
  pendingByUniverse.set(universeUuid, pending);
  notify();
};

export const usePendingSupportBundles = (universeUuid: string): string[] => {
  const read = () => Array.from(pendingByUniverse.get(universeUuid) ?? []);
  const [pending, setPending] = useState(read);
  useEffect(() => {
    const listener = () => setPending(read());
    listeners.add(listener);
    listener();
    return () => {
      listeners.delete(listener);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [universeUuid]);
  return pending;
};
