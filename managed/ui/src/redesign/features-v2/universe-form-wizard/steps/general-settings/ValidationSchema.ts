import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { GeneralSettingsProps } from './dtos';

export const GeneralSettingsValidationSchema = (t: TFunction) => {
  return Yup.object<GeneralSettingsProps>({
    universeName: Yup.string().required(t('errMsg.universeNameRequired')),
    providerConfiguration: Yup.object()
      .typeError(t('errMsg.providerConfigurationRequired'))
      .required(t('errMsg.providerConfigurationRequired')) as any,
    cloud: Yup.string().required(t('errMsg.cloudProviderRequired')),
    databaseVersion: Yup.string()
      .typeError(t('errMsg.databaseVersionRequired'))
      .required(t('errMsg.databaseVersionRequired'))
  });
};
