import React from 'react';
import { useMutation } from 'react-query';
import { browserHistory } from 'react-router';

import { deleteXclusterConfig } from '../../../actions/xClusterReplication';
import { YBCheckBox, YBModal } from '../../common/forms/fields';

import { XClusterConfig } from '../XClusterTypes';

interface DeleteConfigModalProps {
  currentUniverseUUID: string;
  xClusterConfig: XClusterConfig;
  onHide: () => void;
  visible: boolean;
}

export const DeleteConfigModal = ({
  currentUniverseUUID,
  xClusterConfig,
  onHide,
  visible
}: DeleteConfigModalProps) => {
  const deleteConfig = useMutation((xClusterConfigUUID: string) => {
    return deleteXclusterConfig(xClusterConfigUUID).then(() => {
      browserHistory.push(`/universes/${currentUniverseUUID}/replication`);
    });
  });

  const handleFormSubmit = () => {
    deleteConfig.mutate(xClusterConfig.uuid);
    onHide();
  };

  return (
    <YBModal
      visible={visible}
      formName={'DeleteConfigForm'}
      onHide={onHide}
      onFormSubmit={handleFormSubmit}
      submitLabel="Delete Replication"
      title={`Delete Replication: ${xClusterConfig.name}`}
      footerAccessory={
        <div className="force-delete">
          <YBCheckBox
            label="Ignore errors and force delete"
            className="footer-accessory"
            disabled={true}
            input={{ checked: true }}
          />
        </div>
      }
    >
      <p>Are you sure you want to delete this replication?</p>
    </YBModal>
  );
};
