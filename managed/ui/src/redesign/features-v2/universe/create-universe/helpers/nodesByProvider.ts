import { ProviderType } from '../steps/general-settings/dtos';
import { NodeAvailabilityProps, Zone } from '../steps/nodes-availability/dtos';
import { Region } from '../../../../features/universe/universe-form/utils/dto';
import { REPLICATION_FACTOR } from '../fields/FieldNames';

/** Form map: provider uuid → region code → zones (RF/dedicated stay on singular nodes slice). */
export type AvailabilityZonesByProvider = Record<
  string,
  NodeAvailabilityProps['availabilityZones']
>;

export function emptyNodesSlice(): NodeAvailabilityProps {
  return {
    availabilityZones: {},
    useDedicatedNodes: false
  };
}

export function countZonesInByProvider(
  byProvider: AvailabilityZonesByProvider | null | undefined
): number {
  if (!byProvider) return 0;
  let count = 0;
  for (const zonesByRegion of Object.values(byProvider)) {
    for (const zones of Object.values(zonesByRegion ?? {})) {
      count += zones?.length ?? 0;
    }
  }
  return count;
}

export function flattenZonesList(
  byProvider: AvailabilityZonesByProvider | null | undefined
): Zone[] {
  if (!byProvider) return [];
  return Object.values(byProvider).flatMap((zonesByRegion) =>
    Object.values(zonesByRegion ?? {}).flatMap((zones) => zones ?? [])
  );
}

export function getNodeCountFromByProvider(
  byProvider: AvailabilityZonesByProvider | null | undefined
): number {
  return flattenZonesList(byProvider).reduce((sum, zone) => sum + (zone?.nodeCount ?? 0), 0);
}

/** Merge per-provider AZ maps by region code (payload / review readers). */
export function mergeAvailabilityZonesFromNodesByProvider(
  nodesByProvider?: Record<string, NodeAvailabilityProps> | null
): NodeAvailabilityProps['availabilityZones'] {
  if (!nodesByProvider) return {};
  const merged: NodeAvailabilityProps['availabilityZones'] = {};
  for (const slice of Object.values(nodesByProvider)) {
    for (const [regionCode, zones] of Object.entries(slice?.availabilityZones ?? {})) {
      merged[regionCode] = zones ?? [];
    }
  }
  return merged;
}

/**
 * Multi: prefer nodesByProvider when it has any regions; else singular availabilityZones.
 * N=1 keeps the singular map.
 */
export function getEffectiveAvailabilityZones(
  nodesAvailabilitySettings?: Pick<NodeAvailabilityProps, 'availabilityZones'> | null,
  nodesByProvider?: Record<string, NodeAvailabilityProps> | null
): NodeAvailabilityProps['availabilityZones'] {
  const fromByProvider = mergeAvailabilityZonesFromNodesByProvider(nodesByProvider);
  if (Object.keys(fromByProvider).length > 0) return fromByProvider;
  return nodesAvailabilitySettings?.availabilityZones ?? {};
}

/** Per-provider node totals (provider uuid → sum of zone nodeCounts). */
export function getNodeCountsByProvider(
  byProvider: AvailabilityZonesByProvider | null | undefined
): Record<string, number> {
  if (!byProvider) return {};
  const counts: Record<string, number> = {};
  for (const [providerUuid, zonesByRegion] of Object.entries(byProvider)) {
    counts[providerUuid] = Object.values(zonesByRegion ?? {}).reduce(
      (sum, zones) => sum + (zones ?? []).reduce((zSum, zone) => zSum + (zone?.nodeCount ?? 0), 0),
      0
    );
  }
  return counts;
}

/** Flat map for validators / inferResilience that only need zone lists by arbitrary keys. */
export function flattenAvailabilityZonesByProvider(
  byProvider: AvailabilityZonesByProvider | null | undefined
): NodeAvailabilityProps['availabilityZones'] {
  if (!byProvider) return {};
  const flat: NodeAvailabilityProps['availabilityZones'] = {};
  for (const [providerUuid, zonesByRegion] of Object.entries(byProvider)) {
    for (const [regionCode, zones] of Object.entries(zonesByRegion ?? {})) {
      flat[`${providerUuid}:${regionCode}`] = zones ?? [];
    }
  }
  return flat;
}

