import { describe, expect, it, vi } from 'vitest';
import type { UseFormReturn } from 'react-hook-form';
import {
  assignRegionsAZNodeByReplicationFactor,
  getExpertAvailabilityZonesOrEmpty
} from '../../CreateUniverseUtils';
import {
  FaultToleranceType,
  ResilienceFormMode,
  ResilienceType,
  type ResilienceAndRegionsProps
} from '../resilence-regions/dtos';
import {
  FAULT_TOLERANCE_TYPE,
  NODE_COUNT,
  REGIONS_FIELD,
  REPLICATION_FACTOR,
  RESILIENCE_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../fields/FieldNames';
import { applyNodesStepPlacementFromResilience } from './useNodesAvailabilityStep';
import type { NodeAvailabilityProps } from './dtos';

function makeRegion(code: string, zoneCount: number) {
  return {
    uuid: `uuid-${code}`,
    code,
    name: code,
    zones: Array.from({ length: zoneCount }, (_, i) => ({
      uuid: `${code}-z${i}`,
      code: `${code}-z${i}`,
      name: `Z${i}`,
      subnet: ''
    }))
  };
}

function stubForm(initial: Partial<NodeAvailabilityProps> = {}) {
  const values: NodeAvailabilityProps = {
    availabilityZones: {},
    useDedicatedNodes: false,
    ...initial
  };
  return {
    getValues: (name?: keyof NodeAvailabilityProps) =>
      name === undefined ? values : values[name],
    setValue: (name: keyof NodeAvailabilityProps, value: unknown) => {
      (values as Record<string, unknown>)[name as string] = value;
    },
    values
  } as UseFormReturn<NodeAvailabilityProps> & { values: NodeAvailabilityProps };
}

describe('applyNodesStepPlacementFromResilience', () => {
  it('C8: expert empty zones fill via getExpertAvailabilityZonesOrEmpty', () => {
    const resilience = {
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [NODE_COUNT]: 1,
      [REGIONS_FIELD]: [makeRegion('r0', 5)]
    } as ResilienceAndRegionsProps;
    const expected = getExpertAvailabilityZonesOrEmpty(resilience);
    const form = stubForm();
    applyNodesStepPlacementFromResilience(form, resilience, vi.fn());

    expect(form.values.availabilityZones).toEqual(expected.availabilityZones);
    expect(form.values[REPLICATION_FACTOR]).toBe(expected.replicationFactor);
  });

  it('C9: guided empty zones fill named AZs via assignRegionsAZNodeByReplicationFactor', () => {
    const resilience = {
      [RESILIENCE_TYPE]: ResilienceType.REGULAR,
      [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1,
      [NODE_COUNT]: 1,
      [REGIONS_FIELD]: [makeRegion('r0', 5)]
    } as ResilienceAndRegionsProps;
    const expected = assignRegionsAZNodeByReplicationFactor(resilience);
    const form = stubForm();
    applyNodesStepPlacementFromResilience(form, resilience, vi.fn());

    expect(form.values.availabilityZones).toEqual(expected);
    expect(
      Object.values(form.values.availabilityZones)
        .flat()
        .every((z) => Boolean(z.name))
    ).toBe(true);
  });
});
