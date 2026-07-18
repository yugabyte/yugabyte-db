import * as Yup from 'yup';
import { uniq, values } from 'lodash';
import { NodeAvailabilityProps, Zone } from '../dtos';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceType
} from '../../resilence-regions/dtos';
import { getFaultToleranceNeeded, getNodeCount } from '../../../CreateUniverseUtils';

type CreateErrorFn = (opts: { path: string; message?: string }) => Yup.ValidationError;

export type ValidationMetrics = {
  nodeCounts: number;
  azCounts: number;
  faultToleranceNeeded: number;
};

export type ValidationArgs = {
  availabilityZones: NodeAvailabilityProps['availabilityZones'];
  path: string;
  createError: CreateErrorFn;
  resilienceAndRegionsProps?: ResilienceAndRegionsProps;
};

export function getValidationMetrics(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  resilienceAndRegionsProps?: ResilienceAndRegionsProps
): ValidationMetrics {
  return {
    nodeCounts: getNodeCount(availabilityZones),
    azCounts: values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0),
    faultToleranceNeeded: getFaultToleranceNeeded(resilienceAndRegionsProps?.resilienceFactor ?? 1)
  };
}

function collectPreferredRankErrors(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  createError: CreateErrorFn,
  errorPath: string
): Yup.ValidationError[] {
  const out: Yup.ValidationError[] = [];
  const allZones = values(availabilityZones).flat() as Zone[];
  if (allZones.length === 0) {
    return out;
  }

  const preferredValues = allZones
    .map((zone) => zone.preffered)
    .filter((v) => typeof v === 'number' && !Number.isNaN(v)) as number[];

  if (preferredValues.length !== allZones.length) {
    out.push(
      createError({
        path: errorPath,
        message: 'errMsg.preferredRankRequired'
      })
    );
    return out;
  }

  const unique = [...uniq(preferredValues)].sort((a, b) => a - b);
  const min = Math.min(...unique);
  const max = Math.max(...unique);
  for (let i = min; i <= max; i++) {
    if (!unique.includes(i)) {
      out.push(
        createError({
          path: errorPath,
          message: 'errMsg.missingPreferredValue'
        })
      );
      break;
    }
  }
  return out;
}

export function validateCommonRules({
  availabilityZones,
  path,
  createError,
  resilienceAndRegionsProps
}: ValidationArgs): Yup.ValidationError[] {
  const fieldErrors: Yup.ValidationError[] = [];
  const resilienceType = resilienceAndRegionsProps?.resilienceType;
  const faultToleranceType = resilienceAndRegionsProps?.faultToleranceType;

  // Every selected AZ must have at least one node (Regular multi-AZ flows).
  if (
    resilienceType !== ResilienceType.SINGLE_NODE &&
    faultToleranceType !== FaultToleranceType.NONE
  ) {
    let hasZeroNodeAz = false;
    for (const zones of values(availabilityZones)) {
      for (const z of zones) {
        if (typeof z.nodeCount !== 'number' || z.nodeCount < 1) {
          hasZeroNodeAz = true;
          break;
        }
      }
      if (hasZeroNodeAz) break;
    }
    if (hasZeroNodeAz) {
      fieldErrors.push(
        createError({
          path,
          message: 'errMsg.azZeroNodes'
        })
      );
    }
  }

  // Preferred ranks apply only when the UI allows ranking (not NODE_LEVEL / NONE — see Zone.tsx).
  if (
    resilienceType === ResilienceType.REGULAR &&
    faultToleranceType !== FaultToleranceType.NONE &&
    faultToleranceType !== FaultToleranceType.NODE_LEVEL
  ) {
    fieldErrors.push(...collectPreferredRankErrors(availabilityZones, createError, path));
  }

  return fieldErrors;
}

