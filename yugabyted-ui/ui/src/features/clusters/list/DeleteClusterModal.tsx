import React, { FC, useState } from 'react';
import { useTranslation, Trans } from 'react-i18next';
// import { useRouteMatch } from 'react-router-dom';
import { Box } from '@material-ui/core';
import { YBModal, YBInput, AlertVariant, YBAlert } from '@app/components';
import { useDeleteClusterMutation, ClusterData } from '@app/api/src';
// import { browserStorage } from '@app/helpers';

export interface DeleteClusterProps {
  open: boolean;
  onClose: () => void;
  onSuccess: () => void;
  onFailure?: () => void;
  cluster: ClusterData;
}

export const DeleteClusterModal: FC<DeleteClusterProps> = ({ open, onClose, cluster }) => {
  const { t } = useTranslation();
  // const { params } = useRouteMatch<App.RouteParams>();
  const [deletingClusterNameInput, setDeletingClusterNameInput] = useState('');
  const { mutate: deleteCluster, isLoading } = useDeleteClusterMutation();

  const handleDeleteCluster = () => {
    deleteCluster();
  };

  const handleClose = () => {
    onClose();
    setDeletingClusterNameInput('');
  };

  const handleClusterNameInput = (ev: React.ChangeEvent<HTMLInputElement>) => {
    setDeletingClusterNameInput(ev.target.value);
  };

  const deleteModalCopy = (
    <Trans i18nKey={'clusters.deleteModalCopy'}>
      {'Are you sure you want to terminate '}
      <strong>{{ cluster: cluster.spec.name }}</strong>
      {'? Please enter the cluster name to continue.'}
    </Trans>
  );

  const disableDelete = deletingClusterNameInput !== cluster.spec.name || isLoading;

  return (
    <YBModal
      open={open}
      size="sm"
      title={t('clusters.terminateCluster')}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.delete')}
      onClose={handleClose}
      titleSeparator
      onSubmit={handleDeleteCluster}
      buttonProps={{
        primary: { disabled: disableDelete }
      }}
    >
      <div>
        <YBAlert open variant={AlertVariant.Error} text={t('clusters.deletionErrorAlert')} />
        <Box mt={4} mb={2}>
          {deleteModalCopy}
        </Box>
        <YBInput
          label={t('clusters.deleteModalLabel')}
          fullWidth
          placeholder={cluster.spec.name}
          value={deletingClusterNameInput}
          onChange={handleClusterNameInput}
        />
      </div>
    </YBModal>
  );
};
