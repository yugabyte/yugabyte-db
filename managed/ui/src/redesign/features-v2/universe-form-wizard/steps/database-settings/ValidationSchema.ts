import * as Yup from 'yup';
import { useTranslation } from 'react-i18next';
import { DatabaseSettingsProps } from './dtos';

const PASSWORD_REGEX = /^(?=.*[0-9])(?=.*[!@#$%^&*])(?=.*[a-z])(?=.*[A-Z])[a-zA-Z0-9!@#$%^&*]{8,256}$/;

export const DatabaseValidationSchema = () => {
  const { t } = useTranslation();

  return Yup.object<Partial<DatabaseSettingsProps>>({
    ysql: Yup.object({
      password: Yup.string().when(['enable_auth', 'enable'], {
        is: true,
        then: Yup.string()
          .required(
            t('universeForm.validation.required', {
              field: t('universeForm.securityConfig.authSettings.ysqlAuthPassword')
            }) as string
          )
          .matches(PASSWORD_REGEX, t('universeForm.validation.passwordStrength')),
        otherwise: Yup.string().notRequired()
      }),
      confirm_pwd: Yup.string().when('password', (password, field) => {
        return password
          ? field
              .required('Please re-enter your password')
              .oneOf([Yup.ref('password')], t('universeForm.validation.confirmPassword'))
          : field;
      })
    }),
    ycql: Yup.object({
      password: Yup.string().when(['enable_auth', 'enable'], {
        is: true,
        then: Yup.string()
          .required(
            t('universeForm.validation.required', {
              field: t('universeForm.securityConfig.authSettings.ycqlAuthPassword')
            }) as string
          )
          .matches(PASSWORD_REGEX, t('universeForm.validation.passwordStrength')),
        otherwise: Yup.string().notRequired()
      }),
      confirm_pwd: Yup.string().when('password', (password, field) => {
        return password
          ? field
              .required('Please re-enter your password')
              .oneOf([Yup.ref('password')], t('universeForm.validation.confirmPassword'))
          : field;
      })
    })
  });
};
