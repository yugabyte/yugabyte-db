import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { uniq, values } from 'lodash';
import { NodeAvailabilityProps, Zone } from './dtos';
import { FaultToleranceType, ResilienceAndRegionsProps } from '../resilence-regions/dtos';
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
      if (
        resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.NODE_LEVEL &&
        nodeCounts < faultToleranceNeeded
      ) {
        return createError({
          path,
          message: t(useDedicatedNodes ? 'errMsg.lessNodesDedicated' : 'errMsg.lessNodes', {
            nodeCount: nodeCounts,
            faultToleranceNeeded: faultToleranceNeeded
          })
        });
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
