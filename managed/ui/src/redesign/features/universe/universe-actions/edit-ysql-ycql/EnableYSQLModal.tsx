import { FC, useState } from 'react';
import _ from 'lodash';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { toast } from 'react-toastify';
import { Box, Divider, Typography } from '@material-ui/core';
import {
  YBModal,
  YBToggleField,
  YBCheckboxField,
  YBPasswordField,
  YBTooltip,
  YBAlert,
  AlertVariant
} from '../../../../components';
import { Universe } from '../../universe-form/utils/dto';
import { api } from '../../../../utils/api';
import { getPrimaryCluster, createErrorMessage } from '../../universe-form/utils/helpers';
import {
  YSQLFormFields,
  YSQLFormPayload,
  RotatePasswordPayload,
  DATABASE_NAME,
  YSQL_USER_NAME
} from './Helper';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { PASSWORD_REGEX, TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import { dbSettingStyles } from './DBSettingStyles';
//icons
import InfoMessageIcon from '../../../../assets/info-message.svg';

interface EnableYSQLModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
  enforceAuth: boolean;
}

const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const EnableYSQLModal: FC<EnableYSQLModalProps> = ({
  open,
  onClose,
  universeData,
  enforceAuth
}) => {
  const { t } = useTranslation();
  const classes = dbSettingStyles();
  const { universeDetails, universeUUID } = universeData;
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));
  const [warningModalProps, setWarningModalProps] = useState<YSQLFormPayload>();

  const formMethods = useForm<YSQLFormFields>({
    defaultValues: {
      enableYSQL: primaryCluster?.userIntent?.enableYSQL ?? true,
      enableYSQLAuth:
        primaryCluster?.userIntent?.enableYSQL && enforceAuth
          ? true
          : primaryCluster?.userIntent?.enableYSQLAuth ?? true,
      ysqlPassword: '',
      ysqlConfirmPassword: '',
      rotateYSQLPassword: false,
      ysqlCurrentPassword: '',
      ysqlNewPassword: '',
      ysqlConfirmNewPassword: ''
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
  const enableYSQLValue = watch('enableYSQL');
  const enableYSQLAuthValue = watch('enableYSQLAuth');
  const ysqlPasswordValue = watch('ysqlPassword');
  const rotateYSQLPasswordValue = watch('rotateYSQLPassword');
  const ysqlNewPasswordValue = watch('ysqlNewPassword');

  //Enable or Disable  YSQL and YSQLAuth
  const updateYSQLSettings = useMutation(
    (values: YSQLFormPayload) => {
      return api.updateYSQLSettings(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success(
          t('universeActions.editYSQLSettings.updateSettingsSuccessMsg'),
          TOAST_OPTIONS
        );
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  //Rotate YSQL Password
  const rotateYSQLPassword = useMutation(
    (values: Partial<RotatePasswordPayload>) => {
      return api.rotateDBPassword(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success(t('universeActions.editYSQLSettings.rotatePwdSuccessMsg'), TOAST_OPTIONS);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    if (values.rotateYSQLPassword) {
      //Rotate password if rotateYSQLPassword is true
      const payload: Partial<RotatePasswordPayload> = {
        dbName: DATABASE_NAME,
        ysqlAdminUsername: YSQL_USER_NAME,
        ysqlCurrAdminPassword: values.ysqlCurrentPassword,
        ysqlAdminPassword: values.ysqlNewPassword
      };
      try {
        await rotateYSQLPassword.mutateAsync(payload);
      } catch (e) {
        console.log(e);
      }
    } else {
      //Update YSQL settings if it is turned off

      let payload: YSQLFormPayload = {
        enableYSQL: values.enableYSQL ?? false,
        enableYSQLAuth: values.enableYSQL && values.enableYSQLAuth ? values.enableYSQLAuth : false,
        ysqlPassword: values.ysqlPassword ?? '',
        communicationPorts: {
          ysqlServerHttpPort: universeDetails.communicationPorts.ysqlServerHttpPort,
          ysqlServerRpcPort: universeDetails.communicationPorts.ysqlServerRpcPort
        }
      };
      if (primaryCluster?.userIntent?.enableYSQL && !values.enableYSQL) {
        setWarningModalProps(payload);
      } else {
        try {
          await updateYSQLSettings.mutateAsync(payload);
        } catch (e) {
          console.log(e);
        }
      }
    }
  });

  const canUpdateYSQL = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL
  });

  return (
    <YBModal
      open={open}
      titleSeparator
      size="sm"
      overrideHeight="auto"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.applyChanges')}
      title={t('universeActions.editYSQLSettings.modalTitle')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      submitTestId="EnableYSQLModal-Submit"
      cancelTestId="EnableYSQLModal-Close"
      buttonProps={{
        primary: {
          disabled: !isDirty || !canUpdateYSQL
        }
      }}
      submitButtonTooltip={!canUpdateYSQL ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box
          mb={4}
          mt={2}
          display="flex"
          width="100%"
          flexDirection="column"
          data-testid="EnableYSQLModal-Container"
        >
          <Box className={classes.mainContainer} mb={4}>
            <Box
              display="flex"
              flexDirection="row"
              alignItems="center"
              justifyContent="space-between"
            >
              <Typography variant="h6">
                {t('universeActions.editYSQLSettings.ysqlToggleLabel')}&nbsp;
                <YBTooltip title={t('universeForm.securityConfig.authSettings.enableYSQLHelper')}>
                  <img alt="Info" src={InfoMessageIcon} />
                </YBTooltip>
              </Typography>
              <YBTooltip
                title={
                  !primaryCluster?.userIntent?.enableYSQL
                    ? t('universeActions.editYSQLSettings.cannotEnableYSQL') //user cannot enable YSQL post universe creation
                    : primaryCluster?.userIntent?.enableYSQL &&
                      !primaryCluster?.userIntent?.enableYCQL
                    ? t('universeForm.securityConfig.authSettings.enableYsqlOrYcql') // user can disable only one endpoint among YSQL and YCQL
                    : rotateYSQLPasswordValue
                    ? t('universeActions.editYSQLSettings.rotateBothYSQLWarning') // user can rotate password only if YSQL and auth is enabled
                    : ''
                }
                placement="top-end"
              >
                <div>
                  <YBToggleField
                    name={'enableYSQL'}
                    inputProps={{
                      'data-testid': 'EnableYSQLModal-Toggle'
                    }}
                    control={control}
                    disabled={
                      rotateYSQLPasswordValue ||
                      !primaryCluster?.userIntent?.enableYSQL ||
                      (primaryCluster?.userIntent?.enableYSQL &&
                        !primaryCluster?.userIntent?.enableYCQL) // user can disable only one endpoint among YSQL and YCQL
                    }
                  />
                </div>
              </YBTooltip>
            </Box>
            {!enableYSQLValue && primaryCluster?.userIntent?.enableYSQLAuth && (
              <Box flex={1} mt={2} width="300px">
                <YBPasswordField
                  rules={{
                    required: t('universeForm.validation.required', {
                      field: t('universeForm.securityConfig.authSettings.ysqlAuthPassword')
                    }) as string
                  }}
                  name={'ysqlPassword'}
                  control={control}
                  fullWidth
                  inputProps={{
                    autoComplete: 'previous-password',
                    'data-testid': 'YSQLField-PasswordLabelInput'
                  }}
                  placeholder={t('universeActions.editYSQLSettings.currentPwdToAuth')}
                />
              </Box>
            )}
          </Box>
          {enableYSQLValue && (
            <Box className={classes.mainContainer}>
              {!enforceAuth && (
                <Box
                  display="flex"
                  flexDirection="row"
                  alignItems="center"
                  justifyContent="space-between"
                >
                  <Typography variant="h6">
                    {t('universeActions.editYSQLSettings.authToggleLabel')}&nbsp;
                    <YBTooltip
                      title={t('universeForm.securityConfig.authSettings.enableYSQLAuthHelper')}
                    >
                      <img alt="Info" src={InfoMessageIcon} />
                    </YBTooltip>
                  </Typography>
                  <YBTooltip
                    title={
                      rotateYSQLPasswordValue
                        ? t('universeActions.editYSQLSettings.rotateBothYSQLWarning')
                        : ''
                    }
                    placement="top-end"
                  >
                    <div>
                      <YBToggleField
                        name={'enableYSQLAuth'}
                        inputProps={{
                          'data-testid': 'EnableYSQLModal-AuthToggle'
                        }}
                        control={control}
                        disabled={rotateYSQLPasswordValue}
                      />
                    </div>
                  </YBTooltip>
                </Box>
              )}
              {!enableYSQLAuthValue && primaryCluster?.userIntent?.enableYSQLAuth && (
                <Box flex={1} mt={2} width="300px">
                  <YBPasswordField
                    rules={{
                      required: t('universeForm.validation.required', {
                        field: t('universeForm.securityConfig.authSettings.ysqlAuthPassword')
                      }) as string
                    }}
                    name={'ysqlPassword'}
                    control={control}
                    fullWidth
                    inputProps={{
                      autoComplete: 'new-password',
                      'data-testid': 'YSQLField-PasswordLabelInput'
                    }}
                    placeholder={t('universeActions.editYSQLSettings.currentPwdToAuth')}
                  />
                </Box>
              )}
              {enableYSQLAuthValue && !primaryCluster?.userIntent?.enableYSQLAuth && (
                <>
                  <Box flex={1} mt={2} width="300px">
                    <YBPasswordField
                      name={'ysqlPassword'}
                      rules={{
                        required: enableYSQLAuthValue
                          ? (t('universeForm.validation.required', {
                              field: t('universeForm.securityConfig.authSettings.ysqlAuthPassword')
                            }) as string)
                          : '',
                        pattern: {
                          value: PASSWORD_REGEX,
                          message: t('universeForm.validation.passwordStrength')
                        },
                        deps: ['ysqlConfirmPassword', 'enableYSQLAuth']
                      }}
                      control={control}
                      fullWidth
                      inputProps={{
                        autoComplete: 'new-password',
                        'data-testid': 'YSQLField-PasswordLabelInput'
                      }}
                      placeholder={t('universeForm.securityConfig.placeholder.enterYSQLPassword')}
                    />
                  </Box>
                  <Box flex={1} mt={2} mb={2} width="300px">
                    <YBPasswordField
                      name={'ysqlConfirmPassword'}
                      control={control}
                      rules={{
                        validate: {
                          passwordMatch: (value) =>
                            (enableYSQLAuthValue && value === ysqlPasswordValue) ||
                            (t('universeForm.validation.confirmPassword') as string)
                        },
                        deps: ['ysqlPassword', 'enableYSQLAuth']
                      }}
                      fullWidth
                      inputProps={{
                        autoComplete: 'new-password',
                        'data-testid': 'YSQLField-ConfirmPasswordInput'
                      }}
                      placeholder={t('universeForm.securityConfig.placeholder.confirmYSQLPassword')}
                    />
                  </Box>
                </>
              )}
              {enableYSQLAuthValue && primaryCluster?.userIntent?.enableYSQLAuth && (
                <>
                  {!enforceAuth && (
                    <Box mt={2}>
                      <Divider />
                    </Box>
                  )}
                  <Box mt={2} display="flex" flexDirection={'column'}>
                    <Typography variant="h6">
                      {t('universeActions.editYSQLSettings.YSQLPwdLabel')}
                    </Typography>
                    <Box
                      mt={1}
                      display={'flex'}
                      flexDirection={'row'}
                      className={classes.rotatePwdContainer}
                    >
                      <YBCheckboxField
                        name={'rotateYSQLPassword'}
                        label={t('universeActions.editYSQLSettings.rotatePwdLabel')}
                        control={control}
                        inputProps={{
                          'data-testid': 'RotateYSQLPassword-Checkbox'
                        }}
                      />
                    </Box>
                    {rotateYSQLPasswordValue && (
                      <>
                        <Box flex={1} mt={2} width="300px">
                          <YBPasswordField
                            rules={{
                              required: enableYSQLAuthValue
                                ? (t('universeForm.validation.required', {
                                    field: t(
                                      'universeForm.securityConfig.authSettings.ysqlAuthPassword'
                                    )
                                  }) as string)
                                : ''
                            }}
                            name={'ysqlCurrentPassword'}
                            control={control}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YSQLField-PasswordLabelInput'
                            }}
                            placeholder={t('universeActions.editYSQLSettings.currentPwd')}
                          />
                        </Box>
                        <Box flex={1} mt={2} width="300px">
                          <YBPasswordField
                            rules={{
                              required: enableYSQLAuthValue
                                ? (t('universeForm.validation.required', {
                                    field: t(
                                      'universeForm.securityConfig.authSettings.ysqlAuthPassword'
                                    )
                                  }) as string)
                                : '',
                              pattern: {
                                value: PASSWORD_REGEX,
                                message: t('universeForm.validation.passwordStrength')
                              },
                              deps: ['ysqlConfirmNewPassword', 'enableYSQLAuth']
                            }}
                            name={'ysqlNewPassword'}
                            control={control}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YSQLField-PasswordLabelInput'
                            }}
                            placeholder={t('universeActions.editYSQLSettings.newPwd')}
                          />
                        </Box>
                        <Box flex={1} mt={2} mb={2} width="300px">
                          <YBPasswordField
                            name={'ysqlConfirmNewPassword'}
                            control={control}
                            rules={{
                              validate: {
                                passwordMatch: (value) =>
                                  (enableYSQLAuthValue && value === ysqlNewPasswordValue) ||
                                  (t('universeForm.validation.confirmPassword') as string)
                              },
                              deps: ['ysqlNewPassword', 'enableYSQLAuth']
                            }}
                            fullWidth
                            inputProps={{
                              autoComplete: 'new-password',
                              'data-testid': 'YSQLField-ConfirmPasswordInput'
                            }}
                            placeholder={t('universeActions.editYSQLSettings.reEnterNewPwd')}
                          />
                        </Box>
                      </>
                    )}
                  </Box>
                </>
              )}
            </Box>
          )}
          <YBModal
            titleSeparator
            open={Boolean(warningModalProps)}
            size="sm"
            overrideHeight="auto"
            cancelLabel={t('common.no')}
            submitLabel={t('common.yes')}
            title={t('common.areYouSure')}
            onClose={() => setWarningModalProps(undefined)}
            onSubmit={async () => {
              try {
                if (warningModalProps) await updateYSQLSettings.mutateAsync(warningModalProps);
              } catch (e) {
                console.log(e);
              }
            }}
            submitTestId="EnableYSQLModal-WarninngSubmit"
            cancelTestId="EnableYSQLModal-WarningClose"
          >
            <Box
              display="flex"
              width="100%"
              mt={2}
              mb={6}
              flexDirection="column"
              data-testid="YSQLWarningModal-Container"
            >
              <Typography variant="body2">
                {t('universeActions.editYSQLSettings.disableYSQLWarning1')}
              </Typography>
              <Box mt={2}>
                <YBAlert
                  open={true}
                  icon={<></>}
                  text={
                    <Box display="flex">
                      <Typography variant="body2" className={classes.errorNote}>
                        <b>{t('common.note')}</b>&nbsp;
                        {t('universeActions.editYSQLSettings.disableYSQLWarning2')}
                      </Typography>
                    </Box>
                  }
                  variant={AlertVariant.Error}
                />
              </Box>
            </Box>
          </YBModal>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
