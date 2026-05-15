import { FC } from 'react';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { mui, yba, YBInputField } from '@yugabyte-ui-library/core';
import { getGetUniverseQueryKey, useDeleteCluster } from '@app/v2/api/universe/universe';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import { useDispatch } from 'react-redux';
import { showTaskInDrawer } from '@app/actions/tasks';

const { Box, Typography } = mui;
const { YBModal } = yba;

type DeleteReadReplicaFormValues = {
  universeName: string | null;
};

const DEFAULT_VALUES: DeleteReadReplicaFormValues = {
  universeName: null
};

export interface DeleteReadReplicaModalProps {
  open: boolean;
  onClose: () => void;
  universeUuid: string;
  clusterUuid: string;
  universeDisplayName: string;
}

export const DeleteReadReplicaModal: FC<DeleteReadReplicaModalProps> = ({
  open,
  onClose,
  universeUuid,
  clusterUuid,
  universeDisplayName
}) => {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const deleteClusterMutation = useDeleteCluster();
  const dispatch = useDispatch();
  const {
    control,
    formState: { isValid },
    handleSubmit,
    reset
  } = useForm<DeleteReadReplicaFormValues>({
    defaultValues: DEFAULT_VALUES,
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const handleClose = () => {
    reset(DEFAULT_VALUES);
    onClose();
  };

  const handleFormSubmit = handleSubmit(async () => {
    const cUUID = localStorage.getItem('customerId');
    if (!cUUID) return;

    try {
      await deleteClusterMutation.mutateAsync({
        uniUUID: universeUuid,
        clsUUID: clusterUuid,
        params: { isForceDelete: false },
        cUUID
      },
      {
        onSuccess: (response) => {
          dispatch(showTaskInDrawer(response.task_uuid));
          queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid, cUUID));
          toast.success(t('universeForm.deleteClusterModal.deletionStarted'));
          handleClose();
        }
      }
    );
    } catch (e) {
      toast.error(createErrorMessage(e));
    }
  });

  if (!open) return null;

  return (
    <YBModal
      open={open}
      overrideHeight={300}
      titleSeparator
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.yes')}
      title={t('universeForm.deleteClusterModal.modalTitle', {
        universeName: universeDisplayName
      })}
      onClose={handleClose}
      onSubmit={handleFormSubmit}
      buttonProps={{
        primary: {
          disabled: !isValid || deleteClusterMutation.isLoading,
          dataTestId: 'submit-delete-read-replica',
          loading: deleteClusterMutation.isLoading
        }
      }}
      dialogContentProps={{ sx: { paddingTop: '20px' } }}
      submitTestId="submit-delete-cluster"
      cancelTestId="close-delete-cluster"
    >
      <Box display="flex" width="100%" flexDirection="column" data-testid="delete-read-replica-modal">
        <Typography variant="body2">
          {t('universeForm.deleteClusterModal.deleteRRMessage')}
        </Typography>
        <Box mt={2.5}>
          <Typography variant="body1">
            {t('universeForm.deleteClusterModal.enterUniverseName')}
          </Typography>
          <Box mt={1}>
            <YBInputField
              fullWidth
              dataTestId="delete-read-replica-universe-name-input"
              control={control}
              placeholder={universeDisplayName}
              name="universeName"
              inputProps={{
                autoFocus: true,
                'data-testid': 'validate-universename'
              }}
              rules={{
                validate: {
                  universeNameMatch: (value) => value === universeDisplayName
                }
              }}
            />
          </Box>
        </Box>
      </Box>
    </YBModal>
  );
};
