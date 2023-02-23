import React, { useState } from 'react';
import axios, { AxiosError } from 'axios';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';

import { YBModal, YBModalProps } from '../../../redesign/components';
import { api, providerQueryKey } from '../../../redesign/helpers/api';

import { YBProvider } from './types';

interface DeleteModalProps extends YBModalProps {
  providerConfig: YBProvider | undefined;
  onClose: () => void;
  redirectURL?: string;
}

export const DeleteProviderConfigModal = ({
  providerConfig,
  onClose,
  redirectURL,
  ...modalProps
}: DeleteModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const queryClient = useQueryClient();

  const deleteProviderMutation = useMutation(
    (providerUUID: string | undefined) => {
      return api.deleteProvider(providerUUID);
    },
    {
      onSuccess: () => {
        queryClient.invalidateQueries(providerQueryKey.ALL);
        setIsSubmitting(false);
        onClose();
        if (redirectURL) {
          browserHistory.push(redirectURL);
        }
      },
      onError: (error: Error | AxiosError) => {
        if (axios.isAxiosError(error)) {
          toast.error(error.response?.data?.error?.message ?? error.message);
        } else {
          toast.error(error.message);
        }
      }
    }
  );
  const onSubmit = () => {
    setIsSubmitting(true);
    deleteProviderMutation.mutate(providerConfig?.uuid);
  };

  return (
    <YBModal
      title={`Delete Provider Config: ${providerConfig?.name}`}
      submitLabel="Delete"
      cancelLabel="Cancel"
      onSubmit={onSubmit}
      onClose={onClose}
      overrideHeight="fit-content"
      isSubmitting={isSubmitting}
      {...modalProps}
    >
      Are you sure you want to delete this provider configuration?
    </YBModal>
  );
};
