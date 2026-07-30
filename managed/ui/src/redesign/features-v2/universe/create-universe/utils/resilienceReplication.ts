import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../steps/resilence-regions/dtos';
import { NodeAvailabilityProps } from '../steps/nodes-availability/dtos';
import { Region } from '../../../../helpers/dtos';
import { Provider } from '@app/components/configRedesign/providerRedesign/types';
import { FAULT_TOLERANCE_TYPE, REPLICATION_FACTOR, RESILIENCE_FACTOR } from '../fields/FieldNames';
import { values } from 'lodash';
import { getAZCount } from './placementAndAvailability';

export function getFaultToleranceNeeded(replicationFactor: number) {
  return replicationFactor * 2 + 1;
}

/** Guided mode: map fault-tolerance degree to API replication factor (NONE keeps stored value, typically 1). */
export function getGuidedNodesStepReplicationFactor(
  faultToleranceType: FaultToleranceType,
  resilienceFactor: number
): number {
  if (faultToleranceType === FaultToleranceType.NONE) {
    return resilienceFactor;
  }
  return getFaultToleranceNeeded(resilienceFactor);
}

/** API / placement replication factor (guided = resilience step FT → RF; expert = nodes step RF). */
export function getEffectiveReplicationFactorForResilience(
  settings: ResilienceAndRegionsProps,
  nodesAvailability?: Pick<NodeAvailabilityProps, typeof REPLICATION_FACTOR>
): number {
  if (settings.resilienceType === ResilienceType.SINGLE_NODE) {
    return 1;
  }
  if (settings.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
    return nodesAvailability?.[REPLICATION_FACTOR] ?? settings.resilienceFactor;
  }
  return getGuidedNodesStepReplicationFactor(
    settings.faultToleranceType,
    settings.resilienceFactor
  );
}

export const canSelectMultipleRegions = (resilienceType?: ResilienceType) => {
  return resilienceType !== ResilienceType.SINGLE_NODE;
};

type isGuidedModeSupportedResult =
  | {
      isSupported: false;
    }
  | {
      isSupported: true;
      resilienceFactor: number;
      faultToleranceType: FaultToleranceType;
    };

/** Guided mode supports odd RF values up to 7 (FT degree 1–3). */
const guidedRFCheck = (i: number) => i <= 7 && i % 2 === 1;

const guidedResilienceFactorFromReplication = (rf: number, faultToleranceType: FaultToleranceType) => {
  if (faultToleranceType === FaultToleranceType.NONE) {
    return rf;
  }
  return Math.max(0, Math.floor((rf - 1) / 2));
};

export const isCurrentConfigSupportedByGuidedMode = (
  resilience: ResilienceAndRegionsProps,
  nodesAndAvailability?: NodeAvailabilityProps
): isGuidedModeSupportedResult => {
  const availabilityZones = nodesAndAvailability?.availabilityZones;
  const azCount = getAZCount(availabilityZones ?? {});
  const regionCount = resilience.regions.length;
  const nodeCounts = values(availabilityZones ?? {}).flatMap((zones) =>
    zones.map((zone) => zone.nodeCount)
  );

  // No nodes/placement yet — nothing incompatible with guided; keep current resilience settings.
  if (azCount === 0) {
    return {
      isSupported: true,
      resilienceFactor: resilience.resilienceFactor,
      faultToleranceType: resilience.faultToleranceType
    };
  }

  // All AZs must have the same node count.
  if (new Set(nodeCounts).size !== 1) {
    return { isSupported: false };
  }

  // Prefer nodes-step RF. In expert mode resilience.resilienceFactor may be a synced RF or a
  // guided FT degree; for single-AZ, per-AZ node count matches RF in default placements.
  const storedRf = nodesAndAvailability?.[REPLICATION_FACTOR];
  const singleAzNodeCount = azCount === 1 ? nodeCounts[0] : undefined;
  const rf = storedRf ?? singleAzNodeCount ?? resilience.resilienceFactor;

  if (rf <= 1) {
    return {
      isSupported: true,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: guidedResilienceFactorFromReplication(rf, FaultToleranceType.NONE)
    };
  }

  if (!guidedRFCheck(rf)) {
    return { isSupported: false };
  }

  const resilienceFactor = guidedResilienceFactorFromReplication(rf, FaultToleranceType.AZ_LEVEL);

  if (regionCount === rf && azCount === regionCount) {
    return {
      isSupported: true,
      faultToleranceType: FaultToleranceType.REGION_LEVEL,
      resilienceFactor
    };
  }

  if (regionCount < rf && azCount === rf) {
    return {
      isSupported: true,
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor
    };
  }

  if (regionCount < rf && azCount === 1 && guidedRFCheck(nodeCounts[0])) {
    // Single-AZ node FT: use the larger of RF and per-AZ nodes so e.g. RF=3 with 5 nodes
    // (or missing stored RF with 5 nodes) maps to guided outage count 2, not 1.
    const nodeLevelRf = Math.max(rf, nodeCounts[0]);
    return {
      isSupported: true,
      faultToleranceType: FaultToleranceType.NODE_LEVEL,
      resilienceFactor: guidedResilienceFactorFromReplication(
        nodeLevelRf,
        FaultToleranceType.NODE_LEVEL
      )
    };
  }

  return { isSupported: false };
};

export const computeResilienceTypeFromProvider = (
  provider: Provider
): {
  [FAULT_TOLERANCE_TYPE]: FaultToleranceType;
  [RESILIENCE_FACTOR]: number;
} => {
  const numOfRegions = provider.regions.length;
  const numOfAZs = ((provider.regions as unknown) as Region[]).reduce(
    (acc, region) => acc + region.zones.length,
    0
  );
  if (numOfRegions > 1) {
    return {
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1
    };
  }

  if (numOfAZs >= 3) {
    return {
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1
    };
  }

  return {
    [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
    [RESILIENCE_FACTOR]: 1
  };
};
