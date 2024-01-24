import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { Box, Typography } from '@material-ui/core';
import { YBModal } from '../../components';
import { useMutation } from 'react-query';
import { NodeAgentAPI } from './api';
import { ToastNotificationDuration } from '../../helpers/constants';

interface DeleteNodeAgentProps {
  openNodeAgentDialog: boolean;
  providerName: string;
  nodeAgentUUID: string;
  onClose: () => void;
  onNodeAgentDeleted?: () => void;
}

export const DeleteNodeAgent: FC<DeleteNodeAgentProps> = ({
  openNodeAgentDialog,
  providerName,
  nodeAgentUUID,
  onClose,
  onNodeAgentDeleted
}) => {
  const { t } = useTranslation();

  const deleteNodeAgent = useMutation(
    (nodeAgentUUID: string) => NodeAgentAPI.deleteNodeAgent(nodeAgentUUID),
    {
      onSuccess: (data) => {
        toast.success('Successfully deleted Node Agent', {
          autoClose: ToastNotificationDuration.SHORT
        });
        onNodeAgentDeleted?.();
      },
      onError: () => {
        toast.error('Could not delete Node Agent, try again', {
          autoClose: ToastNotificationDuration.SHORT
        });
      }
    }
  );

  const onSubmit = async () => {
    deleteNodeAgent.mutateAsync(nodeAgentUUID);
    onClose();
  };

  return (
    <YBModal
      title={t('nodeAgent.deleteNodeAgent')}
      open={openNodeAgentDialog}
      onClose={onClose}
      size="sm"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.proceed')}
      onSubmit={onSubmit}
      overrideHeight="auto"
      titleSeparator
      submitTestId="submit-node-agent-deletion"
      cancelTestId="close-node-agent-deletion"
    >
      <Box
        display="flex"
        width="100%"
        flexDirection="column"
        data-testid="node-agent-deletion-modal"
      >
        <Typography variant="body2">
          {t('nodeAgent.confirmDeletionMessage', {
            provider_name: providerName
          })}
        </Typography>
      </Box>
    </YBModal>
  );
};
