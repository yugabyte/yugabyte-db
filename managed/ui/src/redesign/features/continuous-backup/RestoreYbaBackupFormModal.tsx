import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, Typography, useTheme, Box, FormHelperText } from '@material-ui/core';
import { SubmitHandler, useForm, Controller } from 'react-hook-form';
import { AxiosError } from 'axios';
import clsx from 'clsx';
import { useQuery } from 'react-query';
import { toast } from 'react-toastify';

import { YBInputField, YBModal, YBModalProps, YBButton } from '@app/redesign/components';
import { BackupStorageConfigSelectField } from './BackupStorageConfigSelectField';
import { assertUnreachableCase, handleServerError } from '@app/utils/errorHandlingUtils';
import { useRestoreContinuousBackup } from '@app/v2/api/continuous-backup/continuous-backup';
import { useRestoreYbaBackup } from '@app/v2/api/isolated-backup/isolated-backup';
import { RedirectToStorageConfigConfigurationModal } from './RedirectToStorageConfigConfigurationModal';
import { INPUT_FIELD_WIDTH_PX } from './constants';
import { getStorageConfigs } from './utils';
import { fetchTaskUntilItCompletes } from '@app/actions/xClusterReplication';
import { api, CUSTOMER_CONFIG_QUERY_KEY } from '@app/redesign/helpers/api';
import SelectedIcon from '@app/redesign/assets/circle-selected.svg';
import UnselectedIcon from '@app/redesign/assets/circle-empty.svg';
import WarningIcon from '@app/redesign/assets/alert.svg';
import { RbacValidator } from '../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../rbac/ApiAndUserPermMapping';

import toastStyles from '../../../redesign/styles/toastStyles.module.scss';

interface RestoreYbaBackupFormModalProps {
  modalProps: YBModalProps;
}

interface RestoreYbaBackupFormValues {
  backupType?: BackupType;
  localDirectory?: string;

  storageConfig?: { value: string; label: string };
  backupFolder?: string;
}

