import { ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid } from '@material-ui/core';
import { YBLabel, YBPasswordField, YBToggleField, YBTooltip } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import {
  YCQL_AUTH_FIELD,
  YCQL_FIELD,
  YCQL_PASSWORD_FIELD,
  YCQL_CONFIRM_PASSWORD_FIELD,
  PASSWORD_REGEX,
  YSQL_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
import InfoMessageIcon from '../../../../../../assets/info-message.svg';

interface YCQLFieldProps {
  disabled: boolean;
  enforceAuth?: boolean;
}

export const YCQLField = ({ disabled, enforceAuth }: YCQLFieldProps): ReactElement => {
  const {
    control,
    setValue,
    formState: { errors }
  } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  // Tooltip messages
  const YCQLTooltipText = t('universeForm.securityConfig.authSettings.enableYCQLHelper');
  const YCQLAuthTooltipText = t('universeForm.securityConfig.authSettings.enableYCQLAuthHelper');

  //watchers
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ycqlAuthEnabled = useWatch({ name: YCQL_AUTH_FIELD });
  const ycqlPassword = useWatch({ name: YCQL_PASSWORD_FIELD });

  //ycqlAuthEnabled cannot be true if ycqlEnabled is false
  useUpdateEffect(() => {
    if (['false', false].includes(ycqlEnabled)) setValue(YCQL_AUTH_FIELD, false);
  }, [ycqlEnabled]);

  return (
    <Box display="flex" width="100%" flexDirection="column" data-testid="YCQLField-Container">
      <Box display="flex">
        <YBTooltip
          title={
            !ysqlEnabled
              ? (t('universeForm.securityConfig.authSettings.enableYsqlOrYcql') as string)
              : ''
          }
          placement="top-start"
        >
          <div>
            <YBToggleField
              name={YCQL_FIELD}
              inputProps={{
                'data-testid': 'YCQLField-EnableToggle'
              }}
              control={control}
              disabled={disabled || !ysqlEnabled}
            />
          </div>
        </YBTooltip>
        <Box flex={1} alignSelf="center">
          <YBLabel dataTestId="YCQLField-EnableLabel">
            {t('universeForm.securityConfig.authSettings.enableYCQL')}
            &nbsp;
            <YBTooltip title={YCQLTooltipText}>
              <img alt="Info" src={InfoMessageIcon} />
            </YBTooltip>
          </YBLabel>
        </Box>
      </Box>

      {ycqlEnabled && (
        <Box mt={3}>
          {!enforceAuth && (
            <Box display="flex" flexDirection="row">
              {/* <Box flex={1}> */}
              <YBToggleField
                name={YCQL_AUTH_FIELD}
                inputProps={{
                  'data-testid': 'YCQLField-AuthToggle'
                }}
                control={control}
                disabled={disabled}
              />
              <Box flex={1} alignSelf="center">
                <YBLabel dataTestId="YCQLField-AuthLabel">
                  {t('universeForm.securityConfig.authSettings.enableYCQLAuth')}
                  &nbsp;
                  <YBTooltip title={YCQLAuthTooltipText}>
                    <img alt="Info" src={InfoMessageIcon} />
                  </YBTooltip>
                </YBLabel>
              </Box>
            </Box>
          )}

          {(ycqlAuthEnabled || enforceAuth) && !disabled && (
            <Box display="flex" mt={3}>
              <Grid container spacing={3}>
                <Grid item sm={12} lg={10}>
                  <Box display="flex">
                    <Box mt={2}>
                      <YBLabel dataTestId="YCQLField-PasswordLabel">
                        {t('universeForm.securityConfig.authSettings.ycqlAuthPassword')}
                      </YBLabel>
                    </Box>
                    <Box flex={1} className={classes.defaultTextBox}>
                      <YBPasswordField
                        name={YCQL_PASSWORD_FIELD}
                        control={control}
                        rules={{
                          required:
                            !disabled && ycqlAuthEnabled
                              ? (t('universeForm.validation.required', {
                                  field: t(
                                    'universeForm.securityConfig.authSettings.ycqlAuthPassword'
                                  )
                                }) as string)
                              : '',
                          pattern: {
                            value: PASSWORD_REGEX,
                            message: t('universeForm.validation.passwordStrength')
                          }
                        }}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'YCQLField-PasswordLabelInput'
                        }}
                        error={!!errors?.instanceConfig?.ycqlPassword}
                        helperText={errors?.instanceConfig?.ycqlPassword?.message}
                        placeholder={t('universeForm.securityConfig.placeholder.enterYCQLPassword')}
                      />
                    </Box>
                  </Box>
                </Grid>
                <Grid item sm={12} lg={10}>
                  <Box display="flex">
                    <Box mt={2}>
                      <YBLabel dataTestId="YCQLField-ConfirmPasswordLabel">
                        {t('universeForm.securityConfig.authSettings.confirmPassword')}
                      </YBLabel>
                    </Box>
                    <Box flex={1} className={classes.defaultTextBox}>
                      <YBPasswordField
                        name={YCQL_CONFIRM_PASSWORD_FIELD}
                        control={control}
                        rules={{
                          validate: {
                            passwordMatch: (value) =>
                              (ycqlAuthEnabled && value === ycqlPassword) ||
                              (t('universeForm.validation.confirmPassword') as string)
                          },
                          deps: [YCQL_PASSWORD_FIELD, YCQL_AUTH_FIELD]
                        }}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'YCQLField-ConfirmPasswordInput'
                        }}
                        error={!!errors?.instanceConfig?.ycqlConfirmPassword}
                        helperText={errors?.instanceConfig?.ycqlConfirmPassword?.message ?? ''}
                        placeholder={t(
                          'universeForm.securityConfig.placeholder.confirmYCQLPassword'
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
