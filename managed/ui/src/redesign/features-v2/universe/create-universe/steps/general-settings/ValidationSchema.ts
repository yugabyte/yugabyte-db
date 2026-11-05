import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { GeneralSettingsProps } from './dtos';
import { api } from '../../../../../helpers/api';
import { MAX_PROVIDER_CONFIGURATIONS } from '../../helpers/providerConfigurations';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  LOWERCASE_ONLY_NAME_CLOUDS,
  LOWERCASE_ONLY_NAME_REGEX
} from '../../../../../features/universe/universe-form/utils/constants';

// Keep in sync with Util.UNIVERSE_NAME_REGEX in the backend (Util.java).
export const UNIVERSE_NAME_REGEX = /^[a-zA-Z0-9]([-a-zA-Z0-9]*[a-zA-Z0-9])?$/;

export const GeneralSettingsValidationSchema = (t: TFunction) => {
  return Yup.object<GeneralSettingsProps>({
    universeName: Yup.string()
      .required(t('errMsg.universeNameRequired'))
      .matches(UNIVERSE_NAME_REGEX, t('errMsg.universeNameInvalid'))
      .test(
        'lowercaseUniverseName',
        'Error',
        function validateLowercaseName(this: Yup.TestContext, value: string | null | undefined) {
          if (!value) return true;
          const cloud = (this.parent?.providerConfigurations ?? []).find((p: { code: CloudType }) =>
            LOWERCASE_ONLY_NAME_CLOUDS.includes(p?.code)
          )?.code;
          if (!cloud || LOWERCASE_ONLY_NAME_REGEX.test(value)) return true;
          return this.createError({
            message: t('errMsg.universeNameLowercase', {
              cloudType: cloud === CloudType.gcp ? 'GCP' : 'Kubernetes'
            })
          });
        }
      )
      .test(
        'uniqueUniverseName',
        'Error',
        async function validateUniqueUniverseName(this: Yup.TestContext, value: string | null | undefined) {
          // yup runs every test even after an earlier one fails, so skip the API call for bad names.
          if (!value || !UNIVERSE_NAME_REGEX.test(value)) return true;
          try {
            const universes = await api.findUniverseByName(value);
            if (universes.length === 0) return true;
            return this.createError({
              message: t('errMsg.universeNameUnique', { display_name: value })
            });
          } catch {
            return true;
          }
        }
      ),
    providerConfigurations: Yup.array()
      .of(Yup.object().required())
      .min(1, t('errMsg.providerConfigurationRequired'))
      .max(MAX_PROVIDER_CONFIGURATIONS, t('errMsg.providerLimitReachedBody'))
      .required(t('errMsg.providerConfigurationRequired')) as any,
    cloud: Yup.lazy((value) =>
      Array.isArray(value)
        ? (Yup.array()
            .of(Yup.string().required())
            .min(1, t('errMsg.cloudProviderRequired'))
            .max(MAX_PROVIDER_CONFIGURATIONS, t('errMsg.providerLimitReachedBody'))
            .required(t('errMsg.cloudProviderRequired')) as any)
        : Yup.string().required(t('errMsg.cloudProviderRequired'))
    ),
    spanAcrossProviders: Yup.boolean().optional(),
    databaseVersion: Yup.string()
      .typeError(t('errMsg.databaseVersionRequired'))
      .required(t('errMsg.databaseVersionRequired'))
  });
};
