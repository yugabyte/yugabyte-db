import React from 'react';
import { YBModal, YBModalProps } from '../../../../redesign/components';
import { K8sRegionField } from '../forms/configureRegion/ConfigureK8sRegionModal';
import { CloudVendorRegionField } from '../forms/configureRegion/ConfigureRegionModal';

interface DeleteModalProps<RegionFieldType extends CloudVendorRegionField | K8sRegionField>
  extends YBModalProps {
  deleteRegion: (region: RegionFieldType) => void;
  onClose: () => void;
  region: RegionFieldType | undefined;
}

export const DeleteRegionModal = <RegionFieldType extends CloudVendorRegionField | K8sRegionField>({
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
        Are you sure you want to delete <b>{region?.code}</b>
      </p>
      <p>
        All region information will be deleted, including VPC ID, Security Group ID, Custom AMI ID
        if applicable, and AZ mapping.
      </p>
    </YBModal>
  );
};
