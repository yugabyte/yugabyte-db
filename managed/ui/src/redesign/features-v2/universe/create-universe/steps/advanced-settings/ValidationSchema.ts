import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { OtherAdvancedProps } from './dtos';

export const OtherAdvancedValidationSchema = (t: TFunction) => {
  return Yup.object<Partial<OtherAdvancedProps>>({
    accessKeyCode: Yup.string().required(t('accessKeyRequired'))
  });
};
