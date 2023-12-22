import { ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid } from '@material-ui/core';
import { YBLabel, YBPasswordField, YBToggleField, YBTooltip } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import {
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YSQL_PASSWORD_FIELD,
  YSQL_CONFIRM_PASSWORD_FIELD,
  PASSWORD_REGEX,
  YCQL_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
import InfoMessageIcon from '../../../../../../assets/info-message.svg';

interface YSQLFieldProps {
  disabled: boolean;
  enforceAuth?: boolean;
}

export const YSQLField = ({ disabled, enforceAuth }: YSQLFieldProps): ReactElement => {
  const {
    control,
    setValue,
    formState: { errors }
  } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  // Tooltip messages
  const YSQLTooltipText = t('universeForm.securityConfig.authSettings.enableYSQLHelper');
  const YSQLAuthTooltipText = t('universeForm.securityConfig.authSettings.enableYSQLAuthHelper');

  //watchers
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const ysqlAuthEnabled = useWatch({ name: YSQL_AUTH_FIELD });
  const ysqlPassword = useWatch({ name: YSQL_PASSWORD_FIELD });

  //ysqlAuthEnabled cannot be true if ysqlEnabled is false
  useUpdateEffect(() => {
    if (['false', false].includes(ysqlEnabled)) setValue(YSQL_AUTH_FIELD, false);
  }, [ysqlEnabled]);

  return (
    <Box display="flex" width="100%" flexDirection="column" data-testid="YSQLField-Container">
      <Box display="flex">
        <YBTooltip
          title={
            !ycqlEnabled
              ? (t('universeForm.securityConfig.authSettings.enableYsqlOrYcql') as string)
              : ''
          }
          placement="top-start"
        >
          <div>
            <YBToggleField
              name={YSQL_FIELD}
              inputProps={{
                'data-testid': 'YSQLField-EnableToggle'
              }}
              control={control}
              disabled={disabled || !ycqlEnabled}
            />
          </div>
        </YBTooltip>
        <Box flex={1} alignSelf="center">
          <YBLabel dataTestId="YSQLField-EnableLabel">
            {t('universeForm.securityConfig.authSettings.enableYSQL')}
            &nbsp;
            <YBTooltip title={YSQLTooltipText}>
              <img alt="Info" src={InfoMessageIcon} />
            </YBTooltip>
          </YBLabel>
        </Box>
      </Box>

      {ysqlEnabled && (
        <Box mt={3}>
          {!enforceAuth && (
            <Box display="flex" flexDirection="row">
              <YBToggleField
                name={YSQL_AUTH_FIELD}
                inputProps={{
                  'data-testid': 'YSQLField-AuthToggle'
                }}
                control={control}
                disabled={disabled}
              />
              <Box flex={1} alignSelf="center">
                <YBLabel dataTestId="YSQLField-AuthLabel">
                  {t('universeForm.securityConfig.authSettings.enableYSQLAuth')}
                  &nbsp;
                  <YBTooltip title={YSQLAuthTooltipText}>
                    <img alt="Info" src={InfoMessageIcon} />
                  </YBTooltip>
                </YBLabel>
              </Box>
            </Box>
          )}

          {(ysqlAuthEnabled || enforceAuth) && !disabled && (
            <Box display="flex" flexDirection="column" mt={3}>
              <Grid container spacing={3} direction="column">
                <Grid item sm={12} lg={10}>
                  <Box display="flex">
                    <Box mt={2}>
                      <YBLabel dataTestId="YSQLField-PasswordLabel">
                        {t('universeForm.securityConfig.authSettings.ysqlAuthPassword')}
                      </YBLabel>
                    </Box>
                    <Box flex={1} className={classes.defaultTextBox}>
                      <YBPasswordField
                        rules={{
                          required:
                            !disabled && ysqlAuthEnabled
                              ? (t('universeForm.validation.required', {
                                  field: t(
                                    'universeForm.securityConfig.authSettings.ysqlAuthPassword'
                                  )
                                }) as string)
                              : '',
                          pattern: {
                            value: PASSWORD_REGEX,
                            message: t('universeForm.validation.passwordStrength')
                          }
                        }}
                        name={YSQL_PASSWORD_FIELD}
                        control={control}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'YSQLField-PasswordLabelInput'
                        }}
                        error={!!errors?.instanceConfig?.ysqlPassword}
                        helperText={errors?.instanceConfig?.ysqlPassword?.message}
                        placeholder={t('universeForm.securityConfig.placeholder.enterYSQLPassword')}
                      />
                    </Box>
                  </Box>
                </Grid>
                <Grid item sm={12} lg={10}>
                  <Box display="flex">
                    <Box mt={2}>
                      <YBLabel dataTestId="YSQLField-ConfirmPasswordLabel">
                        {t('universeForm.securityConfig.authSettings.confirmPassword')}
                      </YBLabel>
                    </Box>
                    <Box flex={1} className={classes.defaultTextBox}>
                      <YBPasswordField
                        name={YSQL_CONFIRM_PASSWORD_FIELD}
                        control={control}
                        rules={{
                          validate: {
                            passwordMatch: (value) =>
                              (ysqlAuthEnabled && value === ysqlPassword) ||
                              (t('universeForm.validation.confirmPassword') as string)
                          },
                          deps: [YSQL_PASSWORD_FIELD, YSQL_AUTH_FIELD]
                        }}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'YSQLField-ConfirmPasswordInput'
                        }}
                        error={!!errors?.instanceConfig?.ysqlConfirmPassword}
                        helperText={errors?.instanceConfig?.ysqlConfirmPassword?.message}
                        placeholder={t(
                          'universeForm.securityConfig.placeholder.confirmYSQLPassword'
                        )}
                      />
                    </Box>
                  </Box>
                </Grid>
              </Grid>
            </Box>
          )}
        </Box>
      )}
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
