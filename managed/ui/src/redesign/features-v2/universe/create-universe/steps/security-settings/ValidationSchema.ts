import * as Yup from 'yup';
import { useTranslation } from 'react-i18next';
import { SecuritySettingsProps } from './dtos';

export const SecurityValidationSchema = () => {
  const { t } = useTranslation();

  return Yup.object<Partial<SecuritySettingsProps>>({
    kmsConfig: Yup.string().when('enableEncryptionAtRest', (ear, field) => {
      return ear === true ? field.required(t('createUniverseV2.validation.fieldRequired')) : field;
    })
  });
};
