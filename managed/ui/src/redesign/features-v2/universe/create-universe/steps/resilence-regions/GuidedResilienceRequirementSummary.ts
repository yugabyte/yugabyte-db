import { getFaultToleranceNeeded } from '../../CreateUniverseUtils';
import { FaultToleranceType } from './dtos';

export type RequirementCardPlacementStep = 'resilience' | 'nodes';

export type GuidedRequirementTag =
  | { kind: 'regions'; count: number }
  | { kind: 'regions_one_plus' }
  | { kind: 'availability_zones'; count: number }
  | { kind: 'nodes_minimum'; count: number }
  | { kind: 'az_range_node_level'; maxAz: number };

export interface GuidedResilienceRequirementSummary {
  tags: GuidedRequirementTag[];
  displayReplicationFactor: number;
}

export function getGuidedResilienceRequirementSummary(
  faultToleranceType: FaultToleranceType,
  resilienceFactor: number
): GuidedResilienceRequirementSummary {
  const n = getFaultToleranceNeeded(resilienceFactor);

  if (faultToleranceType === FaultToleranceType.NONE) {
    return {
      tags: [{ kind: 'availability_zones', count: 1 }],
      displayReplicationFactor: 1
    };
  }

  switch (faultToleranceType) {
    case FaultToleranceType.REGION_LEVEL:
      return {
        tags: [{ kind: 'regions', count: n }],
        displayReplicationFactor: n
      };
    case FaultToleranceType.AZ_LEVEL:
      return {
        tags: [{ kind: 'regions_one_plus' }, { kind: 'availability_zones', count: n }],
        displayReplicationFactor: n
      };
    case FaultToleranceType.NODE_LEVEL: {
      const maxAzForNodeLevel = Math.max(1, n - 1);
      return {
        tags: [
          { kind: 'az_range_node_level', maxAz: maxAzForNodeLevel },
          { kind: 'nodes_minimum', count: n }
        ],
        displayReplicationFactor: n
      };
    }
    default:
      return { tags: [], displayReplicationFactor: n };
  }
}

/** Replication factor shown on the nodes step and in payloads for guided mode. */
export function getGuidedNodesStepReplicationFactor(
  faultToleranceType: FaultToleranceType,
  resilienceFactor: number
): number {
  if (faultToleranceType === FaultToleranceType.NONE) {
    return resilienceFactor;
  }
  return getFaultToleranceNeeded(resilienceFactor);
}

/** Singular noun keys under guidedMode for use with `pluralize(t(key), count)`. */
export type GuidedModeEntityWordKey = 'wordRegion' | 'wordAvailabilityZone' | 'wordNode';

/** Nodes-step card title; null → use selectedResilienceRequires. */
export function getNodesStepRequirementCardTitleSpec(
  faultToleranceType: FaultToleranceType,
  resilienceFactor: number
): { count: number; entityWordKey: GuidedModeEntityWordKey } | null {
  if (faultToleranceType === FaultToleranceType.NONE) {
    return null;
  }
  if (faultToleranceType === FaultToleranceType.REGION_LEVEL) {
    return { count: resilienceFactor, entityWordKey: 'wordRegion' };
  }
  if (faultToleranceType === FaultToleranceType.AZ_LEVEL) {
    return { count: resilienceFactor, entityWordKey: 'wordAvailabilityZone' };
  }
  if (faultToleranceType === FaultToleranceType.NODE_LEVEL) {
    return { count: resilienceFactor, entityWordKey: 'wordNode' };
  }
  return null;
}
