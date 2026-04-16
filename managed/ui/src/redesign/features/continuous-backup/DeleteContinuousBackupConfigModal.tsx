import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, Typography, useTheme } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';

import { useDeleteContinuousBackup } from '@app/v2/api/continuous-backup/continuous-backup';
import { YBInput, YBModal, YBModalProps } from '@app/redesign/components';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { CONTINUOUS_BACKUP_QUERY_KEY } from '../../helpers/api';

interface DeleteContinuousBackupConfigModalProps {
  continuousBackupConfigUuid: string | undefined;
  modalProps: YBModalProps;
}

const MODAL_NAME = 'DeleteContinuousBackupConfigModal';
const TRANSLATION_KEY_PREFIX = 'continuousBackup.deleteContinuousBackupModal';
const DELETE_CONFIRMATION_TEXT = 'DELETE';
export const DeleteContinuousBackupConfigModal = ({
  continuousBackupConfigUuid,
  modalProps
}: DeleteContinuousBackupConfigModalProps) => {
  const [confirmationTextInput, setConfirmationTextInput] = useState<string>('');
  const queryClient = useQueryClient();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const deleteContinuousBackupConfig = useDeleteContinuousBackup();

  const resetModal = () => {
    setConfirmationTextInput('');
  };
  const onSubmit = () => {
    if (!continuousBackupConfigUuid) {
      return;
    }
    deleteContinuousBackupConfig.mutate(
      { bUUID: continuousBackupConfigUuid },
      {
        onSuccess: () => {
          toast.success(t('toast.deleteSuccess'));
          resetModal();
          queryClient.invalidateQueries(CONTINUOUS_BACKUP_QUERY_KEY);
          modalProps.onClose();
        },
        onError: (error: Error | AxiosError) =>
          handleServerError(error, { customErrorLabel: t('toast.deleteFailedLabel') })
      }
    );
  };

  const isFormDisabled =
    deleteContinuousBackupConfig.isLoading || confirmationTextInput !== DELETE_CONFIRMATION_TEXT;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={onSubmit}
      overrideHeight="fit-content"
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={deleteContinuousBackupConfig.isLoading}
      size="sm"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Typography variant="body2">{t('deleteConfirmation')}</Typography>
      <Box marginTop={3} display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
        <Typography variant="body2">{t('confirmationInstructions')}</Typography>
        <YBInput
          fullWidth
          placeholder={DELETE_CONFIRMATION_TEXT}
          value={confirmationTextInput}
          onChange={(event) => setConfirmationTextInput(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};
