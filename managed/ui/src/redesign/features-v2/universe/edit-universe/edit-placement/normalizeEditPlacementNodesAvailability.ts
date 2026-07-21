import { isEmpty, values } from 'lodash';
import { Region } from '@app/redesign/helpers/dtos';
import {
  assignRegionsAZNodeByReplicationFactor,
  getExpertNodesStepDefaultPlacement,
  getFaultToleranceNeeded,
  getGuidedNodesStepReplicationFactor
} from '../../create-universe/CreateUniverseUtils';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../../create-universe/steps/resilence-regions/dtos';
import { NodeAvailabilityProps, Zone } from '../../create-universe/steps/nodes-availability/dtos';
import { REPLICATION_FACTOR } from '../../create-universe/fields/FieldNames';
import { AZ_NOT_PREFERRED, AZ_PREFFERED_HIGHEST_RANK } from '../../create-universe/helpers/constants';
import { EditPlacementContextProps } from './EditPlacementContext';

export function isSingleAzMode(resilience: ResilienceAndRegionsProps): boolean {
  return (
    resilience.faultToleranceType === FaultToleranceType.NONE ||
    resilience.faultToleranceType === FaultToleranceType.NODE_LEVEL
  );
}

/** Guided-mode AZ row count when fixed by fault tolerance; null when shape is region-driven. */
export function requiredAzCountForGuided(resilience: ResilienceAndRegionsProps): number | null {
  if (resilience.resilienceType === ResilienceType.SINGLE_NODE) {
    return 1;
  }
  if (isSingleAzMode(resilience)) {
    return 1;
  }
  if (resilience.faultToleranceType === FaultToleranceType.AZ_LEVEL) {
    return getFaultToleranceNeeded(resilience.resilienceFactor);
  }
  return null;
}

function countAzRows(availabilityZones: NodeAvailabilityProps['availabilityZones']): number {
  return values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0);
}

function selectedRegionCodes(resilience: ResilienceAndRegionsProps): Set<string> {
  return new Set((resilience.regions ?? []).map((region) => region.code));
}

/** Same region-key equality as useNodesAvailabilityStep.regionCodesMatchAvailabilityZones. */
function regionCodesMatchAvailabilityZones(
  regions: Region[],
  availabilityZones: NodeAvailabilityProps['availabilityZones'] | undefined
): boolean {
  const selectedCodes = new Set(regions.map((r) => r.code));
  const zoneKeys = new Set(Object.keys(availabilityZones ?? {}));
  if (selectedCodes.size !== zoneKeys.size) {
    return false;
  }
  for (const code of zoneKeys) {
    if (!selectedCodes.has(code)) {
      return false;
    }
  }
  return true;
}

/**
 * Prefer existing (universe) AZ rows for regions that stay selected; only fill
 * missing/new regions from expert defaults (or guided assign fallback).
 */
function recalculateExpertNodesAvailability(
  resilience: ResilienceAndRegionsProps,
  nodesAndAvailability: NodeAvailabilityProps
): NodeAvailabilityProps {
  const selectedCodes = selectedRegionCodes(resilience);
  const keptExisting = filterToSelectedRegions(
    nodesAndAvailability.availabilityZones ?? {},
    selectedCodes
  );
  const expertPlacement = getExpertNodesStepDefaultPlacement(resilience);
  const defaultZones =
    expertPlacement?.availabilityZones ?? assignRegionsAZNodeByReplicationFactor(resilience);

  const mergedZones: NodeAvailabilityProps['availabilityZones'] = {};
  for (const region of resilience.regions ?? []) {
    const existing = keptExisting[region.code];
    if (existing?.length) {
      mergedZones[region.code] = existing;
      continue;
    }
    const generated = defaultZones[region.code];
    if (generated?.length) {
      mergedZones[region.code] = generated;
    }
  }

  return {
    ...nodesAndAvailability,
    useDedicatedNodes: nodesAndAvailability.useDedicatedNodes,
    availabilityZones: mergedZones,
    [REPLICATION_FACTOR]:
      expertPlacement?.replicationFactor ??
      nodesAndAvailability[REPLICATION_FACTOR] ??
      resilience.resilienceFactor ??
      1
  };
}

function filterToSelectedRegions(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  selectedCodes: Set<string>
): NodeAvailabilityProps['availabilityZones'] {
  const filtered: NodeAvailabilityProps['availabilityZones'] = {};
  for (const [code, zones] of Object.entries(availabilityZones ?? {})) {
    if (selectedCodes.has(code) && zones?.length) {
      filtered[code] = zones;
    }
  }
  return filtered;
}

