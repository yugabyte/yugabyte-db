import { useState } from 'react';
import { browserHistory } from 'react-router';
import { makeStyles } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useQueryClient } from 'react-query';
import { useDispatch } from 'react-redux';

import { YBModal, YBModalProps } from '../../../redesign/components';
import { fetchTaskUntilItCompletes } from '../../../actions/xClusterReplication';
import { providerQueryKey } from '../../../redesign/helpers/api';
import { useDeleteProvider } from '../../../redesign/helpers/hooks';
import { fetchCloudMetadata } from '../../../actions/cloud';

import { YBProvider } from './types';

interface DeleteModalProps extends YBModalProps {
  providerConfig: YBProvider | undefined;
  onClose: () => void;
  redirectURL?: string;
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

export const DeleteProviderConfigModal = ({
  providerConfig,
  onClose,
  redirectURL,
  ...modalProps
}: DeleteModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const classes = useStyles();
  const queryClient = useQueryClient();
  const dispatch = useDispatch();

  const deleteProviderMutation = useDeleteProvider(queryClient, {
    onSuccess: (response, variables) => {
      queryClient.invalidateQueries(providerQueryKey.ALL, { exact: true });
      queryClient.invalidateQueries(providerQueryKey.detail(variables.providerUUID), {
        exact: true
      });

      fetchTaskUntilItCompletes(response.taskUUID, (error: boolean) => {
        if (error) {
          toast.error(
            <span className={classes.toastContainer}>
              <i className="fa fa-exclamation-circle" />
              <span>Provider deletion failed.</span>
              <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                View Details
              </a>
            </span>
          );
        } else {
          toast.success('Provider delete succeeded.');
        }
        queryClient.invalidateQueries(providerQueryKey.ALL);
        dispatch(fetchCloudMetadata());
      });

      setIsSubmitting(false);
      onClose();
      if (redirectURL) {
        browserHistory.push(redirectURL);
      }
    }
  });
  const onSubmit = () => {
    if (!providerConfig?.uuid) {
      toast.error('Error: Selected provider has undefined or empty provider UUID.');
      onClose();
    } else {
      setIsSubmitting(true);
      deleteProviderMutation.mutate({ providerUUID: providerConfig.uuid });
    }
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
