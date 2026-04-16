import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { OtherAdvancedProps } from './dtos';
import { CloudType } from '@app/redesign/helpers/dtos';

export const OtherAdvancedValidationSchema = (
  t: TFunction,
  providerCode: CloudType | undefined
) => {
  return Yup.object<Partial<OtherAdvancedProps>>({
    accessKeyCode:
      providerCode !== CloudType.kubernetes
        ? Yup.string().required(t('accessKeyRequired'))
        : Yup.string().notRequired()
  });
};
