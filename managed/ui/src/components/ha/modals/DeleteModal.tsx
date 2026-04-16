import { FC, useRef } from 'react';
import { FormikProps } from 'formik';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { YBModalForm } from '../../common/forms';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';

interface DeleteModalProps {
  visible: boolean;
  onClose(): void;
  configId: string;
  instanceId?: string; // if defined - will delete platform instance
  isStandby?: boolean;
}

export const DeleteModal: FC<DeleteModalProps> = ({
  visible,
  onClose,
  configId,
  instanceId,
  isStandby
}) => {
  const formik = useRef({} as FormikProps<{}>);
  const queryClient = useQueryClient();
  const { mutateAsync: deleteConfig } = useMutation(() => api.deleteHAConfig(configId));
  const { mutateAsync: deleteInstance } = useMutation(() =>
    api.deleteHAInstance(configId, instanceId!)
  );

  const closeModal = () => {
    if (!formik.current.isSubmitting) onClose();
  };

  const submitForm = async () => {
    try {
      if (instanceId) {
        await deleteInstance();
      } else {
        await deleteConfig();
      }
      queryClient.resetQueries(QUERY_KEY.getHAConfig);
      queryClient.resetQueries(QUERY_KEY.getHAReplicationSchedule);
    } catch (error) {
      toast.error(`Failed to delete ${instanceId ? 'instance' : 'replication configuration'}`);
    } finally {
      formik.current.setSubmitting(false);
      closeModal();
    }
  };

  if (visible) {
    return (
      <YBModalForm
        visible
        submitLabel="Continue"
        cancelLabel="Cancel"
        showCancelButton
        title="Confirm Delete"
        onHide={closeModal}
        onFormSubmit={submitForm}
        render={(formikProps: FormikProps<{}>) => {
          // workaround for outdated version of Formik to access form methods outside of <Formik>
          formik.current = formikProps;
          return (
            <div data-testid="ha-delete-confirmation-modal">
              <p />
              Are you sure you want to <strong>delete</strong> this{' '}
              {instanceId ? ' platform instance' : ' replication configuration'}?
              <br />
              {isStandby &&
                "You'd need to remove this standby instance from the active instance configuration as well."}
              <p />
            </div>
          );
        }}
      />
    );
  } else {
    return null;
  }
};
