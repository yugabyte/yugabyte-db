import { useEffect, useState } from 'react';

import { SupportBundle } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

interface TrackedSupportBundles {
  /** Created in this browser session and still being collected. */
  pending: string[];
  /** Collected, and not yet shared or dismissed. */
  ready: SupportBundle[];
}

/**
 * Bundles created in this browser session, by universe.
 *
 * The ready banner must outlive the Support Bundles modal and the universe page's own remounts
 * (it shows a loader while the universe reloads), so this lives at module scope.
 */
const trackedByUniverse = new Map<string, TrackedSupportBundles>();
const listeners = new Set<() => void>();

const read = (universeUuid: string): TrackedSupportBundles =>
  trackedByUniverse.get(universeUuid) ?? { pending: [], ready: [] };

const update = (
  universeUuid: string,
  change: (tracked: TrackedSupportBundles) => TrackedSupportBundles
) => {
  trackedByUniverse.set(universeUuid, change(read(universeUuid)));
  listeners.forEach((listener) => listener());
};

export const addPendingSupportBundle = (universeUuid: string, bundleUuid: string) =>
  update(universeUuid, ({ pending, ready }) => ({
    pending: [...pending.filter((uuid) => uuid !== bundleUuid), bundleUuid],
    ready
  }));

export const markSupportBundleReady = (
  universeUuid: string,
  bundleUuid: string,
  bundle: SupportBundle
) =>
  update(universeUuid, ({ pending, ready }) => ({
    pending: pending.filter((uuid) => uuid !== bundleUuid),
    ready: [...ready.filter((current) => current.info?.uuid !== bundle.info?.uuid), bundle]
  }));

/** Stops tracking a bundle: it failed, is gone, or was shared or dismissed. */
export const forgetSupportBundle = (universeUuid: string, bundleUuid: string) =>
  update(universeUuid, ({ pending, ready }) => ({
    pending: pending.filter((uuid) => uuid !== bundleUuid),
    ready: ready.filter((bundle) => bundle.info?.uuid !== bundleUuid)
  }));

/** Tests only: the store is module state, so it outlives each test's render. */
export const resetTrackedSupportBundles = () => {
  trackedByUniverse.clear();
  listeners.forEach((listener) => listener());
};

export const useTrackedSupportBundles = (universeUuid: string): TrackedSupportBundles => {
  const [tracked, setTracked] = useState(() => read(universeUuid));
  useEffect(() => {
    const listener = () => setTracked(read(universeUuid));
    listeners.add(listener);
    listener();
    return () => {
      listeners.delete(listener);
    };
  }, [universeUuid]);
  return tracked;
};
