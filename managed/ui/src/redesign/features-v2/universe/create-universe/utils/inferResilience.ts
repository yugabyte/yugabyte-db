import { NodeAvailabilityProps } from '../steps/nodes-availability/dtos';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps
} from '../steps/resilence-regions/dtos';
import { getAZCount, getNodeCount } from './placementAndAvailability';

export const inferResilience = (
  resilience: ResilienceAndRegionsProps,
  nodesAndAvailability: NodeAvailabilityProps
): FaultToleranceType | null => {
  const { regions } = resilience;
  const { replicationFactor = 1, availabilityZones } = nodesAndAvailability;

  if (replicationFactor < 1 || regions.length === 0) {
    return null;
  }

  const azCount = getAZCount(availabilityZones);
  if (azCount === 0) {
    return null;
  }

  if (regions.length > replicationFactor) {
    return null;
  }
  if (regions.length === replicationFactor) {
    return FaultToleranceType.REGION_LEVEL;
  }
  if (regions.length < replicationFactor && azCount === replicationFactor) {
    return FaultToleranceType.AZ_LEVEL;
  }
  if (regions.length < replicationFactor && azCount < replicationFactor) {
    return FaultToleranceType.NODE_LEVEL;
  }

  // Config does not match inference formula (e.g. selected AZs > RF).
  return null;
};

/**
 * Computes the outage count shown in the inferred resilience card.
 * For NODE_LEVEL, cap RF-based tolerance with placement-based tolerance so
 * under-provisioned layouts do not over-claim resilience.
 */
export const getInferredOutageCount = (
  inferredResilience: ReturnType<typeof inferResilience>,
  replicationFactor: number,
  availabilityZones: NodeAvailabilityProps['availabilityZones']
) => {
  const rfOutageCount = Math.max(0, Math.floor((replicationFactor - 1) / 2));
  const nodeCount = getNodeCount(availabilityZones);

  // Placement is under-provisioned for this RF; do not claim outage tolerance.
  if (nodeCount < replicationFactor) {
    return 0;
  }

  if (inferredResilience !== FaultToleranceType.NODE_LEVEL) {
    return rfOutageCount;
  }

  const nodePlacementOutageCount = Math.max(0, Math.floor((nodeCount - 1) / 2));
  return Math.min(rfOutageCount, nodePlacementOutageCount);
};
