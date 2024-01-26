import { AxiosError } from 'axios';
import { SubmitHandler, useForm } from 'react-hook-form';
import { browserHistory } from 'react-router';
import { makeStyles, Typography } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';

import { YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { api, drConfigQueryKey, EditDrConfigRequest } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import {
  ReactSelectStorageConfigField,
  StorageConfigOption
} from '../../sharedComponents/ReactSelectStorageConfig';

import { DrConfig } from '../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface EditConfigModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;

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
 * This modal handles editing the bootstrap parameters of an existing DR config.
 */
export const EditConfigModal = ({ drConfig, modalProps, redirectUrl }: EditConfigModalProps) => {
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  // TODO: Use Existing bootstrap storage config as default.
  const formMethods = useForm<EditConfigFormValues>();

  const editDrConfigMutation = useMutation(
    (formValues: EditConfigFormValues) => {
      const editDrConfigRequest: EditDrConfigRequest = {
        bootstrapBackupParams: {
          storageConfigUUID: formValues.storageConfig.value.uuid
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
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <Typography variant="body2" component="span">
                  {t('error.taskFailure')}
                </Typography>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t('success.taskSuccess')}
              </Typography>
            );
          }
          invalidateQueries();
        };

        modalProps.onClose();
        if (redirectUrl) {
          browserHistory.push(redirectUrl);
        }
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  const onSubmit: SubmitHandler<EditConfigFormValues> = (formValues) => {
    return editDrConfigMutation.mutateAsync(formValues);
  };

  const isFormDisabled = formMethods.formState.isSubmitting;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('applyChanges', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={formMethods.formState.isSubmitting}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <div className={classes.formSectionDescription}>
        <Typography variant="body1">{t('bootstrapConfiguration')}</Typography>
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
          <InfoIcon className={classes.infoIcon} />
        </YBTooltip>
      </div>
      <ReactSelectStorageConfigField
        control={formMethods.control}
        name="storageConfig"
        rules={{ required: t('error.backupStorageConfigRequired') }}
        isDisabled={isFormDisabled}
      />
    </YBModal>
  );
};
