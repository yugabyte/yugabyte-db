import { FC, useEffect, useState } from 'react';
import { YBButton } from '../../common/forms/fields';
import { DeleteModal } from '../modals/DeleteModal';
import './HAErrorPlaceholder.scss';

interface HAErrorPlaceholderProps {
  error: unknown;
  configUUID: string;
}

export const HAErrorPlaceholder: FC<HAErrorPlaceholderProps> = ({ error, configUUID }) => {
  const [showDeleteModal, setShowDeleteModal] = useState(false);

  useEffect(() => {
    console.error(error);
  }, [error]);

  return (
    <>
      <YBButton
        btnText="Delete Existing Configuration"
        btnClass="btn btn-orange delete-err-ha-button"
        onClick={() => {
          setShowDeleteModal(true);
        }}
      />
      <div className="ha-error-placeholder" data-testid="ha-generic-error">
        <div>
          <i className="fa fa-exclamation-circle ha-error-placeholder__icon" />
        </div>
        <div>Something went wrong.</div>
      </div>
      <DeleteModal
        configId={configUUID}
        isStandby={false}
        visible={showDeleteModal}
        onClose={() => setShowDeleteModal(false)}
      />
    </>
  );
};
