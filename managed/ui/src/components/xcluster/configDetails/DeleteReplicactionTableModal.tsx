import { FC } from 'react';
import { useSelector } from 'react-redux';

import { YBConfirmModal } from '../../modals';
import { XClusterModalName } from '../constants';

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
      title="Confirm Remove Table from Replication"
      currentModal={XClusterModalName.REMOVE_TABLE_FROM_CONFIG}
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
      {`Do you want to remove the table "${deleteTableName}" from the replication stream? Note that this does not drop the table from the database.`}
    </YBConfirmModal>
  );
};

export default DeleteReplicactionTableModal;
