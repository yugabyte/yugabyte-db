import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { uniq, values } from 'lodash';
import { NodeAvailabilityProps, Zone } from './dtos';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode
} from '../resilence-regions/dtos';
import { getFaultToleranceNeededForAZ, getNodeCount } from '../../CreateUniverseUtils';

export const NodesAvailabilitySchema = (
  t: TFunction,
  resilienceAndRegionsProps?: ResilienceAndRegionsProps
) => {
  return Yup.object<NodeAvailabilityProps>({
    lesserNodes: Yup.number().test('availabilityZones', 'Error', function () {
      const { path, createError } = this;
      const { availabilityZones, useDedicatedNodes } = this.parent;
      const nodeCounts = getNodeCount(availabilityZones);
      const faultToleranceNeeded = getFaultToleranceNeededForAZ(
        resilienceAndRegionsProps?.replicationFactor ?? 1
      );
      const azCounts = values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0);
      const fieldErrors: Yup.ValidationError[] = [];
      if (resilienceAndRegionsProps?.resilienceFormMode === ResilienceFormMode.FREE_FORM) {
        if (azCounts === faultToleranceNeeded) return true;

        return createError({
          path,
          message: t('errMsg.lessNodes', {
            nodeCount: azCounts,
            faultToleranceNeeded: faultToleranceNeeded
          })
        });
      }
      if (resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.AZ_LEVEL) {
        if (azCounts !== faultToleranceNeeded) {
          fieldErrors.push(
            createError({
              path,
              message: t('errMsg.azErrFew', {
                required_zones: faultToleranceNeeded,
                availability_zone: azCounts,
                keyPrefix: 'createUniverseV2.resilienceAndRegions'
              })
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
            message: t(useDedicatedNodes ? 'errMsg.lessNodesDedicated' : 'errMsg.lessNodes', {
              nodeCount: nodeCounts,
              faultToleranceNeeded: faultToleranceNeeded
            })
          })
        );
      }
      if (
        resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.NONE &&
        nodeCounts > 1
      ) {
        fieldErrors.push(
          createError({
            path,
            message: t('errMsg.moreNodesNone', {
              keyPrefix: 'createUniverseV2.resilienceAndRegions'
            })
          })
        );
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
    nodeCountPerAz: Yup.number().test('availabilityZones', 'Error', function () {
      const preferredValues = values(this.parent.availabilityZones)
        .flatMap((zones) => zones.map((zone: Zone) => zone.preffered))
        .filter((v) => typeof v === 'number')
        .sort((a, b) => a - b);
      const unique = [...uniq(preferredValues)];
      const min = Math.min(...unique);
      const max = Math.max(...unique);

      for (let i = min; i <= max; i++) {
        if (!unique.includes(i)) {
          return this.createError({
            path: this.path,
            message: t('errMsg.missingPreferredValue', { missingValue: i + 1 })
          });
        }
      }
      return true;
    })
  } as any);
};
