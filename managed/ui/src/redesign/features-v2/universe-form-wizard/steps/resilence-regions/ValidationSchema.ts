import * as Yup from 'yup';
import { TFunction } from 'i18next';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './dtos';
import { getFaultToleranceNeeded, getFaultToleranceNeededForAZ } from '../../CreateUniverseUtils';

export const ResilienceAndRegionsSchema = (t: TFunction) => {
  return Yup.object<ResilienceAndRegionsProps>({
    resilienceFormMode: Yup.mixed<ResilienceFormMode>().required(t('errMsg.resilienceFormMode')),
    resilienceType: Yup.mixed<ResilienceType>().required(t('errMsg.resilienceType')),
    regions: Yup.array().min(1, t('errMsg.regions')),
    faultToleranceType: Yup.string().test('replicationFactor', 'Error', function () {
      const { path, createError } = this;
      const {
        regions,
        replicationFactor,
        faultToleranceType,
        resilienceFormMode,
        resilienceType
      } = this.parent as ResilienceAndRegionsProps;
      if (resilienceType === ResilienceType.SINGLE_NODE) {
        return true;
      }
      if (resilienceFormMode !== ResilienceFormMode.GUIDED) {
        return true;
      }
      switch (faultToleranceType) {
        case FaultToleranceType.NONE:
          return true;
        case FaultToleranceType.REGION_LEVEL: {
          const faultToleranceNeeded = getFaultToleranceNeeded(replicationFactor);
          if (faultToleranceNeeded === regions.length) {
            return true;
          }
          return createError({ message: 'errMsg.regionErr', path });
        }
        case FaultToleranceType.AZ_LEVEL: {
          const faultToleranceNeeded = getFaultToleranceNeededForAZ(replicationFactor);
          const azCount = regions.reduce((acc, region) => {
            return acc + region.zones.length;
          }, 0);

          if (regions.length > faultToleranceNeeded) {
            return createError({ message: 'errMsg.azErrMany', path });
          }
          if (faultToleranceNeeded <= azCount) {
            return true;
          }
          return createError({ message: 'errMsg.azErrFew', path });
        }
        case FaultToleranceType.NODE_LEVEL: {
          if (regions.length > 1) {
            return createError({ message: 'errMsg.nodeErr', path });
          }
          return true;
        }
      }
      return true;
    })
  } as any);
};
