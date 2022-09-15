import React from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { editXclusterName, fetchUniversesList } from '../../../actions/xClusterReplication';
import { YBModalForm } from '../../common/forms';
import { TargetUniverseForm } from '../ConfigureReplicationModal';
import { Replication } from '../XClusterTypes';
import * as Yup from 'yup';

interface Props {
  visible: boolean;
  onHide: () => void;
  replication: Replication;
}
const validationSchema = Yup.object().shape({
  name: Yup.string().required('Replication name is required'),
  targetUniverseUUID: Yup.string().required('Target universe UUID is required')
});
export function EditReplicationDetails({ onHide, visible, replication }: Props) {
  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );
  const queryClient = useQueryClient();
  const initialValues: any = { ...replication };

  if (universeList) {
    const targetUniverse = universeList.find(
      (universe: any) => universe.universeUUID === replication.targetUniverseUUID
    );
    initialValues['targetUniverseUUID'] = {
      label: targetUniverse?.name,
      value: targetUniverse?.universeUUID
    };
  }

  const modifyXclusterOperation = useMutation(
    (values: Replication) => {
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
              : JSON.stringify(err.response.data.error));
      }
    }
  );

  return (
    <YBModalForm
      size="large"
      title="Edit cluster replication"
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
        if (isUniverseListLoading) {
          return <p>Loading</p>;
        }

        return <TargetUniverseForm isEdit={true} {...props} universeList={universeList} />;
      }}
    />
  );
}
