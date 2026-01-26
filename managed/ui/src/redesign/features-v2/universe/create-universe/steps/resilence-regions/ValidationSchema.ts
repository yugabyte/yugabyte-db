import * as Yup from 'yup';
import { TFunction } from 'i18next';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './dtos';
import { getFaultToleranceNeeded, getFaultToleranceNeededForAZ } from '../../CreateUniverseUtils';
import { SINGLE_AVAILABILITY_ZONE } from '../../fields/FieldNames';

export const ResilienceAndRegionsSchema = (t: TFunction) => {
  return Yup.object<ResilienceAndRegionsProps>({
    resilienceFormMode: Yup.mixed<ResilienceFormMode>().required(t('errMsg.resilienceFormMode')),
    resilienceType: Yup.mixed<ResilienceType>().required(t('errMsg.resilienceType')),
    regions: Yup.array().min(1, t('errMsg.regions')),
    [SINGLE_AVAILABILITY_ZONE]: Yup.string().when('resilienceType', {
      is: ResilienceType.SINGLE_NODE,
      then: Yup.string().required(t('errMsg.singleAvailabilityZoneRequired')),
      otherwise: Yup.string().notRequired()
    }),
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
      const fieldErrors: Yup.ValidationError[] = [];

      switch (faultToleranceType) {
        case FaultToleranceType.NONE:
          return true;
        case FaultToleranceType.REGION_LEVEL: {
          const faultToleranceNeeded = getFaultToleranceNeeded(replicationFactor);
          if (faultToleranceNeeded === regions.length) {
            return true;
          }
          fieldErrors.push(
            createError({
              message: t('errMsg.regionErr'),
              path
            }),
            createError({
              message: t('errMsg.regionFieldRegionsFew', {
                required_regions: faultToleranceNeeded
              }),
              path: 'regions'
            })
          );
          break;
        }
        case FaultToleranceType.AZ_LEVEL: {
          const faultToleranceNeeded = getFaultToleranceNeededForAZ(replicationFactor);
          const azCount = regions.reduce((acc, region) => {
            return acc + region.zones.length;
          }, 0);

          if (regions.length > faultToleranceNeeded) {
            fieldErrors.push(createError({ message: 'errMsg.azErrMany', path }));
          }
          if (faultToleranceNeeded <= azCount) {
            return true;
          }

          fieldErrors.push(createError({ message: 'errMsg.azErrFew', path }));
          fieldErrors.push(
            createError({
              message: t('errMsg.regionFieldAzFew', {
                az_needed: faultToleranceNeeded,
                available_az: azCount
              }),
              path: 'regions'
            })
          );
          break;
        }
        case FaultToleranceType.NODE_LEVEL: {
          if (regions.length > 1) {
            fieldErrors.push(createError({ message: 'errMsg.nodeErr', path }));
            break;
          }
          return true;
        }
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
    })
  } as any);
};
