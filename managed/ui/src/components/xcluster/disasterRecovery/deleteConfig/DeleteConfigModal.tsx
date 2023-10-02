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

import { DrConfig } from '../types';

interface DeleteConfigModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;

  redirectUrl?: string;
}

const useStyles = makeStyles((theme) => ({
  toastContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    '& a': {
      textDecoration: 'underline',
      color: '#fff'
    }
  },
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
          queryClient.invalidateQueries(
            universeQueryKey.detail(drConfig.xClusterConfig.sourceUniverseUUID)
          );
          queryClient.invalidateQueries(
            universeQueryKey.detail(drConfig.xClusterConfig.targetUniverseUUID)
          );
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={classes.toastContainer}>
                <i className="fa fa-exclamation-circle" />
                <span>{t('error.taskFailure')}</span>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(t('success.taskSuccess'));
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
        handleServerError(error, { customErrorLabel: t('error.requestFailure') })
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
          onChange={(event) => setConfirmationText(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};
