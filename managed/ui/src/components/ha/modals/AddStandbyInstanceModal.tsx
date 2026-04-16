import { FC, useEffect, useRef } from 'react';
import { Field, FormikErrors, FormikProps } from 'formik';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { YBModalForm } from '../../common/forms';
import { YBFormInput } from '../../common/forms/fields';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';

interface AddStandbyInstanceModalProps {
  visible: boolean;
  onClose(): void;
  configId: string;
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string) => void;
  runtimeConfigs: any;
}

interface AddStandbyInstanceFormValues {
  instanceAddress: string;
}

interface AddStandbyInstanceFormErrors {
  instanceAddress: string;
  peerCerts: string;
}

const INITIAL_VALUES: Partial<AddStandbyInstanceFormValues> = {
  instanceAddress: ''
};

const INSTANCE_VALID_ADDRESS_PATTERN = /^(http|https):\/\/.+/i;

export const AddStandbyInstanceModal: FC<AddStandbyInstanceModalProps> = ({
  visible,
  onClose,
  configId,
  fetchRuntimeConfigs
}) => {
  const formik = useRef({} as FormikProps<AddStandbyInstanceFormValues>);
  const queryClient = useQueryClient();
  const { mutateAsync: createHAInstance } = useMutation((instanceAddress: string) =>
    api.createHAInstance(configId, instanceAddress, false, false)
  );

  useEffect(() => {
    fetchRuntimeConfigs();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const closeModal = () => {
    if (!formik.current.isSubmitting) onClose();
  };

  const submitForm = async (values: AddStandbyInstanceFormValues) => {
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
      <>
        <YBModalForm
          visible
          initialValues={INITIAL_VALUES}
          validate={(values: AddStandbyInstanceFormValues) => validateForm(values)}
          validateOnChange
          validateOnBlur
          submitLabel="Continue"
          cancelLabel="Cancel"
          showCancelButton
          title="Add Standby Instance"
          onHide={closeModal}
          onFormSubmit={submitForm}
          render={(formikProps: FormikProps<AddStandbyInstanceFormValues>) => {
            // workaround for outdated version of Formik to access form methods outside of <Formik>
            formik.current = formikProps;
            const errors = formik.current.errors as FormikErrors<AddStandbyInstanceFormErrors>;

            const isHTTPS = formik.current.values?.instanceAddress?.startsWith('https:');
            return (
              <div data-testid="ha-add-standby-instance-modal">
                <Field
                  label="Enter IP Address / Hostname for standby platform instance"
                  name="instanceAddress"
                  placeholder="http://"
                  type="text"
                  component={YBFormInput}
                />
              </div>
            );
          }}
        />
      </>
    );
  } else {
    return null;
  }
};

const validateForm = (values: AddStandbyInstanceFormValues) => {
  // Since our formik version is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  const errors: Partial<AddStandbyInstanceFormErrors> = {};
  if (!values.instanceAddress) {
    errors.instanceAddress = 'Required field';
  } else if (!INSTANCE_VALID_ADDRESS_PATTERN.test(values.instanceAddress)) {
    errors.instanceAddress = 'Must be a valid URL';
  }

  return errors;
};
