import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../steps/resilence-regions/dtos';
import { Region } from '../../../../helpers/dtos';
import { Provider } from '@app/components/configRedesign/providerRedesign/types';
import { FAULT_TOLERANCE_TYPE, RESILIENCE_FACTOR } from '../fields/FieldNames';

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

/** API / placement replication factor from resilience form (expert = literal RF; guided = FT → RF). */
export function getEffectiveReplicationFactorForResilience(
  settings: ResilienceAndRegionsProps
): number {
  if (settings.resilienceType === ResilienceType.SINGLE_NODE) {
    return 1;
  }
  if (settings.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
    return settings.resilienceFactor;
  }
  return getGuidedNodesStepReplicationFactor(
    settings.faultToleranceType,
    settings.resilienceFactor
  );
}

export const canSelectMultipleRegions = (resilienceType?: ResilienceType) => {
  return resilienceType !== ResilienceType.SINGLE_NODE;
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
