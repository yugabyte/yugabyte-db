import { AxiosError } from 'axios';
import { SubmitHandler, useForm } from 'react-hook-form';
import { browserHistory } from 'react-router';
import { makeStyles, Typography, useTheme } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';

import { YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { api, drConfigQueryKey, EditDrConfigRequest } from '../../../../redesign/helpers/api';
import { IStorageConfig as BackupStorageConfig } from '../../../backupv2';
import {
  ReactSelectStorageConfigField,
  StorageConfigOption
} from '../../sharedComponents/ReactSelectStorageConfig';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { isActionFrozen } from '../../../../redesign/helpers/utils';
import { AllowedTasks } from '../../../../redesign/helpers/dtos';
import { UNIVERSE_TASKS } from '../../../../redesign/helpers/constants';
import { INPUT_FIELD_WIDTH_PX } from '../../constants';
import { DrConfig } from '../dtos';

import InfoIcon from '../../../../redesign/assets/info-message.svg';

interface EditConfigModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
  allowedTasks: AllowedTasks;
  redirectUrl?: string;
}

interface EditConfigFormValues {
  storageConfig: StorageConfigOption;
}

const useStyles = makeStyles((theme) => ({
  formSectionDescription: {
    marginBottom: theme.spacing(3)
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    marginBottom: theme.spacing(1)
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));

const MODAL_NAME = 'EditDrConfigModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editModal';

/**
 * This modal handles editing editing:
 * - Backup storage config used for DR
 */
export const EditConfigModal = ({
  drConfig,
  modalProps,
  redirectUrl,
  allowedTasks
}: EditConfigModalProps) => {
  const classes = useStyles();
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
      const editDrConfigRequest: EditDrConfigRequest = {
        bootstrapParams: {
          backupRequestParams: {
            storageConfigUUID: formValues.storageConfig.value.uuid
          }
        }
      };
      return api.editDrConfig(drConfig.uuid, editDrConfigRequest);
    },
    {
      onSuccess: (response) => {
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
    defaultValues: {}
  });
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
      <div className={classes.formSectionDescription}>
        <Typography variant="body2">
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.infoText`}
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
              />
            </Typography>
          }
        >
          <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
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
    </YBModal>
  );
};
