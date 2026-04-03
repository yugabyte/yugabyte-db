import * as Yup from 'yup';
import { uniq, values } from 'lodash';
import { NodeAvailabilityProps, Zone } from './dtos';

function collectPreferredRankErrors(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  createError: (opts: { path: string; message: string }) => Yup.ValidationError,
  /** Use lesserNodes so yupResolver surfaces errors on the same field as other node/AZ rules. */
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
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../resilence-regions/dtos';
import { getFaultToleranceNeeded, getNodeCount } from '../../CreateUniverseUtils';

export const NodesAvailabilitySchema = (resilienceAndRegionsProps?: ResilienceAndRegionsProps) => {
  return Yup.object<NodeAvailabilityProps>({
    lesserNodes: Yup.number().test('availabilityZones', 'Error', function () {
      const { path, createError } = this;
      const { availabilityZones, useDedicatedNodes } = this.parent;
      const nodeCounts = getNodeCount(availabilityZones);
      const faultToleranceNeeded = getFaultToleranceNeeded(
        resilienceAndRegionsProps?.resilienceFactor ?? 1
      );
      const azCounts = values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0);
      const fieldErrors: Yup.ValidationError[] = [];

      const resilienceType = resilienceAndRegionsProps?.resilienceType;
      const regionsFromResilience = resilienceAndRegionsProps?.regions ?? [];

      // Every selected AZ must have at least one node (Regular multi-AZ flows).
      if (
        resilienceType !== ResilienceType.SINGLE_NODE &&
        resilienceAndRegionsProps?.faultToleranceType !== FaultToleranceType.NONE
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

      // Node-level resilience: placement only in one region; guided mode caps total AZs at RF-1.
      if (
        resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.NODE_LEVEL &&
        resilienceType === ResilienceType.REGULAR
      ) {
        const nodeLevelAzCap = Math.max(1, faultToleranceNeeded - 1);
        const regionsWithZones = values(availabilityZones).filter(
          (zones) => zones && zones.length > 0
        );
        if (regionsWithZones.length !== 1) {
          fieldErrors.push(
            createError({
              path,
              message: 'errMsg.nodeLevelOneRegionOneAz'
            })
          );
        } else if (
          resilienceAndRegionsProps?.resilienceFormMode === ResilienceFormMode.GUIDED &&
          azCounts > nodeLevelAzCap
        ) {
          fieldErrors.push(
            createError({
              path,
              message: 'errMsg.nodeLevelAzTooMany'
            })
          );
        }
      }

      if (resilienceAndRegionsProps?.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
        if (azCounts !== faultToleranceNeeded) {
          fieldErrors.push(
            createError({
              path,
              message: 'errMsg.expertAzCountMismatch'
            })
          );
        }
      } else {
        if (resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.AZ_LEVEL) {
          if (azCounts !== faultToleranceNeeded) {
            fieldErrors.push(
              createError({
                path,
                message:
                  azCounts > faultToleranceNeeded
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
          resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.NODE_LEVEL &&
          nodeCounts < faultToleranceNeeded
        ) {
          fieldErrors.push(
            createError({
              path,
              message: useDedicatedNodes ? 'errMsg.lessNodesDedicated' : 'errMsg.lessNodes'
            })
          );
        }
      }

      // Preferred ranks apply only when the UI allows ranking (not NODE_LEVEL / NONE — see Zone.tsx).
      if (
        resilienceType === ResilienceType.REGULAR &&
        resilienceAndRegionsProps?.faultToleranceType !== FaultToleranceType.NONE &&
        resilienceAndRegionsProps?.faultToleranceType !== FaultToleranceType.NODE_LEVEL
      ) {
        fieldErrors.push(...collectPreferredRankErrors(availabilityZones, createError, path));
      }

      const error = new Yup.ValidationError(
        fieldErrors.map((e) => e.message),
        'errors',
        path
      );

      error.inner = fieldErrors;

      if (fieldErrors.length > 0) {
        throw error;
      }
      return true;
    }),
    nodeCountPerAz: Yup.number()
  } as any);
};
