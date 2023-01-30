import React, { ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid } from '@material-ui/core';
import {
  YBLabel,
  YBHelper,
  YBPasswordField,
  YBToggleField,
  YBTooltip
} from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import {
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YSQL_PASSWORD_FIELD,
  YSQL_CONFIRM_PASSWORD_FIELD,
  PASSWORD_REGEX,
  YCQL_FIELD
} from '../../../utils/constants';

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
  const { t } = useTranslation();

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
        <YBLabel dataTestId="YSQLField-EnableLabel">
          {t('universeForm.instanceConfig.enableYSQL')}
        </YBLabel>
        <Box flex={1}>
          <YBTooltip
            title={
              !ycqlEnabled ? (t('universeForm.instanceConfig.enableYsqlOrYcql') as string) : ''
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
              <YBHelper dataTestId="YSQLField-EnableHelper">
                {t('universeForm.instanceConfig.enableYSQLHelper')}
              </YBHelper>
            </div>
          </YBTooltip>
        </Box>
      </Box>

      {ysqlEnabled && (
        <Box mt={1}>
          {!enforceAuth && (
            <Box display="flex">
              <YBLabel dataTestId="YSQLField-AuthLabel">
                {t('universeForm.instanceConfig.enableYSQLAuth')}
              </YBLabel>
              <Box flex={1}>
                <YBToggleField
                  name={YSQL_AUTH_FIELD}
                  inputProps={{
                    'data-testid': 'YSQLField-AuthToggle'
                  }}
                  control={control}
                  disabled={disabled}
                />
                <YBHelper dataTestId="YSQLField-AuthHelper">
                  {t('universeForm.instanceConfig.enableYSQLAuthHelper')}
                </YBHelper>
              </Box>
            </Box>
          )}

          {ysqlAuthEnabled && !disabled && (
            <Box display="flex">
              <Grid container spacing={3}>
                <Grid item sm={12} lg={6}>
                  <Box display="flex">
                    <YBLabel dataTestId="YSQLField-PasswordLabel">
                      {t('universeForm.instanceConfig.ysqlAuthPassword')}
                    </YBLabel>
                    <Box flex={1}>
                      <YBPasswordField
                        rules={{
                          required:
                            !disabled && ysqlAuthEnabled
                              ? (t('universeForm.validation.required', {
                                  field: t('universeForm.instanceConfig.ysqlAuthPassword')
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
                      />
                    </Box>
                  </Box>
                </Grid>
                <Grid item sm={12} lg={6}>
                  <Box display="flex">
                    <YBLabel dataTestId="YSQLField-ConfirmPasswordLabel">
                      {t('universeForm.instanceConfig.confirmPassword')}
                    </YBLabel>
                    <Box flex={1}>
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
