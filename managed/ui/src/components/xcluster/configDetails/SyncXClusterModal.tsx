import React, { FC } from 'react';
import { useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { makeStyles } from '@material-ui/core';

import { YBConfirmModal } from '../../modals';
import { XClusterModalName } from '../constants';
import { XClusterConfig } from '../XClusterTypes';
import { useSyncXClusterConfigWithDB } from '../../../redesign/helpers/hooks';
import { xClusterQueryKey } from '../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../actions/xClusterReplication';

interface SyncXClusterConfigModalProps {
  xClusterConfig: XClusterConfig;
  onHide: () => void;
}

const useStyles = makeStyles((theme) => ({
  toastContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    '& a': {
      textDecoration: 'underline',
      color: '#fff'
    }
  }
}));

const SyncXClusterConfigModal: FC<SyncXClusterConfigModalProps> = ({ xClusterConfig, onHide }) => {
  const { visibleModal } = useSelector((state: any) => state.modal);
  const queryClient = useQueryClient();
  const classes = useStyles();

  const syncXClusterConfig = useSyncXClusterConfigWithDB(queryClient, {
    onSuccess: (data, variables) => {
      fetchTaskUntilItCompletes(data.taskUUID, (error: boolean) => {
        if (error) {
          toast.error(
            <span className={classes.toastContainer}>
              <i className="fa fa-exclamation-circle" />
              <span>xCluster Config DB sync failed.</span>
              <a href={`/tasks/${data.taskUUID}`} rel="noopener noreferrer" target="_blank">
                View Details
              </a>
            </span>
          );
        } else {
          toast.success('Reconciled xCluster config with DB.');
        }
        queryClient.invalidateQueries(xClusterQueryKey.detail(variables.xClusterConfigUUID));
      });
    }
  });

  return (
    <YBConfirmModal
      name="delete-replication-modal"
      title={`Confirm xCluster config reconciliation: ${xClusterConfig.name}`}
      currentModal={XClusterModalName.SYNC_XCLUSTER_CONFIG_WITH_DB}
      visibleModal={visibleModal}
      confirmLabel="Confirm"
      cancelLabel="Cancel"
      onConfirm={() =>
        xClusterConfig.targetUniverseUUID
          ? syncXClusterConfig.mutate({
              xClusterConfigUUID: xClusterConfig.uuid,
              replicationGroupName: xClusterConfig.replicationGroupName,
              universeUUID: xClusterConfig.targetUniverseUUID
            })
          : toast.error(
              `The target universe is deleted for the following xCluster config: ${xClusterConfig.name}`
            )
      }
      hideConfirmModal={() => {
        onHide();
      }}
    >
      {`Are you sure you want to set this xcluster configuration to the current xcluster replication configuration on the database?`}
    </YBConfirmModal>
  );
};

export default SyncXClusterConfigModal;
