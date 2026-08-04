import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { GeneralSettingsProps } from './dtos';
import { api } from '../../../../../helpers/api';

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
    providerConfiguration: Yup.object()
      .typeError(t('errMsg.providerConfigurationRequired'))
      .required(t('errMsg.providerConfigurationRequired')) as any,
    cloud: Yup.string().required(t('errMsg.cloudProviderRequired')),
    databaseVersion: Yup.string()
      .typeError(t('errMsg.databaseVersionRequired'))
      .required(t('errMsg.databaseVersionRequired'))
  });
};
