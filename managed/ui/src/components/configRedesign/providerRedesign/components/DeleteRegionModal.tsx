import React from 'react';
import { YBModal, YBModalProps } from '../../../../redesign/components';

import { SupportedRegionField } from '../forms/configureRegion/types';

interface DeleteModalProps<RegionFieldType extends SupportedRegionField> extends YBModalProps {
  deleteRegion: (region: RegionFieldType) => void;
  onClose: () => void;
  region: RegionFieldType | undefined;
}

export const DeleteRegionModal = <RegionFieldType extends SupportedRegionField>({
  deleteRegion,
  onClose,
  region,
  ...modalProps
}: DeleteModalProps<RegionFieldType>) => {
  const onSubmit = () => {
    if (region !== undefined) {
      deleteRegion(region);
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
        Are you sure you want to delete <b>{region?.code}</b>?
      </p>
      <p>All region information will be deleted.</p>
    </YBModal>
  );
};
