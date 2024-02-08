import { useState } from 'react';
import { AxiosError } from 'axios';
import { browserHistory } from 'react-router';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';

import { YBInput, YBModal, YBModalProps } from '../../../../redesign/components';
import { api, drConfigQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../utils/errorHandlingUtils';

import { DrConfig } from '../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface DeleteConfigModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;

  redirectUrl?: string;
}

const useStyles = makeStyles((theme) => ({
  consequenceText: {
    marginTop: theme.spacing(2)
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.deleteModal';
export const DeleteConfigModal = ({
  drConfig,
  modalProps,
  redirectUrl
}: DeleteConfigModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [confirmationText, setConfirmationText] = useState<string>('');
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const deleteDrConfigMutation = useMutation(
    (drConfig: DrConfig) => api.deleteDrConfig(drConfig.uuid),
    {
      onSuccess: (response, drConfig) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));

          // Refetch the source & target universes to remove references to the deleted DR config.
          queryClient.invalidateQueries(universeQueryKey.detail(drConfig.primaryUniverseUuid), {
            exact: true
          });
          queryClient.invalidateQueries(universeQueryKey.detail(drConfig.drReplicaUniverseUuid), {
            exact: true
          });
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

  const resetModal = () => {
    setIsSubmitting(false);
    setConfirmationText('');
  };
  const onSubmit = () => {
    setIsSubmitting(true);
    deleteDrConfigMutation.mutate(drConfig, { onSettled: () => resetModal() });
  };

  const isFormDisabled = isSubmitting || confirmationText !== drConfig.name;
  return (
    <YBModal
      title={t('title', { drConfigName: drConfig.name })}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={onSubmit}
      overrideHeight="fit-content"
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={isSubmitting}
      size="sm"
      {...modalProps}
    >
      <Typography variant="body2">{t('deleteConfirmation')}</Typography>
      <Typography variant="body2" className={classes.consequenceText}>
        {t('deleteConsequence')}
      </Typography>
      <Box marginTop={3}>
        <Typography variant="body2">{t('confirmationInstructions')}</Typography>
        <YBInput
          fullWidth
          placeholder={drConfig.name}
          value={confirmationText}
          onChange={(event) => setConfirmationText(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};
