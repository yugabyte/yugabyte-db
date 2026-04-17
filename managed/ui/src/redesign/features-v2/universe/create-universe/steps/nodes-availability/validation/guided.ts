import * as Yup from 'yup';
import { values } from 'lodash';
import { FaultToleranceType, ResilienceType } from '../../resilence-regions/dtos';
import { ValidationArgs, ValidationMetrics } from './common';

type GuidedValidationArgs = ValidationArgs & {
  metrics: ValidationMetrics;
  useDedicatedNodes: boolean;
};

export function validateGuidedRules({
  availabilityZones,
  path,
  createError,
  resilienceAndRegionsProps,
  metrics,
  useDedicatedNodes
}: GuidedValidationArgs): Yup.ValidationError[] {
  const fieldErrors: Yup.ValidationError[] = [];
  const faultToleranceType = resilienceAndRegionsProps?.faultToleranceType;
  const resilienceType = resilienceAndRegionsProps?.resilienceType;

  // Node-level resilience (guided): one region only and exactly one availability zone.
  if (
    faultToleranceType === FaultToleranceType.NODE_LEVEL &&
    resilienceType === ResilienceType.REGULAR
  ) {
    const regionsWithZones = values(availabilityZones).filter((zones) => zones && zones.length > 0);
    if (regionsWithZones.length !== 1) {
      fieldErrors.push(
        createError({
          path,
          message: 'errMsg.nodeLevelOneRegionOneAz'
        })
      );
    } else if (metrics.azCounts !== 1) {
      fieldErrors.push(
        createError({
          path,
          message: 'errMsg.nodeLevelGuidedExactlyOneAz'
        })
      );
    }
  }

  if (faultToleranceType === FaultToleranceType.AZ_LEVEL) {
    if (metrics.azCounts !== metrics.faultToleranceNeeded) {
      fieldErrors.push(
        createError({
          path,
          message:
            metrics.azCounts > metrics.faultToleranceNeeded
              ? 'errMsg.azCountTooMany'
              : 'errMsg.azCountTooFew'
        })
      );
      fieldErrors.push(
        createError({
          path: 'availabilityZones'
        })
      );
    }
  }

  if (
    faultToleranceType === FaultToleranceType.NODE_LEVEL &&
    metrics.nodeCounts < metrics.faultToleranceNeeded
  ) {
    fieldErrors.push(
      createError({
        path,
        message: useDedicatedNodes ? 'errMsg.lessNodesDedicated' : 'errMsg.lessNodes'
      })
    );
  }

  return fieldErrors;
}

