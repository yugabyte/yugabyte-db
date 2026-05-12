import * as Yup from 'yup';
import { values } from 'lodash';
import { ValidationArgs, ValidationMetrics } from './common';
import { inferResilience } from '../../../CreateUniverseUtils';

type ExpertValidationArgs = ValidationArgs & {
  metrics: ValidationMetrics;
  replicationFactor?: number;
};

export function validateExpertRules({
  availabilityZones,
  path,
  createError,
  resilienceAndRegionsProps,
  replicationFactor,
  metrics
}: ExpertValidationArgs): Yup.ValidationError[] {
  const fieldErrors: Yup.ValidationError[] = [];
  if (!resilienceAndRegionsProps) {
    return fieldErrors;
  }

  const expertRf = replicationFactor ?? 1;



  const inferred = inferResilience(resilienceAndRegionsProps, {
    availabilityZones,
    replicationFactor: expertRf,
    useDedicatedNodes: false
  });

  if (inferred === null) {
    fieldErrors.push(
      createError({
        path,
        message: 'errMsg.expertResilienceUninferable'
      })
    );
    fieldErrors.push(
      createError({
        path: 'availabilityZones'
      })
    );
    return fieldErrors;
  }

  // Expert mode requires at least RF nodes in the current placement.
  if (metrics.nodeCounts < expertRf) {
    fieldErrors.push(
      createError({
        path,
        message: 'errMsg.expertResilienceUninferable'
      })
    );
    fieldErrors.push(
      createError({
        path: 'availabilityZones'
      })
    );
  }

  const hasBlankAzSelection = values(availabilityZones)
    .flat()
    .some((zone) => !zone.name);
  if (hasBlankAzSelection) {
    fieldErrors.push(
      createError({
        path,
        message: 'errMsg.expertAzBlank'
      })
    );
    fieldErrors.push(
      createError({
        path: 'availabilityZones'
      })
    );
    return fieldErrors;
  }
  return fieldErrors;
}

