import * as Yup from 'yup';
import { TFunction } from 'i18next';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './dtos';
import { getFaultToleranceNeeded } from '../../CreateUniverseUtils';
import { REGIONS_BY_PROVIDER, SINGLE_AVAILABILITY_ZONE } from '../../fields/FieldNames';
import { flattenRegionsByProvider } from '../../helpers/placementsByProvider';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';

function resolveRegionsForValidation(parent: ResilienceAndRegionsProps): Region[] {
  const byProvider = parent[REGIONS_BY_PROVIDER];
  if (byProvider && Object.keys(byProvider).length > 0) {
    return flattenRegionsByProvider(byProvider);
  }
  return parent.regions ?? [];
}

export const ResilienceAndRegionsSchema = (
  t: TFunction,
  options?: { disallowNodeAndNone?: boolean }
) => {
  return Yup.object<ResilienceAndRegionsProps>({
    resilienceFormMode: Yup.mixed<ResilienceFormMode>().required(t('errMsg.resilienceFormMode')),
    resilienceType: Yup.mixed<ResilienceType>().required(t('errMsg.resilienceType')),
    regions: Yup.array().when(REGIONS_BY_PROVIDER, {
      is: (byProvider: ResilienceAndRegionsProps[typeof REGIONS_BY_PROVIDER]) =>
        !!byProvider && Object.keys(byProvider).length > 0,
      then: Yup.array().notRequired(),
      otherwise: Yup.array().min(1, t('errMsg.regions'))
    }),
    [REGIONS_BY_PROVIDER]: Yup.object()
      .nullable()
      .notRequired()
      .test('each-provider-has-region', t('errMsg.providerRegionRequired'), function (value) {
        if (!value || Object.keys(value).length === 0) return true;
        const missing = Object.entries(value as Record<string, Region[]>).some(
          ([, regions]) => !regions || regions.length === 0
        );
        return !missing;
      }),
    [SINGLE_AVAILABILITY_ZONE]: Yup.string().when('resilienceType', {
      is: ResilienceType.SINGLE_NODE,
      then: Yup.string().required(t('errMsg.singleAvailabilityZoneRequired')),
      otherwise: Yup.string().notRequired()
    }),
    faultToleranceType: Yup.string().test('replicationFactor', 'Error', function () {
      const { path, createError } = this;
      const parent = this.parent as ResilienceAndRegionsProps;
      const {
        resilienceFactor,
        faultToleranceType,
        resilienceFormMode,
        resilienceType
      } = parent;
      const regions = resolveRegionsForValidation(parent);

      if (resilienceType === ResilienceType.SINGLE_NODE) {
        return true;
      }
      if (resilienceFormMode !== ResilienceFormMode.GUIDED) {
        return true;
      }
      if (
        options?.disallowNodeAndNone &&
        (faultToleranceType === FaultToleranceType.NODE_LEVEL ||
          faultToleranceType === FaultToleranceType.NONE)
      ) {
        return createError({
          message: 'errMsg.multiProviderFtDisabled',
          path
        });
      }
      const fieldErrors: Yup.ValidationError[] = [];
      const regionsErrorPath =
        parent[REGIONS_BY_PROVIDER] && Object.keys(parent[REGIONS_BY_PROVIDER]!).length > 0
          ? REGIONS_BY_PROVIDER
          : 'regions';

      switch (faultToleranceType) {
        case FaultToleranceType.NONE:
          return true;
        case FaultToleranceType.REGION_LEVEL: {
          const faultToleranceNeeded = getFaultToleranceNeeded(resilienceFactor);
          if (faultToleranceNeeded === regions.length) {
            return true;
          }
          const regionCountMessage =
            regions.length > faultToleranceNeeded
              ? 'errMsg.regionErrExceeds'
              : 'errMsg.regionErrTooFew';
          fieldErrors.push(
            createError({
              message: regionCountMessage,
              path
            }),
            createError({
              message: t('errMsg.regionFieldRegionsFew', {
                required_regions: faultToleranceNeeded
              }),
              path: regionsErrorPath
            })
          );
          break;
        }
        case FaultToleranceType.AZ_LEVEL: {
          const faultToleranceNeeded = getFaultToleranceNeeded(resilienceFactor);
          const azCount = regions.reduce((acc, region) => {
            return acc + (region.zones?.length ?? 0);
          }, 0);
          // Too many regions for AZ-level RF (1 AZ/region max): must not return true early
          // when azCount already meets the minimum, or azErrMany would never surface.
          if (regions.length > faultToleranceNeeded) {
            fieldErrors.push(createError({ message: 'errMsg.azErrMany', path }));
          }
          if (azCount < faultToleranceNeeded) {
            fieldErrors.push(createError({ message: 'errMsg.azErrFew', path }));
            fieldErrors.push(
              createError({
                message: t('errMsg.regionFieldAzFew', {
                  az_needed: faultToleranceNeeded,
                  available_az: azCount
                }),
                path: regionsErrorPath
              })
            );
          }
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