function trimAzRowsToCount(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  regions: Region[],
  targetCount: number
): NodeAvailabilityProps['availabilityZones'] {
  const result: NodeAvailabilityProps['availabilityZones'] = {};
  let remaining = targetCount;
  let rank = AZ_PREFFERED_HIGHEST_RANK;

  for (const region of regions) {
    if (remaining <= 0) {
      break;
    }
    const regionZones = availabilityZones[region.code] ?? [];
    if (!regionZones.length) {
      continue;
    }
    const take = Math.min(regionZones.length, remaining);
    result[region.code] = regionZones.slice(0, take).map((zone) => ({
      ...zone,
      preffered: rank++
    }));
    remaining -= take;
  }

  return result;
}

/**
 * Map existing AZ rows onto the expected layout by matching uuid/name within the
 * same region. Existing universe AZs are placed first so guided mode can treat
 * them as the lead AZ (node count source). Remaining slots are filled from
 * expected without duplicating names/uuids.
 */
function overlayExistingOntoExpected(
  expected: NodeAvailabilityProps['availabilityZones'],
  existing: NodeAvailabilityProps['availabilityZones'],
  regions: Region[]
): NodeAvailabilityProps['availabilityZones'] {
  const result: NodeAvailabilityProps['availabilityZones'] = {};
  let rank = AZ_PREFFERED_HIGHEST_RANK;

  for (const region of regions) {
    const expectedZones = expected[region.code] ?? [];
    if (!expectedZones.length) {
      continue;
    }

    const existingInRegion = existing[region.code] ?? [];
    const usedKeys = new Set<string>();
    const merged: Zone[] = [];

    const markUsed = (zone: Zone) => {
      if (zone.uuid) usedKeys.add(zone.uuid);
      if (zone.name) usedKeys.add(zone.name);
    };
    const isUsed = (zone: Zone) =>
      (Boolean(zone.uuid) && usedKeys.has(zone.uuid)) ||
      (Boolean(zone.name) && usedKeys.has(zone.name));

    // Existing universe zones first (matched to expected when possible).
    for (const fromExisting of existingInRegion) {
      if (merged.length >= expectedZones.length) {
        break;
      }
      if (isUsed(fromExisting)) {
        continue;
      }
      const matchedExpected = expectedZones.find(
        (expectedZone) =>
          (Boolean(fromExisting.uuid) && fromExisting.uuid === expectedZone.uuid) ||
          (Boolean(fromExisting.name) && fromExisting.name === expectedZone.name)
      );
      const zone = {
        ...(matchedExpected ?? {}),
        ...fromExisting,
        uuid: fromExisting.uuid || matchedExpected?.uuid,
        name: fromExisting.name || matchedExpected?.name,
        nodeCount: fromExisting.nodeCount ?? matchedExpected?.nodeCount ?? 1,
        preffered: AZ_NOT_PREFERRED
      };
      merged.push(zone);
      markUsed(zone);
    }

    // Fill remaining slots from expected, skipping already-selected AZs.
    for (const expectedZone of expectedZones) {
      if (merged.length >= expectedZones.length) {
        break;
      }
      if (isUsed(expectedZone)) {
        continue;
      }
      merged.push({ ...expectedZone, preffered: AZ_NOT_PREFERRED });
      markUsed(expectedZone);
    }

    result[region.code] = merged.map((zone) => ({
      ...zone,
      preffered: rank++
    }));
  }

  return result;
}

/** Guided mode: every AZ uses the first AZ's node count (same rule as the nodes step UI). */
function syncGuidedNodeCountsToFirstAz(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  regions: Region[]
): NodeAvailabilityProps['availabilityZones'] {
  let nodeCount: number | undefined;
  for (const region of regions) {
    const first = availabilityZones[region.code]?.[0];
    if (first && typeof first.nodeCount === 'number' && first.nodeCount >= 1) {
      nodeCount = first.nodeCount;
      break;
    }
  }
  if (nodeCount === undefined) {
    return availabilityZones;
  }

  const result: NodeAvailabilityProps['availabilityZones'] = {};
  for (const [code, zones] of Object.entries(availabilityZones ?? {})) {
    result[code] = zones.map((zone) =>
      zone.nodeCount === nodeCount ? zone : { ...zone, nodeCount }
    );
  }
  return result;
}

