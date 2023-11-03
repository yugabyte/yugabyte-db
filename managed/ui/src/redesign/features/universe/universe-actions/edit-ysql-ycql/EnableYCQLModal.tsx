import { FC } from 'react';
import _ from 'lodash';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useForm, FormProvider, Controller } from 'react-hook-form';
import { toast } from 'react-toastify';
import { Box, Divider, Typography } from '@material-ui/core';
import {
  YBModal,
  YBToggleField,
  YBCheckboxField,
  YBPasswordField,
  YBLabel,
  YBInput,
  YBTooltip
} from '../../../../components';
import { Universe } from '../../universe-form/utils/dto';
import { api } from '../../../../utils/api';
import { getPrimaryCluster, createErrorMessage } from '../../universe-form/utils/helpers';
import {
  YCQLFormFields,
  YCQLFormPayload,
  RotatePasswordPayload,
  DATABASE_NAME,
  YCQL_USER_NAME
} from './Helper';
import { PASSWORD_REGEX, TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import { dbSettingStyles } from './DBSettingStyles';
//icons
import InfoMessageIcon from '../../../../assets/info-message.svg';

interface EnableYCQLModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
  enforceAuth: boolean;
  isItKubernetesUniverse: boolean;
}

const MAX_PORT = 65535;
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const EnableYCQLModal: FC<EnableYCQLModalProps> = ({
  open,
  onClose,
  universeData,
  enforceAuth,
  isItKubernetesUniverse
}) => {
  const { t } = useTranslation();
  const classes = dbSettingStyles();
  const { universeDetails, universeUUID } = universeData;
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));

  const formMethods = useForm<YCQLFormFields>({
    defaultValues: {
      enableYCQL: primaryCluster?.userIntent?.enableYCQL ?? true,
      enableYCQLAuth:
        primaryCluster?.userIntent?.enableYCQL && enforceAuth
          ? true
          : primaryCluster?.userIntent?.enableYCQLAuth ?? true,
      overridePorts: false,
      yqlServerHttpPort: universeDetails.communicationPorts.yqlServerHttpPort,
      yqlServerRpcPort: universeDetails.communicationPorts.yqlServerRpcPort,
      ycqlPassword: '',
      ycqlConfirmPassword: '',
      rotateYCQLPassword: false,
      ycqlCurrentPassword: '',
      ycqlNewPassword: '',
      ycqlConfirmNewPassword: ''
    },
    mode: 'onTouched',
    reValidateMode: 'onChange'
  });
  const {
    control,
    watch,
    handleSubmit,
    formState: { isDirty }
  } = formMethods;

  //watchers
  const enableYCQLValue = watch('enableYCQL');
  const enableYCQLAuthValue = watch('enableYCQLAuth');
  const ycqlPasswordValue = watch('ycqlPassword');
  const rotateYCQLPasswordValue = watch('rotateYCQLPassword');
  const ycqlNewPasswordValue = watch('ycqlNewPassword');
  const overridePortsValue = watch('overridePorts');

  //Enable or Disable  YCQL and YCQLAuth
  const updateYCQLSettings = useMutation(
    (values: YCQLFormPayload) => {
      return api.updateYCQLSettings(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success(
          t('universeActions.editYCQLSettings.updateSettingsSuccessMsg'),
          TOAST_OPTIONS
        );
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  //Rotate YCQL Password
  const rotateYCQLPassword = useMutation(
    (values: Partial<RotatePasswordPayload>) => {
      return api.rotateDBPassword(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success(t('universeActions.editYCQLSettings.rotatePwdSuccessMsg'), TOAST_OPTIONS);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    if (values.rotateYCQLPassword) {
      //Rotate password if rotateYCQLPassword is true
      const payload: Partial<RotatePasswordPayload> = {
        dbName: DATABASE_NAME,
        ycqlAdminUsername: YCQL_USER_NAME,
        ycqlCurrAdminPassword: values.ycqlCurrentPassword,
        ycqlAdminPassword: values.ycqlNewPassword
      };
      try {
        await rotateYCQLPassword.mutateAsync(payload);
      } catch (e) {
        console.log(e);
      }
    } else {
      //Update YCQL settings if it is turned off
      let payload: YCQLFormPayload = {
        enableYCQL: values.enableYCQL ?? false,
        enableYCQLAuth: values.enableYCQL && values.enableYCQLAuth ? values.enableYCQLAuth : false,
        ycqlPassword: values.ycqlPassword ?? '',
        CommunicationPorts: {
          yqlServerHttpPort:
            values.overridePorts && values.yqlServerHttpPort
              ? values.yqlServerHttpPort
              : universeDetails.communicationPorts.yqlServerHttpPort,
          yqlServerRpcPort:
            values.overridePorts && values.yqlServerRpcPort
              ? values.yqlServerRpcPort
              : universeDetails.communicationPorts.yqlServerRpcPort
        }
      };
      try {
        await updateYCQLSettings.mutateAsync(payload);
      } catch (e) {
        console.log(e);
      }
    }
  });

  return (
    <YBModal
      open={open}
      titleSeparator
      size="sm"
      overrideHeight="auto"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.applyChanges')}
      title={t('universeActions.editYCQLSettings.modalTitle')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      submitTestId="EnableYCQLModal-Submit"
      cancelTestId="EnableYCQLModal-Close"
      buttonProps={{
        primary: {
          disabled: !isDirty
        }
      }}
    >
      <FormProvider {...formMethods}>
        <Box
          mb={4}
          mt={2}
          display="flex"
          width="100%"
          flexDirection="column"
          data-testid="EnableYCQLModal-Container"
        >
          <Box className={classes.mainContainer} mb={4}>
            <Box
              display="flex"
              flexDirection="row"
              alignItems="center"
              justifyContent="space-between"
            >
              <Typography variant="h6">
                {t('universeActions.editYCQLSettings.ycqlToggleLabel')}&nbsp;
                <YBTooltip title={t('universeForm.securityConfig.authSettings.enableYCQLHelper')}>
                  <img alt="Info" src={InfoMessageIcon} />
                </YBTooltip>
              </Typography>
              <YBTooltip
                title={
                  !primaryCluster?.userIntent?.enableYSQL && primaryCluster?.userIntent?.enableYCQL // user can disable only one endpoint among YSQL and YCQL
                    ? t('universeForm.securityConfig.authSettings.enableYsqlOrYcql')
                    : rotateYCQLPasswordValue
                    ? t('universeActions.editYCQLSettings.rotateBothYCQLWarning') // user can rotate password only if YCQL and auth is enabled
                    : ''
                }
                placement="top-end"
              >
                <div>
                  <YBToggleField
                    name={'enableYCQL'}
                    inputProps={{
                      'data-testid': 'EnableYCQLModal-Toggle'
                    }}
                    control={control}
                    disabled={
                      rotateYCQLPasswordValue ||
                      (!primaryCluster?.userIntent?.enableYSQL &&
                        primaryCluster?.userIntent?.enableYCQL) // user can disable only one endpoint among YSQL and YCQL
                    }
                  />
                </div>
              </YBTooltip>
            </Box>
            {!enableYCQLValue && primaryCluster?.userIntent?.enableYCQLAuth && (
              <Box flex={1} mt={2} width="300px">
                <YBPasswordField
                  rules={{
                    required: t('universeForm.validation.required', {
                      field: t('universeForm.securityConfig.authSettings.ycqlAuthPassword')
                    }) as string
                  }}
                  name={'ycqlPassword'}
                  control={control}
                  fullWidth
                  inputProps={{
                    autoComplete: 'previous-password',
                    'data-testid': 'YCQLField-PasswordLabelInput'
                  }}
                  placeholder={t('universeActions.editYCQLSettings.currentPwdToAuth')}
                />
              </Box>
            )}
          </Box>
          {enableYCQLValue && (
            <Box className={classes.mainContainer}>
              {!enforceAuth && (
                <Box
                  display="flex"
                  flexDirection="row"
                  alignItems="center"
                  justifyContent="space-between"
                >
                  <Typography variant="h6">
                    {t('universeActions.editYCQLSettings.authToggleLabel')}&nbsp;
                    <YBTooltip
                      title={t('universeForm.securityConfig.authSettings.enableYCQLAuthHelper')}
                    >
                      <img alt="Info" src={InfoMessageIcon} />
                    </YBTooltip>
                  </Typography>
                  <YBTooltip
                    title={
                      rotateYCQLPasswordValue
                        ? t('universeActions.editYCQLSettings.rotateBothYCQLWarning')
                        : ''
                    }
                    placement="top-end"
                  >
                    <div>
                      <YBToggleField
                        name={'enableYCQLAuth'}
                        inputProps={{
                          'data-testid': 'EnableYCQLModal-AuthToggle'
                        }}
                        control={control}
                        disabled={rotateYCQLPasswordValue}
                      />
                    </div>
                  </YBTooltip>
                </Box>
              )}
              {!enableYCQLAuthValue && primaryCluster?.userIntent?.enableYCQLAuth && (
                <Box flex={1} mt={2} width="300px">
                  <YBPasswordField
                    rules={{
                      required: t('universeForm.validation.required', {
                        field: t('universeForm.securityConfig.authSettings.ycqlAuthPassword')
                      }) as string
                    }}
                    name={'ycqlPassword'}
                    control={control}
                    fullWidth
                    inputProps={{
                      autoComplete: 'previous-password',
                      'data-testid': 'YCQLField-PasswordLabelInput'
                    }}
                    placeholder={t('universeActions.editYCQLSettings.currentPwdToAuth')}
                  />
                </Box>
              )}
              {enableYCQLAuthValue && !primaryCluster?.userIntent?.enableYCQLAuth && (
                <>
                  <Box flex={1} mt={2} width="300px">
                    <YBPasswordField
                      name={'ycqlPassword'}
                      rules={{
                        required: enableYCQLAuthValue
                          ? (t('universeForm.validation.required', {
                              field: t('universeForm.securityConfig.authSettings.ycqlAuthPassword')
                            }) as string)
                          : '',
                        pattern: {
                          value: PASSWORD_REGEX,
                          message: t('universeForm.validation.passwordStrength')
                        },
                        deps: ['ycqlConfirmPassword', 'enableYCQLAuth']
                      }}
                      control={control}
                      fullWidth
                      inputProps={{
                        autoComplete: 'new-password',
                        'data-testid': 'YCQLField-PasswordLabelInput'
                      }}
                      placeholder={t('universeForm.securityConfig.placeholder.enterYCQLPassword')}
                    />
                  </Box>
                  <Box flex={1} mt={2} mb={2} width="300px">
                    <YBPasswordField
                      name={'ycqlConfirmPassword'}
                      control={control}
                      rules={{
                        validate: {
                          passwordMatch: (value) =>
                            (enableYCQLAuthValue && value === ycqlPasswordValue) ||
                            (t('universeForm.validation.confirmPassword') as string)
                        },
                        deps: ['ycqlPassword', 'enableYCQLAuth']
                      }}
                      fullWidth
                      inputProps={{
                        autoComplete: 'new-password',
                        'data-testid': 'YCQLField-ConfirmPasswordInput'
                      }}
                      placeholder={t('universeForm.securityConfig.placeholder.confirmYCQLPassword')}
                    />
                  </Box>
                </>
              )}
              {!isItKubernetesUniverse && (
                <>
                  <Box
                    display="flex"
                    flexDirection="row"
                    alignItems="center"
                    justifyContent="space-between"
                    mt={2}
                  >
                    <Typography variant="h6">
                      {t('universeActions.editYCQLSettings.overridePortsLabel')} &nbsp;
                    </Typography>
                    <YBTooltip
                      title={
                        rotateYCQLPasswordValue
                          ? t('universeActions.editYCQLSettings.rotateBothYCQLWarning')
                          : ''
                      }
                      placement="top-end"
                    >
                      <div>
                        <YBToggleField
                          name={'overridePorts'}
                          inputProps={{
                            'data-testid': 'EnableYCQLModal-OverrideToggle'
                          }}
                          control={control}
                          disabled={rotateYCQLPasswordValue}
                        />
                      </div>
                    </YBTooltip>
                  </Box>
                  {overridePortsValue && (
                    <>
                      <Controller
                        name="yqlServerHttpPort"
                        render={({ field: { value, onChange } }) => {
                          return (
                            <Box
                              flex={1}
                              mt={2}
                              display={'flex'}
                              width="100%"
                              flexDirection={'row'}
                              alignItems={'center'}
                            >
                              <Box flexShrink={1}>
                                <YBLabel dataTestId={`EnableYCQLModal-yqlServerHttpPort`}>
                                  {t(`universeForm.advancedConfig.yqlServerHttpPort`)}
                                </YBLabel>
                              </Box>

                              <Box flex={1} display={'flex'} width="300px">
                                <YBInput
                                  disabled={rotateYCQLPasswordValue}
                                  value={value}
                                  onChange={onChange}
                                  onBlur={(event) => {
                                    let port =
                                      Number(event.target.value.replace(/\D/g, '')) ||
                                      universeDetails.communicationPorts.yqlServerHttpPort;
                                    port = port > MAX_PORT ? MAX_PORT : port;
                                    onChange(port);
                                  }}
                                  inputProps={{
                                    'data-testid': 'EnableYCQLModal-Input-yqlServerHttpPort'
                                  }}
                                />
                              </Box>
                            </Box>
                          );
                        }}
                      />
                      <Controller
                        name="yqlServerRpcPort"
                        render={({ field: { value, onChange } }) => {
                          return (
                            <Box
                              flex={1}
                              mt={2}
                              display={'flex'}
                              width="100%"
                              flexDirection={'row'}
                              alignItems={'center'}
                            >
                              <Box flexShrink={1}>
                                <YBLabel dataTestId={`EnableYCQLModal-yqlServerRpcPort`}>
                                  {t(`universeForm.advancedConfig.yqlServerRpcPort`)}
                                </YBLabel>
                              </Box>

                              <Box flex={1} display={'flex'} width="300px">
                                <YBInput
                                  disabled={rotateYCQLPasswordValue}
                                  value={value}
                                  onChange={onChange}
                                  onBlur={(event) => {
                                    let port =
                                      Number(event.target.value.replace(/\D/g, '')) ||
                                      universeDetails.communicationPorts.yqlServerRpcPort;
                                    port = port > MAX_PORT ? MAX_PORT : port;
                                    onChange(port);
                                  }}
                                  inputProps={{
                                    'data-testid': 'EnableYCQLModal-Input-yqlServerRpcPort'
                                  }}
                                />
                              </Box>
                            </Box>
                          );
                        }}
                      />
                    </>
                  )}
                </>
              )}
              {/* rotate password section */}
              {enableYCQLAuthValue && primaryCluster?.userIntent?.enableYCQLAuth && (
                <>
                  {!enforceAuth && (
                    <Box mt={2}>
                      <Divider />
                    </Box>
                  )}
                  <Box mt={2} display="flex" flexDirection={'column'}>
                    <Typography variant="h6">
                      {t('universeActions.editYCQLSettings.YCQLPwdLabel')}
                    </Typography>
                    <Box
                      mt={1}
                      display={'flex'}
                      flexDirection={'row'}
                      className={classes.rotatePwdContainer}
                    >
                      <YBCheckboxField
                        name={'rotateYCQLPassword'}
                        label={t('universeActions.editYCQLSettings.rotatePwdLabel')}
                        control={control}
                        inputProps={{
                          'data-testid': 'RotateYCQLPassword-Checkbox'
                        }}
                      />
                    </Box>
                    {rotateYCQLPasswordValue && (
                      <>
                        <Box flex={1} mt={2} width="300px">
                          <YBPasswordField
                            rules={{
                              required: enableYCQLAuthValue
                                ? (t('universeForm.validation.required', {
                                    field: t(
                                      'universeForm.securityConfig.authSettings.ycqlAuthPassword'
                                    )
                                  }) as string)
                                : ''
                            }}
                            name={'ycqlCurrentPassword'}
                            control={control}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YCQLField-PasswordLabelInput'
                            }}
                            placeholder={t('universeActions.editYCQLSettings.currentPwd')}
                          />
                        </Box>
                        <Box flex={1} mt={2} width="300px">
                          <YBPasswordField
                            rules={{
                              required: enableYCQLAuthValue
                                ? (t('universeForm.validation.required', {
                                    field: t(
                                      'universeForm.securityConfig.authSettings.ycqlAuthPassword'
                                    )
                                  }) as string)
                                : '',
                              pattern: {
                                value: PASSWORD_REGEX,
                                message: t('universeForm.validation.passwordStrength')
                              },
                              deps: ['ycqlConfirmNewPassword', 'enableYCQLAuth']
                            }}
                            name={'ycqlNewPassword'}
                            control={control}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YCQLField-PasswordLabelInput'
                            }}
                            placeholder={t('universeActions.editYCQLSettings.newPwd')}
                          />
                        </Box>
                        <Box flex={1} mt={2} mb={2} width="300px">
                          <YBPasswordField
                            name={'ycqlConfirmNewPassword'}
                            control={control}
                            rules={{
                              validate: {
                                passwordMatch: (value) =>
                                  (enableYCQLAuthValue && value === ycqlNewPasswordValue) ||
                                  (t('universeForm.validation.confirmPassword') as string)
                              },
                              deps: ['ycqlNewPassword', 'enableYCQLAuth']
                            }}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YCQLField-ConfirmPasswordInput'
                            }}
                            placeholder={t('universeActions.editYCQLSettings.reEnterNewPwd')}
                          />
                        </Box>
                      </>
                    )}
                  </Box>
                </>
              )}
            </Box>
          )}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
