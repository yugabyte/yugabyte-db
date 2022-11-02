import React from 'react';
import * as Yup from 'yup';
import { Field } from 'formik';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { editXclusterName } from '../../../actions/xClusterReplication';
import { YBModalForm } from '../../common/forms';
import { XClusterConfig } from '../XClusterTypes';
import { YBFormInput } from '../../common/forms/fields';
import { XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN } from '../constants';

interface Props {
  visible: boolean;
  onHide: () => void;
  replication: XClusterConfig;
}
const validationSchema = Yup.object().shape({
  name: Yup.string()
    .required('Replication name is required')
    .test(
      'Should not contain illegal characters',
      "The name of the replication configuration cannot contain any characters in [SPACE '_' '*' '<' '>' '?' '|' '\"' NULL])",
      (value) =>
        value !== null && value !== undefined && !XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN.test(value)
    )
});
export function EditConfigModal({ onHide, visible, replication }: Props) {
  const queryClient = useQueryClient();
  const initialValues: any = { ...replication };

  const modifyXclusterOperation = useMutation(
    (values: XClusterConfig) => {
      return editXclusterName(values);
    },
    {
      onSuccess: () => {
        queryClient.invalidateQueries(['Xcluster', replication.uuid]);
        onHide();
      },
      onError: (err: any) => {
        toast.error(
          err.response.data.error instanceof String
            ? err.response.data.error
            : JSON.stringify(err.response.data.error)
        );
      }
    }
  );

  return (
    <YBModalForm
      size="large"
      title="Edit Replication Name"
      visible={visible}
      onHide={onHide}
      validationSchema={validationSchema}
      onFormSubmit={(values: any, { setSubmitting }: { setSubmitting: any }) => {
        modifyXclusterOperation
          .mutateAsync(values)
          .then(() => {
            setSubmitting(false);
            onHide();
          })
          .catch(() => {
            setSubmitting(false);
          });
      }}
      initialValues={initialValues}
      submitLabel="Apply Changes"
      showCancelButton
      render={(props: any) => {
        return (
          <Field
            name="name"
            placeholder="Replication name"
            label="Replication Name"
            component={YBFormInput}
          />
        );
      }}
    />
  );
}
