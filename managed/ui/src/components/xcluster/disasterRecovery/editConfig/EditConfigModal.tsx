import { useEffect } from 'react';
import { AxiosError } from 'axios';
import { SubmitHandler, useForm } from 'react-hook-form';
import { browserHistory } from 'react-router';
import { Box, Typography, useTheme } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';

import InfoIcon from '../../../../redesign/assets/info-message.svg?img';
import { YBInputField, YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { api, drConfigQueryKey, EditDrConfigRequest } from '../../../../redesign/helpers/api';
import { CustomerConfig as BackupStorageConfig } from '../../../backupv2';
import {
  ReactSelectStorageConfigField,
  StorageConfigOption
} from '../../sharedComponents/ReactSelectStorageConfig';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { isActionFrozen } from '../../../../redesign/helpers/utils';
import { I18N_KEY_PREFIX_XCLUSTER_TERMS, INPUT_FIELD_WIDTH_PX } from '../../constants';
import {
  I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX,
  UNIVERSE_TASKS
} from '@app/redesign/helpers/constants';
import {
  DurationUnit,
  DURATION_UNIT_TO_SECONDS,
  PITR_RETENTION_PERIOD_UNIT_OPTIONS
} from '../constants';
import {
  convertSecondsToLargestDurationUnit,
  formatRetentionPeriod,
  getPitrRetentionPeriodMinValue
} from '../utils';
import {
  ReactSelectOption,
  YBReactSelectField
} from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';

import { AllowedTasks } from '../../../../redesign/helpers/dtos';
import { DrConfig } from '../dtos';

import { useModalStyles } from '../../styles';

interface EditConfigModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
  allowedTasks: AllowedTasks;
  redirectUrl?: string;
}

interface EditConfigFormValues {
  storageConfig: StorageConfigOption;
  pitrRetentionPeriodValue: number;
  pitrRetentionPeriodUnit: { label: string; value: DurationUnit };
}

const MODAL_NAME = 'EditDrConfigModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editModal';

/**
 * This modal handles editing:
 * - Backup storage config used for DR
 * - PITR Retention time.
 */
