import { useState } from 'react';
import { AxiosError } from 'axios';
import { browserHistory } from 'react-router';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';

import { YBCheckbox, YBInput, YBModal, YBModalProps } from '../../../../redesign/components';
import { api, drConfigQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { isActionFrozen } from '../../../../redesign/helpers/utils';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { DrConfig } from '../dtos';
import { AllowedTasks } from '../../../../redesign/helpers/dtos';
import { UNIVERSE_TASKS } from '../../../../redesign/helpers/constants';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface DeleteConfigModalProps {
  drConfig: DrConfig;
  currentUniverseName: string;
  modalProps: YBModalProps;
  allowedTasks: AllowedTasks;
  redirectUrl?: string;
}

const useStyles = makeStyles((theme) => ({
  consequenceText: {
    marginTop: theme.spacing(2)
  }
}));

const MODAL_NAME = 'DeleteConfigModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.deleteModal';
export const DeleteConfigModal = ({
  drConfig,
  currentUniverseName,
  modalProps,
  allowedTasks,
  redirectUrl
}: DeleteConfigModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [isForceDelete, setIsForceDelete] = useState<boolean>(false);
  const [confirmationText, setConfirmationText] = useState<string>('');
  const classes = useStyles();
  const queryClient = useQueryClient();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const deleteDrConfigMutation = useMutation(
    (drConfig: DrConfig) => api.deleteDrConfig(drConfig.uuid, isForceDelete),
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

        toast.success(
          <Typography variant="body2" component="span">
            {t('success.requestSuccess')}
          </Typography>
        );
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

  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  const isDeleteActionFrozen = isActionFrozen(allowedTasks, UNIVERSE_TASKS.DELETE_DR);
  const isFormDisabled =
    isSubmitting || confirmationText !== currentUniverseName || isDeleteActionFrozen;

  return (
    <YBModal
      title={modalTitle}
      submitLabel={t('submitButton')}
      cancelLabel={cancelLabel}
      onSubmit={onSubmit}
      overrideHeight="fit-content"
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={isSubmitting}
      size="sm"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      footerAccessory={
        <YBCheckbox
          label="Force Delete"
          checked={isForceDelete}
          onChange={(e) => {
            setIsForceDelete(e.target.checked);
          }}
          size="medium"
        />
      }
      {...modalProps}
    >
      <Typography variant="body2">
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.deleteConfirmation`}
          values={{ currentUniverseName: currentUniverseName }}
          components={{
            bold: <b />
          }}
        />
      </Typography>
      <Typography variant="body2" className={classes.consequenceText}>
        {t('deleteConsequence')}
      </Typography>
      <Box marginTop={3} display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
        <Typography variant="body2">{t('confirmationInstructions')}</Typography>
        <YBInput
          fullWidth
          placeholder={currentUniverseName}
          value={confirmationText}
          onChange={(event) => setConfirmationText(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};
