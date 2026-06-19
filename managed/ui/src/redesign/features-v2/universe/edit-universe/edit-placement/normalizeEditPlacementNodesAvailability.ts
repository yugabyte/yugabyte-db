import { values } from 'lodash';
import { Region } from '@app/redesign/helpers/dtos';
import {
  assignRegionsAZNodeByReplicationFactor,
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

function flattenZonesInRegionOrder(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  regions: Region[]
): Zone[] {
  const flat: Zone[] = [];
  for (const region of regions) {
    for (const zone of availabilityZones[region.code] ?? []) {
      flat.push(zone);
    }
  }
  return flat;
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

function overlayExistingOntoExpected(
  expected: NodeAvailabilityProps['availabilityZones'],
  existing: NodeAvailabilityProps['availabilityZones'],
  regions: Region[]
): NodeAvailabilityProps['availabilityZones'] {
  const flatExisting = flattenZonesInRegionOrder(existing, regions);
  const result: NodeAvailabilityProps['availabilityZones'] = {};
  let existingIndex = 0;
  let rank = AZ_PREFFERED_HIGHEST_RANK;

  for (const region of regions) {
    const expectedZones = expected[region.code] ?? [];
    if (!expectedZones.length) {
      continue;
    }
    result[region.code] = expectedZones.map((expectedZone) => {
      const fromExisting = flatExisting[existingIndex];
      existingIndex += 1;
      if (fromExisting) {
        return {
          ...expectedZone,
          ...fromExisting,
          uuid: fromExisting.uuid || expectedZone.uuid,
          name: fromExisting.name || expectedZone.name,
          nodeCount: fromExisting.nodeCount ?? expectedZone.nodeCount,
          preffered: rank++
        };
      }
      return {
        ...expectedZone,
        preffered: rank++
      };
    });
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
    return nodesAndAvailability;
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
  } else {
    normalizedZones = overlayExistingOntoExpected(expectedZones, filteredExisting, regions);
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
