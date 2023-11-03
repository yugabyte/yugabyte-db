import { FC, useRef } from 'react';
import { Field, FormikProps } from 'formik';
import moment from 'moment';
import * as Yup from 'yup';
import { useMutation, useQuery } from 'react-query';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import { Alert } from 'react-bootstrap';
import { YBModalForm } from '../../common/forms';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';
import { YBLoading } from '../../common/indicators';
import { YBCheckBox, YBFormSelect } from '../../common/forms/fields';
import './PromoteInstanceModal.scss';

interface PromoteInstanceModalProps {
  visible: boolean;
  onClose(): void;
  configId: string;
  instanceId: string;
}

interface FormValues {
  backupFile: { value: string; label: string } | null;
  confirmed: boolean;
}

const INITIAL_VALUES: FormValues = {
  backupFile: null,
  confirmed: false
};

const validationSchema = Yup.object().shape({
  backupFile: Yup.object().nullable().required('Backup file is required'),
  confirmed: Yup.boolean().oneOf([true])
});

const mapFileName = (value: string): FormValues['backupFile'] => {
  // backup_21-02-20-00-40.tgz --> 21-02-20-00-40
  const timestamp = value.replace('backup_', '').replace('.tgz', '');
  const label = moment.utc(timestamp, 'YY-MM-DD-HH:mm').local().format('LLL');

  return { value, label };
};

export const PromoteInstanceModal: FC<PromoteInstanceModalProps> = ({
  visible,
  onClose,
  configId,
  instanceId
}) => {
  const formik = useRef({} as FormikProps<FormValues>);
  const { isLoading, data } = useQuery(
    [QUERY_KEY.getHABackups, configId],
    () => api.getHABackups(configId),
    {
      enabled: visible,
      onSuccess: (data) => {
        // pre-select first backup file from the list
        if (Array.isArray(data) && data.length) {
          formik.current.setFieldValue('backupFile', mapFileName(data[0]));
        }
      }
    }
  );
  const { mutateAsync: promoteInstance } = useMutation<void, unknown, string>((backupFile) =>
    api.promoteHAInstance(configId, instanceId, backupFile)
  );

  const backupsList = (data ?? []).map(mapFileName);

  const closeModal = () => {
    if (!formik.current.isSubmitting) onClose();
  };

  const submitForm = async (values: FormValues) => {
    try {
      // values.backupFile never be null here due to form validation
      await promoteInstance(values.backupFile!.value);
      browserHistory.push('/login');
    } catch (error) {
      toast.error('Failed to promote platform instance');
      formik.current.setSubmitting(false);
      onClose();
    }
  };

  if (visible) {
    return (
      <YBModalForm
        visible
        initialValues={INITIAL_VALUES}
        validationSchema={validationSchema}
        submitLabel="Continue"
        cancelLabel="Cancel"
        showCancelButton
        title="Make Active"
        onHide={closeModal}
        onFormSubmit={submitForm}
        footerAccessory={
          <Field name="confirmed" component={YBCheckBox} label="Confirm promotion" />
        }
        render={(formikProps: FormikProps<FormValues>) => {
          // workaround for outdated version of Formik to access form methods outside of <Formik>
          formik.current = formikProps;

          return (
            <div data-testid="ha-make-active-modal">
              {isLoading ? (
                <YBLoading />
              ) : (
                <div className="ha-promote-instance-modal">
                  <Alert bsStyle="warning">
                    Note: promotion will replace all existing data on this platform instance with
                    the data from the selected backup. After promotion succeeds you will need to
                    re-sign in with the credentials of the previously active platform instance.
                  </Alert>
                  <Field
                    name="backupFile"
                    component={YBFormSelect}
                    options={backupsList}
                    label="Select the backup to restore from"
                    isSearchable
                  />
                </div>
              )}
            </div>
          );
        }}
      />
    );
  } else {
    return null;
  }
};
