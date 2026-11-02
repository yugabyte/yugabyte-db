import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { GeneralSettingsProps } from './dtos';
import { api } from '../../../../../helpers/api';
import { MAX_PROVIDER_CONFIGURATIONS } from '../../helpers/providerConfigurations';

export const GeneralSettingsValidationSchema = (t: TFunction) => {
  return Yup.object<GeneralSettingsProps>({
    universeName: Yup.string()
      .required(t('errMsg.universeNameRequired'))
      .test(
        'uniqueUniverseName',
        'Error',
        async function validateUniqueUniverseName(this: Yup.TestContext, value: string | null | undefined) {
          if (!value) return true;
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
