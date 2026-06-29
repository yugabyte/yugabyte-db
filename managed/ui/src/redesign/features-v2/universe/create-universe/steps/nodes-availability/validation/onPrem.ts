import * as Yup from 'yup';
import { OnPremNodeState } from '../../../../../../helpers/dtos';
import { ProviderNode } from '../../../../../../utils/dtos';
import { NodeAvailabilityProps } from '../dtos';
import { ValidationArgs } from './common';

export type OnPremValidationContext = {
  isOnPrem: boolean;
  freeNodesByAzUuid?: Record<string, number>;
  isOnPremNodesLoaded: boolean;
};

export type OnPremNodeViolation = {
  az_name: string;
  requested_count: number;
  free_count: number;
  region_code: string;
  zone_index: number;
};

export function computeFreeNodesByAzUuid(nodes: ProviderNode[]): Record<string, number> {
  const counts: Record<string, number> = {};
  for (const node of nodes) {
    if (node.state !== OnPremNodeState.FREE || !node.zoneUuid) {
      continue;
    }
    counts[node.zoneUuid] = (counts[node.zoneUuid] ?? 0) + 1;
  }
  return counts;
}

export function findFirstOnPremNodeViolation(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  freeNodesByAzUuid: Record<string, number>
): OnPremNodeViolation | null {
  for (const [regionCode, zones] of Object.entries(availabilityZones)) {
    for (let index = 0; index < zones.length; index += 1) {
      const zone = zones[index];
      if (!zone.uuid || typeof zone.nodeCount !== 'number' || zone.nodeCount < 1) {
        continue;
      }
      const freeCount = freeNodesByAzUuid[zone.uuid] ?? 0;
      if (zone.nodeCount > freeCount) {
        return {
          az_name: zone.name || zone.uuid,
          requested_count: zone.nodeCount,
          free_count: freeCount,
          region_code: regionCode,
          zone_index: index
        };
      }
    }
  }
  return null;
}

export function validateOnPremFreeNodesPerAz({
  availabilityZones,
  path,
  createError,
  onPremContext
}: ValidationArgs & { onPremContext?: OnPremValidationContext }): Yup.ValidationError[] {
  const fieldErrors: Yup.ValidationError[] = [];
  if (!onPremContext?.isOnPrem) {
    return fieldErrors;
  }

  if (!onPremContext.isOnPremNodesLoaded) {
    fieldErrors.push(
      createError({
        path,
        message: 'errMsg.onPremNodesLoading'
      })
    );
    return fieldErrors;
  }

  const freeNodesByAzUuid = onPremContext.freeNodesByAzUuid ?? {};
  const violation = findFirstOnPremNodeViolation(availabilityZones, freeNodesByAzUuid);
  if (violation) {
    fieldErrors.push(
      createError({
        path,
        message: 'errMsg.onPremFreeNodesExceeded'
      })
    );
    fieldErrors.push(
      createError({
        path: `availabilityZones.${violation.region_code}.${violation.zone_index}.nodeCount`,
        message: 'errMsg.onPremFreeNodesExceeded'
      })
    );
  }

  return fieldErrors;
}
