import { useState } from 'react';
import { useMutation, useQueryClient } from 'react-query';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import { FormikActions } from 'formik';
import { AxiosError } from 'axios';

import {
  deleteXclusterConfig,
  fetchTaskUntilItCompletes
} from '../../../actions/xClusterReplication';
import { YBModalForm } from '../../common/forms';
import { YBCheckBox } from '../../common/forms/fields';
import { AllowedTasks } from '../../../redesign/helpers/dtos';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import { UNIVERSE_TASKS } from '../../../redesign/helpers/constants';
import { universeQueryKey, xClusterQueryKey } from '../../../redesign/helpers/api';

import styles from './DeleteConfigModal.module.scss';

interface DeleteConfigModalProps {
  onHide: () => void;
  visible: boolean;
  xClusterConfigUUID: string;
  allowedTasks: AllowedTasks;
  redirectUrl?: string;
  sourceUniverseUUID?: string;
  targetUniverseUUID?: string;
  xClusterConfigName?: string;
}

export const DeleteConfigModal = ({
  onHide,
  redirectUrl,
  sourceUniverseUUID,
  targetUniverseUUID,
  visible,
  allowedTasks,
  xClusterConfigName,
  xClusterConfigUUID
}: DeleteConfigModalProps) => {
  const [forceDelete, setForceDelete] = useState(false);
  const queryClient = useQueryClient();
  const xClusterConfigLabel = xClusterConfigName ?? xClusterConfigUUID;
  const deleteConfig = useMutation(
    (xClusterConfigUUID: string) => {
      return deleteXclusterConfig(xClusterConfigUUID, forceDelete);
    },
    {
      onSuccess: (response) => {
        onHide();
        if (redirectUrl) {
          browserHistory.push(redirectUrl);
        }

        fetchTaskUntilItCompletes(
          response.data.taskUUID,
          (error: boolean) => {
            if (error) {
              toast.error(
                <span className={styles.alertMsg}>
                  <i className="fa fa-exclamation-circle" />
                  <span>{`Failed to delete xCluster configuration: ${xClusterConfigLabel}`}</span>
                  <a
                    href={`/tasks/${response.data.taskUUID}`}
                    rel="noopener noreferrer"
                    target="_blank"
                  >
                    View Details
                  </a>
                </span>
              );
              // Invalidate the cached data for current xCluster config.
              queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUUID));
            }

            // This xCluster config will be removed from the sourceXClusterConfigs for the source universe and
            // from the targetXClusterConfigs for the target universe.
            // Invalidate queries for the participating universes.
            if (sourceUniverseUUID) {
              queryClient.invalidateQueries(universeQueryKey.detail(sourceUniverseUUID), {
                exact: true
              });
            }
            if (targetUniverseUUID) {
              queryClient.invalidateQueries(universeQueryKey.detail(targetUniverseUUID), {
                exact: true
              });
            }
          },
          () => {
            // Invalidate the cached data for current xCluster config. The xCluster config status should change to
            // 'in progress' once the restart config task starts.
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUUID));
          }
        );
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: 'Delete xCluster config request failed' })
    }
  );

  const toggleForceDelete = () => setForceDelete(!forceDelete);

  const handleFormSubmit = (_: any, actions: FormikActions<any>) => {
    deleteConfig.mutate(xClusterConfigUUID, { onSettled: () => actions.setSubmitting(false) });
    onHide();
  };

  const isDeleteActionFrozen = isActionFrozen(allowedTasks, UNIVERSE_TASKS.DELETE_REPLICATION);

  return (
    <YBModalForm
      visible={visible}
      formName={'DeleteConfigForm'}
      onHide={onHide}
      onFormSubmit={handleFormSubmit}
      isButtonDisabled={isDeleteActionFrozen}
      submitLabel="Delete Replication"
      title={`Delete Replication: ${xClusterConfigLabel}`}
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
