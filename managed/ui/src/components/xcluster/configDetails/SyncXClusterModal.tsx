import { useState } from 'react';
import { useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { useSyncXClusterConfigWithDB } from '../../../redesign/helpers/hooks';
import { xClusterQueryKey } from '../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../actions/xClusterReplication';
import { YBModal, YBModalProps } from '../../../redesign/components';
import { YBErrorIndicator } from '../../common/indicators';

import { XClusterConfig } from '../dtos';

import toastStyles from '../../../redesign/styles/toastStyles.module.scss';

interface SyncXClusterConfigModalProps {
  xClusterConfig: XClusterConfig;
  modalProps: YBModalProps;
}

const MODAL_NAME = 'SyncDbModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.syncDbModal';

export const SyncXClusterConfigModal = ({
  xClusterConfig,
  modalProps
}: SyncXClusterConfigModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const syncXClusterConfig = useSyncXClusterConfigWithDB(queryClient, {
    onSuccess: (data, variables) => {
      const invalidateQueries = () => {
        queryClient.invalidateQueries(xClusterQueryKey.detail(variables.xClusterConfigUuid));
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
          toast.success(t('success.taskSuccess'));
        }
        invalidateQueries();
      };

      modalProps.onClose();
      fetchTaskUntilItCompletes(data.taskUUID, handleTaskCompletion, invalidateQueries);
    }
  });

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
    syncXClusterConfig.mutate(
      {
        xClusterConfigUuid: xClusterConfig.uuid,
        replicationGroupName: xClusterConfig.replicationGroupName,
        targetUniverseUuid: targetUniverseUuid
      },
      { onSettled: () => resetModal() }
    );
  };

  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      cancelLabel={cancelLabel}
      onSubmit={onSubmit}
      buttonProps={{ primary: { disabled: isSubmitting } }}
      isSubmitting={isSubmitting}
      size="sm"
      {...modalProps}
    >
      {t('syncConfirmation')}
    </YBModal>
  );
};
