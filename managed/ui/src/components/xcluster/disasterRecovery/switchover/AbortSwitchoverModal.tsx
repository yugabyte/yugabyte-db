import { useState } from 'react';
import { AxiosError } from 'axios';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';

import { YBModal, YBModalProps } from '../../../../redesign/components';
import { api, drConfigQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { handleServerError } from '../../../../utils/errorHandlingUtils';

import { DrConfig } from '../dtos';

interface AbortSwitchoverModalProps {
  taskUuid: string;
  drConfig: DrConfig;
  modalProps: YBModalProps;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.switchover.abortModal';

export const AbortSwitchoverModal = ({
  taskUuid,
  drConfig,
  modalProps
}: AbortSwitchoverModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();

  const abortSwitchoverMutation = useMutation((taskUuid: string) => api.abortTask(taskUuid), {
    onSuccess: () => {
      queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
      queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));

      queryClient.invalidateQueries(
        universeQueryKey.detail(drConfig.xClusterConfig.sourceUniverseUUID),
        { exact: true }
      );
      queryClient.invalidateQueries(
        universeQueryKey.detail(drConfig.xClusterConfig.targetUniverseUUID),
        { exact: true }
      );
      toast.success(t('success.requestSuccess'));
    },
    onError: (error: Error | AxiosError) =>
      handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
  });

  const onSubmit = () => {
    setIsSubmitting(true);
    abortSwitchoverMutation.mutate(taskUuid, { onSettled: () => setIsSubmitting(false) });
  };

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      size="sm"
      onSubmit={onSubmit}
      buttonProps={{ primary: { disabled: isSubmitting } }}
      isSubmitting={isSubmitting}
      {...modalProps}
    >
      {t('description')}
    </YBModal>
  );
};
