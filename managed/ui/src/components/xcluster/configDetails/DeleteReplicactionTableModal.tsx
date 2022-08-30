import React, { FC } from 'react';
import { useSelector } from 'react-redux';
import { YBConfirmModal } from '../../modals';

interface DeleteModalProps {
  deleteTableName: string;
  onConfirm: () => void;
  onCancel: () => void;
}

const DeleteReplicactionTableModal: FC<DeleteModalProps> = ({
  deleteTableName,
  onConfirm,
  onCancel
}) => {
  const { visibleModal } = useSelector((state: any) => state.modal);

  return (
    <YBConfirmModal
      name="delete-replication-modal"
      title="Confirm remove table"
      currentModal={'DeleteReplicationTableModal'}
      visibleModal={visibleModal}
      confirmLabel="Delete"
      cancelLabel="Cancel"
      onConfirm={() => {
        onConfirm();
      }}
      hideConfirmModal={() => {
        onCancel();
      }}
    >
      Do you want to remove the table "{deleteTableName}"?
    </YBConfirmModal>
  );
};

export default DeleteReplicactionTableModal;
