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
        t('errMsg.universeNameUnique'),
        async function validateUniqueUniverseName(value) {
          if (!value) return true;
          try {
            const universes = await api.findUniverseByName(value);
            return universes.length === 0;
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