export const EditConfigModal = ({
  drConfig,
  modalProps,
  redirectUrl,
  allowedTasks
}: EditConfigModalProps) => {
  const classes = useModalStyles();
  const queryClient = useQueryClient();
  const theme = useTheme();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data?.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );

  const editDrConfigMutation = useMutation(
    (formValues: EditConfigFormValues) => {
      const retentionPeriodSec =
        formValues.pitrRetentionPeriodValue *
        DURATION_UNIT_TO_SECONDS[formValues.pitrRetentionPeriodUnit.value];

      const editDrConfigRequest: EditDrConfigRequest = {
        bootstrapParams: {
          backupRequestParams: {
            storageConfigUUID: formValues.storageConfig.value.uuid
          }
        },
        pitrParams: {
          retentionPeriodSec: retentionPeriodSec
        }
      };
      return api.editDrConfig(drConfig.uuid, editDrConfigRequest);
    },
    {
      onSuccess: () => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));
        };

        modalProps.onClose();
        if (redirectUrl) {
          browserHistory.push(redirectUrl);
        }
        invalidateQueries();
        toast.success(
          <Typography variant="body2" component="span">
            {t('success.requestSuccess')}
          </Typography>
        );
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, {
          customErrorLabel: t('error.requestFailureLabel')
        })
    }
  );

  const formMethods = useForm<EditConfigFormValues>({
    defaultValues: getDefaultValues(drConfig, storageConfigs)
  });

  const pitrRetentionPeriodValue = formMethods.watch('pitrRetentionPeriodValue');
  const pitrRetentionPeriodUnit = formMethods.watch('pitrRetentionPeriodUnit.value');
  const pitrRetentionPeriodMinValue = getPitrRetentionPeriodMinValue(pitrRetentionPeriodUnit);
  useEffect(() => {
    // Changing the retention period unit might require revalidation of the value.
    if (pitrRetentionPeriodUnit !== undefined) {
      formMethods.trigger('pitrRetentionPeriodValue');
    }
  }, [pitrRetentionPeriodUnit]);

  const handlePitrRetentionPeriodUnitChange = (option: ReactSelectOption) => {
    const pitrRetentionPeriodMinValue = getPitrRetentionPeriodMinValue(
      option.value as DurationUnit
    );
    // `pitrRetentionPeriodValue` is undefined when the user hasn't entered a value yet.
    if (
      pitrRetentionPeriodValue === undefined ||
      pitrRetentionPeriodMinValue > pitrRetentionPeriodValue
    ) {
      formMethods.setValue('pitrRetentionPeriodValue', pitrRetentionPeriodMinValue, {
        shouldValidate: true
      });
    }
  };

  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });

  const onSubmit: SubmitHandler<EditConfigFormValues> = (formValues) => {
    return editDrConfigMutation.mutateAsync(formValues);
  };

  const currentBackupStorageConfig = storageConfigs.find(
    (storageConfig) =>
      storageConfig.configUUID === drConfig.bootstrapParams.backupRequestParams.storageConfigUUID
  );
  const defaultBackupStorageConfigOption = currentBackupStorageConfig
    ? {
        value: {
          uuid: currentBackupStorageConfig.configUUID,
          name: currentBackupStorageConfig.name
        },
        label: currentBackupStorageConfig.configName
      }
    : undefined;
  const isEditActionFrozen = isActionFrozen(allowedTasks, UNIVERSE_TASKS.EDIT_DR);
  const isFormDisabled = formMethods.formState.isSubmitting || isEditActionFrozen;

  return (
    <YBModal
      title={modalTitle}
      submitLabel={t('applyChanges', { keyPrefix: 'common' })}
      cancelLabel={cancelLabel}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={formMethods.formState.isSubmitting}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(4)}>
        <div>
          <div className={classes.formSectionDescription}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.backupStorageConfigInfoText`}
                components={{ bold: <b />, paragraph: <p /> }}
              />
            </Typography>
          </div>
          <div className={classes.fieldLabel}>
            <Typography variant="body2">{t('backupStorageConfig.label')}</Typography>
            <YBTooltip
              title={
                <Typography variant="body2">
                  <Trans
                    i18nKey={`${TRANSLATION_KEY_PREFIX}.backupStorageConfig.tooltip`}
                    components={{ paragraph: <p />, bold: <b /> }}
                    values={{
                      sourceUniverseTerm: t('source.dr', {
                        keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                      }),
                      targetUniverseTerm: t('target.dr', {
                        keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                      })
                    }}
                  />
                </Typography>
              }
            >
              <img
                src={InfoIcon}
                alt={t('infoIcon', { keyPrefix: I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX })}
              />
            </YBTooltip>
          </div>
          <ReactSelectStorageConfigField
            control={formMethods.control}
            name="storageConfig"
            rules={{ required: t('error.backupStorageConfigRequired') }}
            isDisabled={isFormDisabled}
            autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
            maxWidth="100%"
            defaultValue={defaultBackupStorageConfigOption}
          />
        </div>
        <div>
          <div className={classes.formSectionDescription}>
            <Typography variant="body2">{t('pitrInfoText')}</Typography>
          </div>
          <div className={classes.fieldLabel}>
            <Typography variant="body2">{t('retentionPeriod.label')}</Typography>
            <YBTooltip
              title={<Typography variant="body2">{t('retentionPeriod.tooltip')}</Typography>}
            >
              <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
            </YBTooltip>
          </div>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
            <Box display="flex" gridGap={theme.spacing(1)} alignItems="flex-start">
              <YBInputField
                control={formMethods.control}
                name="pitrRetentionPeriodValue"
                type="number"
                inputProps={{ min: pitrRetentionPeriodMinValue }}
                rules={{
                  required: t('error.pitrRetentionPeriodValueRequired'),
                  validate: {
                    pattern: (value) => {
                      const integerPattern = /^\d+$/;
                      return (
                        integerPattern.test(value?.toString() ?? '') ||
                        t('error.pitrRetentionPeriodValueIntegerValidation')
                      );
                    },
                    min: (value) => {
                      return (
                        (value as number) >= pitrRetentionPeriodMinValue ||
                        t('error.pitrRetentionPeriodValueMinimum')
                      );
                    }
                  }
                }}
                disabled={isFormDisabled}
              />
              <YBReactSelectField
                control={formMethods.control}
                name="pitrRetentionPeriodUnit"
                onChange={handlePitrRetentionPeriodUnitChange}
                options={PITR_RETENTION_PERIOD_UNIT_OPTIONS}
                autoSizeMinWidth={200}
                maxWidth="100%"
                rules={{ required: t('error.pitrRetentionPeriodUnitRequired') }}
                isDisabled={isFormDisabled}
              />
            </Box>
            <Typography variant="body2">
              {t('currentRetentionPeriod', {
                retentionPeriod: formatRetentionPeriod(drConfig.pitrRetentionPeriodSec)
              })}
            </Typography>
          </Box>
        </div>
      </Box>
    </YBModal>
  );
};

const getDefaultValues = (drConfig: DrConfig, storageConfigs: BackupStorageConfig[]) => {
  const currentBackupStorageConfig = storageConfigs.find(
    (storageConfig) =>
      storageConfig.configUUID === drConfig.bootstrapParams.backupRequestParams.storageConfigUUID
  );
  const defaultBackupStorageConfigOption = currentBackupStorageConfig
    ? {
        value: {
          uuid: currentBackupStorageConfig.configUUID,
          name: currentBackupStorageConfig.name
        },
        label: currentBackupStorageConfig.configName
      }
    : undefined;
  const {
    value: pitrRetentionPeriodValue,
    unit: durationUnit
  } = convertSecondsToLargestDurationUnit(drConfig.pitrRetentionPeriodSec, { noThrow: true });
  const pitrRetentionPeriodUnit = PITR_RETENTION_PERIOD_UNIT_OPTIONS.find(
    (option) => option.value === durationUnit
  );
  return {
    storageConfig: defaultBackupStorageConfigOption,
    // Fall back to hours if we can't find a matching duration unit.
    pitrRetentionPeriodValue: pitrRetentionPeriodUnit
      ? pitrRetentionPeriodValue
      : Math.max(1, Math.round(drConfig.pitrRetentionPeriodSec / 3600)),
    pitrRetentionPeriodUnit: pitrRetentionPeriodUnit
      ? pitrRetentionPeriodUnit
      : PITR_RETENTION_PERIOD_UNIT_OPTIONS.find((option) => option.value === DurationUnit.HOUR)
  };
};