function normalizeSingleAzLayout(
  resilience: ResilienceAndRegionsProps,
  existing: NodeAvailabilityProps['availabilityZones'],
  expected: NodeAvailabilityProps['availabilityZones']
): NodeAvailabilityProps['availabilityZones'] {
  const firstRegion = resilience.regions?.[0];
  if (!firstRegion) {
    return expected;
  }

  const existingZone =
    existing[firstRegion.code]?.[0] ??
    values(existing).find((zones) => zones.length > 0)?.[0];
  const expectedZone = expected[firstRegion.code]?.[0];

  if (!existingZone && !expectedZone) {
    return {};
  }

  const nodeCount =
    resilience.faultToleranceType === FaultToleranceType.NODE_LEVEL
      ? getFaultToleranceNeeded(resilience.resilienceFactor)
      : existingZone?.nodeCount ?? expectedZone?.nodeCount ?? 1;

  const zone = existingZone ?? expectedZone!;

  return {
    [firstRegion.code]: [
      {
        ...zone,
        nodeCount,
        preffered: AZ_NOT_PREFERRED
      }
    ]
  };
}

export function needsEditPlacementNodesNormalization(
  resilience: ResilienceAndRegionsProps,
  availabilityZones: NodeAvailabilityProps['availabilityZones'] | undefined
): boolean {
  if (!availabilityZones || Object.keys(availabilityZones).length === 0) {
    return false;
  }

  const selectedCodes = selectedRegionCodes(resilience);
  const filtered = filterToSelectedRegions(availabilityZones, selectedCodes);
  const zoneKeys = Object.keys(availabilityZones);

  if (zoneKeys.some((code) => !selectedCodes.has(code))) {
    return true;
  }

  if (selectedCodes.size > 0 && zoneKeys.length !== selectedCodes.size) {
    const filteredKeys = Object.keys(filtered);
    if (filteredKeys.length !== selectedCodes.size) {
      return true;
    }
  }

  if (isSingleAzMode(resilience)) {
    const regionsWithZones = values(filtered).filter((zones) => zones.length > 0);
    return regionsWithZones.length !== 1 || countAzRows(filtered) !== 1;
  }

  const requiredAz = requiredAzCountForGuided(resilience);
  if (requiredAz !== null && countAzRows(filtered) !== requiredAz) {
    return true;
  }

  return false;
}

export function normalizeEditPlacementNodesAvailability(
  context: Pick<EditPlacementContextProps, 'resilience' | 'nodesAndAvailability'>
): NodeAvailabilityProps | undefined {
  const { resilience, nodesAndAvailability } = context;
  if (!resilience || !nodesAndAvailability) {
    return nodesAndAvailability;
  }

  if (resilience.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
    const regions = resilience.regions ?? [];
    const { availabilityZones } = nodesAndAvailability;
    if (
      !isEmpty(availabilityZones) &&
      regionCodesMatchAvailabilityZones(regions, availabilityZones)
    ) {
      return nodesAndAvailability;
    }
    return recalculateExpertNodesAvailability(resilience, nodesAndAvailability);
  }

  const { availabilityZones, ...rest } = nodesAndAvailability;
  if (!needsEditPlacementNodesNormalization(resilience, availabilityZones)) {
    return nodesAndAvailability;
  }

  const expectedZones = assignRegionsAZNodeByReplicationFactor(resilience);
  const selectedCodes = selectedRegionCodes(resilience);
  const filteredExisting = filterToSelectedRegions(availabilityZones ?? {}, selectedCodes);
  const regions = resilience.regions ?? [];

  let normalizedZones: NodeAvailabilityProps['availabilityZones'];

  if (isSingleAzMode(resilience)) {
    normalizedZones = normalizeSingleAzLayout(resilience, filteredExisting, expectedZones);
  } else if (resilience.faultToleranceType === FaultToleranceType.AZ_LEVEL) {
    const requiredAz = requiredAzCountForGuided(resilience)!;
    const currentCount = countAzRows(filteredExisting);

    if (currentCount > requiredAz) {
      normalizedZones = trimAzRowsToCount(filteredExisting, regions, requiredAz);
    } else if (currentCount < requiredAz) {
      normalizedZones = overlayExistingOntoExpected(expectedZones, filteredExisting, regions);
    } else {
      normalizedZones = overlayExistingOntoExpected(expectedZones, filteredExisting, regions);
    }
    normalizedZones = syncGuidedNodeCountsToFirstAz(normalizedZones, regions);
  } else {
    normalizedZones = syncGuidedNodeCountsToFirstAz(
      overlayExistingOntoExpected(expectedZones, filteredExisting, regions),
      regions
    );
  }

  return {
    ...rest,
    availabilityZones: normalizedZones,
    [REPLICATION_FACTOR]: getGuidedNodesStepReplicationFactor(
      resilience.faultToleranceType,
      resilience.resilienceFactor
    )
  };
}
