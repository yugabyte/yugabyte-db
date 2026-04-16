import { FormHelperText, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import { SubmitHandler, useForm } from 'react-hook-form';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';

import { YBInputField, YBModal, YBModalProps, YBTooltip } from '../../components';
import CheckmarkIcon from '../../assets/check.svg';
import UnavailableIcon from '../../assets/unavailable.svg';
import TipIcon from '../../assets/tip.svg';
import { I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX } from '../../helpers/constants';
import { BackupStorageConfigSelectField } from './BackupStorageConfigSelectField';
import {
  createContinuousBackup,
  editContinuousBackup
} from '../../../v2/api/continuous-backup/continuous-backup';
import { INPUT_FIELD_WIDTH_PX } from './constants';
import { ContinuousBackup, TimeUnitType } from '../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { BackupStorageConfigReactSelectOption } from './BackupStorageConfigSelect';
import { CONTINUOUS_BACKUP_QUERY_KEY } from '../../helpers/api';
import { assertUnreachableCase, handleServerError } from '../../../utils/errorHandlingUtils';

interface ConfigureContinuousBackupModalCommonProps {
  operation: ConfigureContinuousBackupOperation;
  modalProps: YBModalProps;
}

interface CreateContinuousBackupModalProps extends ConfigureContinuousBackupModalCommonProps {
  operation: typeof ConfigureContinuousBackupOperation.CREATE;
}

interface EditContinuousBackupModalProps extends ConfigureContinuousBackupModalCommonProps {
  operation: typeof ConfigureContinuousBackupOperation.EDIT;
  continuousBackupConfig: ContinuousBackup;
}

type ConfigureContinuousBackupModalProps =
  | CreateContinuousBackupModalProps
  | EditContinuousBackupModalProps;

interface ConfigureContinuousBackupFormValues {
  storageConfig: BackupStorageConfigReactSelectOption | undefined;
  storageSubfolder: string;
  backupFrequency: number;
}

const useStyles = makeStyles((theme) => ({
  modalContainer: {
    display: 'flex',
    padding: 0
  },
  formFieldsContainer: {
    display: 'flex',
    flexDirection: 'column',
    flex: 1,
    gap: theme.spacing(2),

    '& ol': {
      display: 'flex',
      flexDirection: 'column',
      gap: theme.spacing(5),

      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  fieldContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    marginTop: theme.spacing(2)
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  },
  inputField: {
    width: INPUT_FIELD_WIDTH_PX
  },
  backupFrequencyFieldContainer: {
    marginTop: theme.spacing(2)
  },
  backupFrequencyInputField: {
    width: '120px'
  },
  tipContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    width: '550px',
    marginTop: theme.spacing(2),
    padding: theme.spacing(1),

    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[200]}`,
    backgroundColor: theme.palette.ybacolors.tipBackgroundLightGray,

    '& ul': {
      display: 'flex',
      flexDirection: 'column',
      gap: theme.spacing(1.5),

      paddingInlineStart: theme.spacing(2)
    },
    '& li': {
      listStyleType: 'disc'
    }
  },
  infoPanel: {
    height: '100%',
    width: '298px',
    padding: theme.spacing(3),

    backgroundColor: theme.palette.ybacolors.backgroundGrayLight
  },
  infoCard: {
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  infoCardHeader: {
    padding: `${theme.spacing(2.5)}px ${theme.spacing(2)}px`,

    borderBottom: `1px solid ${theme.palette.grey[200]}`
  },
  backupInfoList: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    padding: `${theme.spacing(3)}px ${theme.spacing(2)}px`
  },
  backupInfoItem: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  },
  backupInfoItemIcon: {
    width: '24px',
    height: '24px',

    color: theme.palette.grey[500]
  },
  backupInfoItemText: {
    color: theme.palette.grey[700]
  },
  tooltipAnchorText: {
    marginLeft: theme.spacing(4),

    color: theme.palette.grey[700],
    cursor: 'pointer',
    textDecoration: 'underline',
    textDecorationStyle: 'dashed'
  },
  backupFrequencyInputFieldContainer: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  }
}));

/**
 * These values are mapped to i18n translation keys in src/translations/en.json
 */
export const ConfigureContinuousBackupOperation = {
  CREATE: 'create',
  EDIT: 'edit'
} as const;
export type ConfigureContinuousBackupOperation = typeof ConfigureContinuousBackupOperation[keyof typeof ConfigureContinuousBackupOperation];

const MODAL_NAME = 'ConfigureContinuousBackupModal';
const TRANSLATION_KEY_PREFIX = 'continuousBackup.configureContinuousBackupModal';
const DEFAULT_BACKUP_FREQUENCY_MINUTE = 5;

export const ConfigureContinuousBackupModal = (props: ConfigureContinuousBackupModalProps) => {
  const { modalProps } = props;
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const invalidateQueries = () => {
    queryClient.invalidateQueries(CONTINUOUS_BACKUP_QUERY_KEY);
  };
  const createContinuousBackupConfigMutation = useMutation(
    (values: ConfigureContinuousBackupFormValues) =>
      createContinuousBackup({
        storage_config_uuid: values.storageConfig?.value ?? '',
        backup_dir: values.storageSubfolder,
        frequency: values.backupFrequency,
        frequency_time_unit: TimeUnitType.MINUTES
      }),
    {
      onSuccess: () => {
        toast.success(
          <Typography variant="body2" component="span">
            {t('toast.createSuccess')}
          </Typography>
        );
        invalidateQueries();
        modalProps.onClose();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('toast.createFailedLabel') })
    }
  );

  const continuousBackupConfigUuid =
    (props.operation === ConfigureContinuousBackupOperation.EDIT
      ? props.continuousBackupConfig.info?.uuid
      : '') ?? '';
  const editContinuousBackupConfigMutation = useMutation(
    (values: ConfigureContinuousBackupFormValues) =>
      editContinuousBackup(continuousBackupConfigUuid, {
        storage_config_uuid: values.storageConfig?.value ?? '',
        backup_dir: values.storageSubfolder,
        frequency: values.backupFrequency,
        frequency_time_unit: TimeUnitType.MINUTES
      }),
    {
      onSuccess: () => {
        toast.success(
          <Typography variant="body2" component="span">
            {t('toast.editSuccess')}
          </Typography>
        );
        invalidateQueries();
        modalProps.onClose();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('toast.editFailedLabel') })
    }
  );

  const defaultStorageConfigUuid =
    (props.operation === ConfigureContinuousBackupOperation.EDIT
      ? props.continuousBackupConfig.spec?.storage_config_uuid
      : '') ?? '';
  const defaultValues =
    props.operation === ConfigureContinuousBackupOperation.EDIT
      ? {
          backupFrequency:
            props.continuousBackupConfig.spec?.frequency ?? DEFAULT_BACKUP_FREQUENCY_MINUTE,
          storageSubfolder: props.continuousBackupConfig.spec?.backup_dir
        }
      : { backupFrequency: DEFAULT_BACKUP_FREQUENCY_MINUTE };
  const formMethods = useForm<ConfigureContinuousBackupFormValues>({
    defaultValues,
    mode: 'onChange'
  });

  const onSubmit: SubmitHandler<ConfigureContinuousBackupFormValues> = async (formValues) => {
    const { operation } = props;
    switch (operation) {
      case ConfigureContinuousBackupOperation.CREATE:
        return await createContinuousBackupConfigMutation.mutateAsync(formValues);
      case ConfigureContinuousBackupOperation.EDIT:
        return await editContinuousBackupConfigMutation.mutateAsync(formValues);
      default:
        return assertUnreachableCase(operation);
    }
  };

  const modalTitle = t(`title.${props.operation}`);
  const submitLabel = t(`submitButton.${props.operation}`);
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  const isFormDisabled = formMethods.formState.isSubmitting;
  return (
    <YBModal
      title={modalTitle}
      cancelLabel={cancelLabel}
      submitLabel={submitLabel}
      overrideWidth={'1000px'}
      overrideHeight={'700px'}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      size="md"
      dialogContentProps={{ className: classes.modalContainer }}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      isSubmitting={formMethods.formState.isSubmitting}
      {...modalProps}
    >
      <div className={classes.formFieldsContainer}>
        <ol>
          <li>
            <Typography variant="body1">{t('instruction.selectStorageConfig')}</Typography>
            <div className={classes.fieldContainer}>
              <Typography variant="body2" className={classes.fieldLabel}>
                {t('label.storageConfig')}
              </Typography>
              <BackupStorageConfigSelectField
                reset={formMethods.reset}
                {...(defaultStorageConfigUuid && { defaultStorageConfigUuid })}
                useControllerProps={{
                  control: formMethods.control,
                  name: 'storageConfig',
                  rules: { required: t('formFieldRequired', { keyPrefix: 'common' }) }
                }}
                autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
              />
            </div>
          </li>
          <li>
            <Typography variant="body1">{t('instruction.createSubfolder')}</Typography>
            <div className={classes.fieldContainer}>
              <Typography variant="body2" className={classes.fieldLabel}>
                {t('label.folderName')}
              </Typography>
              <YBInputField
                className={classes.inputField}
                control={formMethods.control}
                name="storageSubfolder"
                disabled={isFormDisabled}
                rules={{
                  required: t('formFieldRequired', { keyPrefix: 'common' })
                }}
              />
            </div>
          </li>
          <li>
            <Typography variant="body1">{t('instruction.setBackupFrequency')}</Typography>
            <div className={classes.backupFrequencyFieldContainer}>
              <div className={classes.backupFrequencyInputFieldContainer}>
                <Typography variant="body2">{t('every', { keyPrefix: 'common' })}</Typography>
                <YBInputField
                  className={classes.backupFrequencyInputField}
                  control={formMethods.control}
                  name="backupFrequency"
                  type="number"
                  inputProps={{ min: 2, max: 1440 }}
                  rules={{
                    required: t('formFieldRequired', { keyPrefix: 'common' }),
                    min: {
                      value: 2,
                      message: t('error.backupFrequencyMustBeGreaterThanOrEqualTo2')
                    },
                    max: {
                      value: 1440,
                      message: t('error.backupFrequencyMustBeLessThanOrEqualTo1440')
                    },
                    validate: {
                      pattern: (value) => {
                        const integerPattern = /^\d+$/;
                        return (
                          integerPattern.test(value?.toString() ?? '') ||
                          t('error.backupFrequencyMustBePositiveInteger')
                        );
                      }
                    }
                  }}
                  hideInlineError
                  disabled={isFormDisabled}
                />
                <Typography variant="body2">
                  {t('duration.minutes', { keyPrefix: 'common' }).toLocaleLowerCase()}
                </Typography>
              </div>
              {formMethods.formState.errors.backupFrequency?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.backupFrequency.message}
                </FormHelperText>
              )}
              <div className={classes.tipContainer}>
                <div>
                  <TipIcon />
                </div>
                <ul>
                  <li>
                    <Typography variant="body2">{t('tip.performanceImpact')}</Typography>
                  </li>
                  <li>
                    <Typography variant="body2">{t('tip.copiesRetained')}</Typography>
                  </li>
                </ul>
              </div>
            </div>
          </li>
        </ol>
      </div>
      <div className={classes.infoPanel}>
        <div className={classes.infoCard}>
          <Typography className={classes.infoCardHeader} variant="body1">
            {t('whatWillBeBackedUpInfo.header')}
          </Typography>
          <div className={classes.backupInfoList}>
            <div className={classes.backupInfoItem}>
              <CheckmarkIcon
                className={classes.backupInfoItemIcon}
                title={t('checkIcon', { keyPrefix: I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX })}
              />
              <Typography className={classes.backupInfoItemText} variant="body2">
                {t('whatWillBeBackedUpInfo.platformMetadata')}
              </Typography>
            </div>
            <div className={classes.backupInfoItem}>
              <CheckmarkIcon
                className={classes.backupInfoItemIcon}
                title={t('checkIcon', { keyPrefix: I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX })}
              />
              <Typography className={classes.backupInfoItemText} variant="body2">
                {t('whatWillBeBackedUpInfo.yugabyteDbReleases')}
              </Typography>
            </div>
            <div className={classes.backupInfoItem}>
              <UnavailableIcon
                className={classes.backupInfoItemIcon}
                title={t('checkIcon', { keyPrefix: I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX })}
              />
              <Typography className={classes.backupInfoItemText} variant="body2">
                {t('whatWillBeBackedUpInfo.universeMetrics')}
              </Typography>
            </div>
            <YBTooltip title={t('whatWillBeBackedUpInfo.howToBackupThis.tooltip')}>
              <Typography
                className={clsx(classes.backupInfoItemText, classes.tooltipAnchorText)}
                variant="body2"
              >
                {t('whatWillBeBackedUpInfo.howToBackupThis.label')}
              </Typography>
            </YBTooltip>
          </div>
        </div>
      </div>
    </YBModal>
  );
};
