import { useState } from 'react';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { AxiosError } from 'axios';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import { Typography } from '@material-ui/core';

import { api, drConfigQueryKey, xClusterQueryKey } from '../../../redesign/helpers/api';
import {
  fetchTaskUntilItCompletes,
  syncXClusterConfigWithDB
} from '../../../actions/xClusterReplication';
import { YBModal, YBModalProps } from '../../../redesign/components';
import { YBErrorIndicator } from '../../common/indicators';
import { XClusterConfig } from '../dtos';
import { AllowedTasks } from '../../../redesign/helpers/dtos';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { UNIVERSE_TASKS } from '../../../redesign/helpers/constants';

import toastStyles from '../../../redesign/styles/toastStyles.module.scss';

interface CommonSyncXClusterConfigModalProps {
  xClusterConfig: XClusterConfig;
  modalProps: YBModalProps;
  allowedTasks: AllowedTasks;
}

type SyncXClusterConfigModalProps =
  | (CommonSyncXClusterConfigModalProps & {
      isDrInterface: true;
      drConfigUuid: string;
    })
  | (CommonSyncXClusterConfigModalProps & { isDrInterface: false });

const MODAL_NAME = 'SyncDbModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.syncDbModal';

export const SyncXClusterConfigModal = (props: SyncXClusterConfigModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { xClusterConfig, modalProps } = props;

  const syncXClusterConfigMutation = useMutation(
    ({ targetUniverseUuid }: { targetUniverseUuid: string }) => {
      return props.isDrInterface
        ? api.syncDrConfig(props.drConfigUuid)
        : syncXClusterConfigWithDB(
            xClusterConfig.uuid,
            xClusterConfig.replicationGroupName,
            targetUniverseUuid
          );
    },
    {
      onSuccess: (data) => {
        const invalidateQueries = () => {
          if (props.isDrInterface) {
            queryClient.invalidateQueries(drConfigQueryKey.detail(props.drConfigUuid));
          }
          queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <span>{t('error.taskFailure')}</span>
                <a href={`/tasks/${data.taskUUID}`} rel="noopener noreferrer" target="_blank">
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
        fetchTaskUntilItCompletes(data.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  const modalTitle = t('title');
  const submitLabel = t('submitButton');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (!xClusterConfig.targetUniverseUUID) {
    return (
      <YBModal
        title={modalTitle}
        submitLabel={submitLabel}
        cancelLabel={cancelLabel}
        buttonProps={{
          primary: { disabled: true }
        }}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        {...modalProps}
      >
        <YBErrorIndicator customErrorMessage={t('error.undefinedTargetUniverseUuid')} />
      </YBModal>
    );
  }

  const targetUniverseUuid = xClusterConfig.targetUniverseUUID;
  const resetModal = () => {
    setIsSubmitting(false);
  };
  const onSubmit = () => {
    setIsSubmitting(true);
    syncXClusterConfigMutation.mutate(
      {
        targetUniverseUuid: targetUniverseUuid
      },
      { onSettled: () => resetModal() }
    );
  };

  const isFormDisabled = props.isDrInterface
    ? isActionFrozen(props.allowedTasks, UNIVERSE_TASKS.SYNC_DR)
    : isActionFrozen(props.allowedTasks, UNIVERSE_TASKS.SYNC_REPLICATION);
  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      cancelLabel={cancelLabel}
      onSubmit={onSubmit}
      buttonProps={{ primary: { disabled: isSubmitting || isFormDisabled } }}
      isSubmitting={isSubmitting}
      size="sm"
      {...modalProps}
    >
      {props.isDrInterface ? t('syncDrConfirmation') : t('syncXClusterConfirmation')}
    </YBModal>
  );
};