const useStyles = makeStyles((theme) => ({
  instructionsHeader: {
    marginBottom: theme.spacing(2)
  },
  optionCard: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',

    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`,

    background: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px',

    '&:hover': {
      cursor: 'pointer'
    },
    '&$selected': {
      background: theme.palette.ybacolors.backgroundBlueLight,
      border: `2px solid ${theme.palette.ybacolors.borderBlue}`
    }
  },
  selected: {},
  configureBackupStorageConfigBanner: {
    display: 'flex',
    alignItems: 'flexStart',
    gap: theme.spacing(3),

    marginTop: theme.spacing(4),
    padding: theme.spacing(3),

    background: theme.palette.background.paper,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.shape.borderRadius,
    boxShadow: '0 8px 16px 0 rgba(0, 0, 0, 0.08)'
  },
  waveIcon: {
    fontSize: 32
  },
  accentedText: {
    color: theme.palette.ybacolors.ybPurple,
    fontSize: 15,
    fontWeight: 600,
    lineHeight: '20px'
  },
  bannerContent: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  configureBackupStorageButton: {
    marginTop: theme.spacing(2),
    padding: theme.spacing(0.5, 1),
    height: 30,
    width: 'fit-content'
  },
  stepContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2.5)
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',
    marginBottom: theme.spacing(1)
  },
  formContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(5),

    padding: theme.spacing(2.5),
    marginTop: theme.spacing(3),

    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.shape.borderRadius
  },
  inputField: {
    width: 550
  },
  warningBanner: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    marginTop: theme.spacing(2),
    padding: theme.spacing(1),

    backgroundColor: theme.palette.warning[100],
    borderRadius: theme.shape.borderRadius
  }
}));

const MODAL_NAME = 'RestoreYbaBackupFormModal';
const TRANSLATION_KEY_PREFIX = 'continuousBackup.restoreYbaBackupFormModal';

const BackupType = {
  LOCAL: 'local',
  CLOUD: 'cloud'
} as const;
type BackupType = typeof BackupType[keyof typeof BackupType];

export const RestoreYbaBackupFormModal = ({ modalProps }: RestoreYbaBackupFormModalProps) => {
  const [isRedirectConfirmationModalOpen, setIsRedirectConfirmationModalOpen] = useState(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const theme = useTheme();
  const restoreContinuousBackup = useRestoreContinuousBackup();
  const restoreYbaBackup = useRestoreYbaBackup();

  const customerConfigsQuery = useQuery(CUSTOMER_CONFIG_QUERY_KEY, () => api.getCustomerConfig(), {
    select: getStorageConfigs
  });

  const formMethods = useForm<RestoreYbaBackupFormValues>({
    defaultValues: {
      backupType: BackupType.CLOUD,
      localDirectory: '',

      storageConfig: undefined,
      backupFolder: undefined
    }
  });

  const onSubmit: SubmitHandler<RestoreYbaBackupFormValues> = async (formValues) => {
    if (!formValues.backupType) {
      // This shouldn't be reached.
      // Field level validation should have prevented form submission if backupType is not defined.
      return;
    }

    try {
      switch (formValues.backupType) {
        case BackupType.LOCAL:
          return restoreYbaBackup.mutateAsync(
            {
              data: { local_path: formValues.localDirectory ?? '' }
            },
            {
              onSuccess: (response) => {
                const handleTaskCompletion = (error: boolean) => {
                  if (error) {
                    toast.error(
                      <span className={toastStyles.toastMessage}>
                        <i className="fa fa-exclamation-circle" />
                        <Typography variant="body2" component="span">
                          {t('toast.restoreBackupTaskFailed')}
                        </Typography>
                        <a
                          href={`/tasks/${response.task_uuid}`}
                          rel="noopener noreferrer"
                          target="_blank"
                        >
                          {t('viewDetails', { keyPrefix: 'task' })}
                        </a>
                      </span>
                    );
                  } else {
                    toast.success(
                      <Typography variant="body2">{t('toast.restoreBackupTaskSuccess')}</Typography>
                    );
                  }
                };
                modalProps.onClose();
                fetchTaskUntilItCompletes(response.task_uuid ?? '', handleTaskCompletion);
              },
              onError: (error: Error | AxiosError) =>
                handleServerError(error, {
                  customErrorLabel: t('toast.restoreBackupRequestFailedLabel')
                })
            }
          );
        case BackupType.CLOUD:
          return restoreContinuousBackup.mutateAsync(
            {
              data: {
                storage_config_uuid: formValues.storageConfig?.value ?? '',
                backup_dir: formValues.backupFolder ?? ''
              }
            },
            {
              onSuccess: (response) => {
                modalProps.onClose();
              },
              onError: (error: Error | AxiosError) =>
                handleServerError(error, {
                  customErrorLabel: t('toast.restoreBackupRequestFailedLabel')
                })
            }
          );
        default:
          return assertUnreachableCase(formValues.backupType);
      }
    } catch (error) {
      // Already handled by the onError callback
      return;
    }
  };

  const openRedirectConfirmationModal = () => setIsRedirectConfirmationModalOpen(true);
  const closeRedirectConfirmationModal = () => setIsRedirectConfirmationModalOpen(false);
  const handleOptionCardClick = (
    backupType: BackupType,
    onChange: (backupType: BackupType) => void
  ) => {
    onChange(backupType);
  };

  const backupType = formMethods.watch('backupType');

  const isCloudBackupSelected = backupType === BackupType.CLOUD;
  const isLocalBackupSelected = backupType === BackupType.LOCAL;
  const isPreReqSatisfied = !(isCloudBackupSelected && customerConfigsQuery.data?.length === 0);

  const isFormDisabled = formMethods.formState.isSubmitting || !isPreReqSatisfied;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={formMethods.formState.isSubmitting}
      size="lg"
      minHeight="fit-content"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" justifyContent="space-between" height="100%">
        <div>
          <Typography className={classes.instructionsHeader} variant="h6">
            {t('instructions')}
          </Typography>
          <Controller
            control={formMethods.control}
            name="backupType"
            rules={{ required: t('error.backupTypeRequired') }}
            render={({ field: { onChange } }) => (
              <Box display="flex" gridGap={theme.spacing(1)}>
                <Box width="50%">
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.RESTORE_CONTINUOUS_YBA_BACKUP}
                    isControl
                    overrideStyle={{ display: 'unset' }}
                  >
                    <div
                      className={clsx(
                        classes.optionCard,
                        backupType === BackupType.CLOUD && classes.selected
                      )}
                      onClick={() => handleOptionCardClick(BackupType.CLOUD, onChange)}
                    >
                      <Typography variant="body1">{t('option.cloudBackup')}</Typography>
                      <Box display="flex" alignItems="center" marginLeft="auto">
                        {backupType === BackupType.CLOUD ? <SelectedIcon /> : <UnselectedIcon />}
                      </Box>
                    </div>
                  </RbacValidator>
                </Box>
                <Box width="50%">
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.RESTORE_ISOLATED_YBA_BACKUP}
                    isControl
                    overrideStyle={{ display: 'unset' }}
                  >
                    <div
                      className={clsx(
                        classes.optionCard,
                        backupType === BackupType.LOCAL && classes.selected
                      )}
                      onClick={() => handleOptionCardClick(BackupType.LOCAL, onChange)}
                    >
                      <Typography variant="body1">{t('option.localBackup')}</Typography>
                      <Box display="flex" alignItems="center" marginLeft="auto">
                        {backupType === BackupType.LOCAL ? <SelectedIcon /> : <UnselectedIcon />}
                      </Box>
                    </div>
                  </RbacValidator>
                </Box>
              </Box>
            )}
          />
          {formMethods.formState.errors.backupType?.message && (
            <FormHelperText error={true}>
              {formMethods.formState.errors.backupType.message}
            </FormHelperText>
          )}
          {isCloudBackupSelected && (
            <>
              {customerConfigsQuery.data?.length === 0 && (
                <div className={classes.configureBackupStorageConfigBanner}>
                  <div className={classes.waveIcon}>👋</div>
                  <div className={classes.bannerContent}>
                    <Typography variant="body2" className={classes.accentedText}>
                      {t('cloudBackup.configureBackupStorageConfigPrompt.beforeYouStart')}
                    </Typography>
                    <Typography variant="body2">
                      {t('cloudBackup.configureBackupStorageConfigPrompt.configureBackupStorage')}
                    </Typography>
                    <YBButton
                      variant="secondary"
                      className={classes.configureBackupStorageButton}
                      onClick={openRedirectConfirmationModal}
                      data-testid={`${MODAL_NAME}-ConfigureStorageButton`}
                    >
                      {t(
                        'cloudBackup.configureBackupStorageConfigPrompt.configureBackupStorageButton'
                      )}
                    </YBButton>
                  </div>
                </div>
              )}
              <div className={classes.formContainer}>
                <div className={classes.stepContainer}>
                  <Typography variant="body1">1. {t('cloudBackup.selectStorageConfig')}</Typography>
                  <div>
                    <Typography variant="body2" className={classes.fieldLabel}>
                      {t('cloudBackup.storageConfig')}
                    </Typography>
                    <BackupStorageConfigSelectField
                      reset={formMethods.reset}
                      useControllerProps={{ control: formMethods.control, name: 'storageConfig' }}
                      autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
                    />
                  </div>
                </div>

                <div className={classes.stepContainer}>
                  <Typography variant="body1">2. {t('cloudBackup.selectBackupFolder')}</Typography>
                  <div>
                    <Typography variant="body2" className={classes.fieldLabel}>
                      {t('cloudBackup.folder')}
                    </Typography>
                    <YBInputField
                      className={classes.inputField}
                      control={formMethods.control}
                      name="backupFolder"
                      disabled={isFormDisabled}
                      fullWidth
                    />
                  </div>
                </div>
                <RedirectToStorageConfigConfigurationModal
                  open={isRedirectConfirmationModalOpen}
                  onClose={closeRedirectConfirmationModal}
                />
              </div>
            </>
          )}
          {isLocalBackupSelected && (
            <div className={classes.formContainer}>
              <div className={classes.stepContainer}>
                <Typography variant="body1">{t('localBackup.provideBackupFilePath')}</Typography>
                <div>
                  <Typography variant="body2" className={classes.fieldLabel}>
                    {t('localBackup.filePath')}
                  </Typography>
                  <YBInputField
                    control={formMethods.control}
                    name="localDirectory"
                    placeholder={t('localBackup.filePathPlaceholder')}
                    disabled={isFormDisabled}
                    fullWidth
                  />
                </div>
              </div>
            </div>
          )}
        </div>
        <div className={classes.warningBanner}>
          <Box width={24} height={24}>
            <WarningIcon width={24} height={24} color={theme.palette.warning[700]} />
          </Box>
          <Typography variant="body2">
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.ybaRestartWarning`}
              components={{ bold: <b /> }}
            />
          </Typography>
        </div>
      </Box>
    </YBModal>
  );
};
