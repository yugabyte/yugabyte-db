import { YBModal, YBModalProps } from '../../../../../redesign/components';

import { InstanceType } from '../../../../../redesign/helpers/dtos';

interface DeleteInstanceTpeModalProps extends YBModalProps {
  deleteInstanceType: (instanceType: InstanceType) => void;
  onClose: () => void;
  instanceType: InstanceType | undefined;
}

export const DeleteInstanceTypeModal = ({
  deleteInstanceType,
  onClose,
  instanceType,
  ...modalProps
}: DeleteInstanceTpeModalProps) => {
  const onSubmit = () => {
    if (instanceType !== undefined) {
      deleteInstanceType(instanceType);
    }
    onClose();
  };

  return (
    <YBModal
      title="Delete Region"
      submitLabel="Delete"
      cancelLabel="Cancel"
      onSubmit={onSubmit}
      onClose={onClose}
      overrideHeight="fit-content"
      {...modalProps}
    >
      <p>
        Are you sure you want to delete <b>{instanceType?.instanceTypeCode}</b>
      </p>
      <p>All related instance type information will be removed.</p>
    </YBModal>
  );
};
