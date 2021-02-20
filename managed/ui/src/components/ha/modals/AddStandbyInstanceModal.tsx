import React, { FC, useRef } from 'react';
import { Field, FormikProps } from 'formik';
import * as Yup from 'yup';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { YBModalForm } from '../../common/forms';
import { YBFormInput } from '../../common/forms/fields';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';

interface AddStandbyInstanceModalProps {
  visible: boolean;
  onClose(): void;
  configId: string;
}

const INITIAL_VALUES = {
  instanceAddress: ''
};
type FormValues = typeof INITIAL_VALUES;

const validationSchema = Yup.object().shape({
  instanceAddress: Yup.string()
    .required('Required field')
    .matches(/^(http|https):\/\/.+/i, 'Should be a valid URL')
});

export const AddStandbyInstanceModal: FC<AddStandbyInstanceModalProps> = ({
  visible,
  onClose,
  configId
}) => {
  const formik = useRef({} as FormikProps<{}>);
  const queryClient = useQueryClient();
  const { mutateAsync: createHAInstance } = useMutation((instanceAddress: string) =>
    api.createHAInstance(configId, instanceAddress, false, false)
  );

  const closeModal = () => {
    if (!formik.current.isSubmitting) onClose();
  };

  const submitForm = async (values: FormValues) => {
    try {
      await createHAInstance(values.instanceAddress);
      queryClient.invalidateQueries(QUERY_KEY.getHAConfig);
    } catch (error) {
      toast.error('Failed to add standby platform instance');
    } finally {
      formik.current.setSubmitting(false);
      closeModal();
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
        title="Add Standby Instance"
        onHide={closeModal}
        onFormSubmit={submitForm}
        render={(formikProps: FormikProps<{}>) => {
          // workaround for outdated version of Formik to access form methods outside of <Formik>
          formik.current = formikProps;

          return (
            <Field
              label="Enter IP Address / Hostname for standby platform instance"
              name="instanceAddress"
              type="text"
              component={YBFormInput}
            />
          );
        }}
      />
    );
  } else {
    return null;
  }
};