/** Inverse of {@link flattenAvailabilityZonesByProvider}. */
export function unflattenAvailabilityZonesByProvider(
  flat: NodeAvailabilityProps['availabilityZones'] | null | undefined,
  providers: ProviderType[]
): AvailabilityZonesByProvider {
  const next: AvailabilityZonesByProvider = {};
  for (const provider of providers) {
    if (!provider?.uuid) continue;
    next[provider.uuid] = {};
  }
  for (const [key, zones] of Object.entries(flat ?? {})) {
    const sep = key.indexOf(':');
    if (sep < 0) continue;
    const providerUuid = key.slice(0, sep);
    const regionCode = key.slice(sep + 1);
    if (!next[providerUuid]) next[providerUuid] = {};
    next[providerUuid][regionCode] = zones ?? [];
  }
  return next;
}

/** Hydrate form ByProvider from context nodesByProvider for selected providers. */
export function availabilityZonesByProviderFromNodes(
  nodesByProvider?: Record<string, NodeAvailabilityProps> | null,
  providers?: ProviderType[] | null
): AvailabilityZonesByProvider {
  const result: AvailabilityZonesByProvider = {};
  for (const provider of providers ?? []) {
    if (!provider?.uuid) continue;
    result[provider.uuid] = {
      ...(nodesByProvider?.[provider.uuid]?.availabilityZones ?? {})
    };
  }
  return result;
}

/** Persist: only availabilityZones per provider; RF/dedicated stay singular. */
export function buildNodesByProvider(
  availabilityZonesByProvider: AvailabilityZonesByProvider | null | undefined,
  providers: ProviderType[]
): Record<string, NodeAvailabilityProps> {
  const result: Record<string, NodeAvailabilityProps> = {};
  for (const provider of providers) {
    if (!provider?.uuid) continue;
    result[provider.uuid] = {
      ...emptyNodesSlice(),
      availabilityZones: { ...(availabilityZonesByProvider?.[provider.uuid] ?? {}) }
    };
  }
  return result;
}

/**
 * Build ByProvider zone maps from placement regions + optional existing rows.
 * Used when applying Expert/Guided defaults per provider.
 */
export function partitionAvailabilityZonesByProvider(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  regions: Region[],
  providers: ProviderType[]
): AvailabilityZonesByProvider {
  const byCodeToProvider = new Map<string, string>();
  for (const region of regions) {
    if (region.providerUuid) {
      byCodeToProvider.set(region.code, region.providerUuid);
    }
  }
  const next: AvailabilityZonesByProvider = {};
  for (const provider of providers) {
    if (!provider?.uuid) continue;
    next[provider.uuid] = {};
  }
  for (const [regionCode, zones] of Object.entries(availabilityZones ?? {})) {
    const providerUuid = byCodeToProvider.get(regionCode);
    if (!providerUuid || !next[providerUuid]) continue;
    next[providerUuid][regionCode] = zones;
  }
  // Ensure every selected region has a key (possibly empty) under its provider.
  for (const region of regions) {
    if (!region.providerUuid || !next[region.providerUuid]) continue;
    if (!(region.code in next[region.providerUuid])) {
      next[region.providerUuid][region.code] = [];
    }
  }
  return next;
}

export function singularNodesFromMultiForm(
  values: NodeAvailabilityProps
): NodeAvailabilityProps {
  const { availabilityZonesByProvider: _drop, ...rest } = values as NodeAvailabilityProps & {
    availabilityZonesByProvider?: AvailabilityZonesByProvider;
  };
  return {
    ...rest,
    availabilityZones: {},
    useDedicatedNodes: rest.useDedicatedNodes ?? false,
    [REPLICATION_FACTOR]: rest[REPLICATION_FACTOR]
  };
}

/**
 * Masters per provider: one per AZ (zones with nodes), round-robin in provider order until rf placed.
 * shortcut: mirrors YBA's one-master-per-AZ placement; switch to backend-provided placement if it diverges.
 */
export function getMasterCountsByProvider(
  byProvider: AvailabilityZonesByProvider | null | undefined,
  providers: Array<Pick<ProviderType, 'uuid'>>,
  replicationFactor: number
): Record<string, number> {
  const zoneOwners = providers.flatMap((provider) =>
    Object.values(byProvider?.[provider.uuid] ?? {})
      .flatMap((zones) => zones ?? [])
      .filter((zone) => (zone?.nodeCount ?? 0) > 0)
      .map(() => provider.uuid)
  );
  const counts: Record<string, number> = {};
  if (zoneOwners.length === 0) return counts;
  for (let i = 0; i < replicationFactor; i++) {
    const uuid = zoneOwners[i % zoneOwners.length];
    counts[uuid] = (counts[uuid] ?? 0) + 1;
  }
  return counts;
}
