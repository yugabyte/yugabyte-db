import React, { useState } from 'react';
import { useMutation, useQueryClient } from 'react-query';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import { FormikActions } from 'formik';

import {
  deleteXclusterConfig,
  fetchTaskUntilItCompletes
} from '../../../actions/xClusterReplication';
import { YBModalForm } from '../../common/forms';
import { YBCheckBox } from '../../common/forms/fields';

import { XClusterConfig } from '../XClusterTypes';

import styles from './DeleteConfigModal.module.scss';

interface DeleteConfigModalProps {
  sourceUniverseUUID: string;
  targetUniverseUUID: string;
  xClusterConfig: XClusterConfig;
  onHide: () => void;
  visible: boolean;
}

export const DeleteConfigModal = ({
  sourceUniverseUUID,
  targetUniverseUUID,
  xClusterConfig,
  onHide,
  visible
}: DeleteConfigModalProps) => {
  const [forceDelete, setForceDelete] = useState(false);
  const queryClient = useQueryClient();

  const deleteConfig = useMutation(
    (xClusterConfigUUID: string) => {
      return deleteXclusterConfig(xClusterConfigUUID, forceDelete);
    },
    {
      onSuccess: (response) => {
        onHide();
        // Redirect to the universe's xCluster configurations page
        browserHistory.push(`/universes/${sourceUniverseUUID}/replication`);

        fetchTaskUntilItCompletes(
          response.data.taskUUID,
          (error: boolean) => {
            if (error) {
              toast.error(
                <span className={styles.alertMsg}>
                  <i className="fa fa-exclamation-circle" />
                  <span>{`Failed to delete xCluster configuration: ${xClusterConfig.name}`}</span>
                </span>
              );
              // Invalidate the cached data for current xCluster config.
              queryClient.invalidateQueries(['Xcluster', xClusterConfig.uuid]);
            }

            // This xCluster config will be removed from the sourceXClusterConfigs for the source universe and
            // from the targetXClusterConfigs for the target universe.
            // Invalidate queries for the participating universes.
            queryClient.invalidateQueries(['universe', sourceUniverseUUID], { exact: true });
            queryClient.invalidateQueries(['universe', targetUniverseUUID], { exact: true });
          },
          () => {
            // Invalidate the cached data for current xCluster config. The xCluster config status should change to
            // 'in progress' once the restart config task starts.
            queryClient.invalidateQueries(['Xcluster', xClusterConfig.uuid]);
          }
        );
      },
      onError: (err: any) => {
        toast.error(
          err.response.data.error instanceof String
            ? err.response.data.error
            : JSON.stringify(err.response.data.error)
        );
      }
    }
  );

  const toggleForceDelete = () => setForceDelete(!forceDelete);

  const handleFormSubmit = (_: any, actions: FormikActions<any>) => {
    deleteConfig.mutate(xClusterConfig.uuid, { onSettled: () => actions.setSubmitting(false) });
    onHide();
  };

  return (
    <YBModalForm
      visible={visible}
      formName={'DeleteConfigForm'}
      onHide={onHide}
      onFormSubmit={handleFormSubmit}
      submitLabel="Delete Replication"
      title={`Delete Replication: ${xClusterConfig.name}`}
      footerAccessory={
        <div className="force-delete">
          <YBCheckBox
            label="Ignore errors and force delete"
            className="footer-accessory"
            input={{ checked: forceDelete, onChange: toggleForceDelete }}
          />
        </div>
      }
    >
      <p>Are you sure you want to delete this replication?</p>
    </YBModalForm>
  );
};
