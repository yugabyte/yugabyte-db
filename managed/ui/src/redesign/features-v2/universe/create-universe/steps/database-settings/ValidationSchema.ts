import * as Yup from 'yup';
import { useTranslation } from 'react-i18next';
import { DatabaseSettingsProps } from './dtos';
import { YSQL_FIELD } from '../../fields';

const PASSWORD_REGEX = /^(?=.*[0-9])(?=.*[!@#$%^&*])(?=.*[a-z])(?=.*[A-Z])[a-zA-Z0-9!@#$%^&*]{8,256}$/;

export const DatabaseValidationSchema = () => {
  const { t } = useTranslation();

  return Yup.object<Partial<DatabaseSettingsProps>>({
    ysql: Yup.object({
      password: Yup.string().when(['enable_auth', 'enable'], {
        is: true,
        then: Yup.string()
          .required(
            t('createUniverseV2.validation.required', {
              field: t('createUniverseV2.databaseSettings.ysqlSettings.authPwd')
            }) as string
          )
          .matches(PASSWORD_REGEX, t('createUniverseV2.validation.passwordStrength')),
        otherwise: Yup.string().notRequired()
      }),
      confirm_pwd: Yup.string().when('password', (password, field) => {
        return password
          ? field
              .required('Please re-enter your password')
              .oneOf([Yup.ref('password')], t('createUniverseV2.validation.confirmPassword'))
          : field;
      })
    }),
    ycql: Yup.object({
      password: Yup.string().when(['enable_auth', 'enable'], {
        is: true,
        then: Yup.string()
          .required(
            t('createUniverseV2.validation.required', {
              field: t('createUniverseV2.databaseSettings.ycqlSettings.authPwd')
            }) as string
          )
          .matches(PASSWORD_REGEX, t('createUniverseV2.validation.passwordStrength')),
        otherwise: Yup.string().notRequired()
      }),
      confirm_pwd: Yup.string().when('password', (password, field) => {
        return password
          ? field
              .required('Please re-enter your password')
              .oneOf([Yup.ref('password')], t('createUniverseV2.validation.confirmPassword'))
          : field;
      })
    })
  }).test(
    'both-ysql-ycql-enabled',
    t('createUniverseV2.databaseSettings.enableYsqlOrYcql'),
    function (value) {
      const ysqlEnabled = value?.ysql?.enable;
      const ycqlEnabled = value?.ycql?.enable;

      // If neither is enabled, return error
      if (!ysqlEnabled && !ycqlEnabled) {
        return this.createError({
          message: t('createUniverseV2.databaseSettings.enableYsqlOrYcql'),
          path: YSQL_FIELD
        });
      }

      return true;
    }
  );
};
