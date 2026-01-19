import { FC, useRef } from 'react';
import { yba } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  mapUniversePayloadToResilienceAndRegionsProps,
  useEditUniverseContext
} from '../EditUniverseUtils';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../create-universe/CreateUniverseContext';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { InstanceSettings } from '../../create-universe/steps';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

interface EditHardwareConfirmModalProps {
  visible: boolean;
  onSubmit: () => void;
  onHide: () => void;
  title?: string;
}

const { YBModal } = yba;

export const EditHardwareConfirmModal: FC<EditHardwareConfirmModalProps> = ({
  visible,
  onSubmit,
  onHide,
  title
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware.editHardwareModal'
  });

  const instanceSettingsRef = useRef<StepsRef>(null);

  const { universeData, providerRegions } = useEditUniverseContext();

  if (!visible) return null;

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const providerUUID = primaryCluster?.provider_spec.provider;
  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const useDedicatedNodes = primaryCluster?.node_spec.dedicated_nodes;
  const stats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);
  const regions = mapUniversePayloadToResilienceAndRegionsProps(
    providerRegions!,
    stats,
    primaryCluster!
  );

  const storageSpec = primaryCluster?.node_spec.storage_spec;

  return (
    <YBModal
      open={visible}
      onSubmit={() => {
        instanceSettingsRef.current?.onNext();
      }}
      onClose={onHide}
      title={title ?? t('title')}
      dialogContentProps={{ sx: { padding: '16px !important' } }}
      size="md"
      submitLabel={t('submitLabel')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      overrideHeight={'fit-content'}
      titleSeparator
    >
      <CreateUniverseContext.Provider
        value={
          ([
            {
              activeStep: 1,
              resilienceAndRegionsSettings: regions,
              generalSettings: {
                providerConfiguration: {
                  uuid: providerUUID,
                  code: providerCode
                }
              },
              nodesAvailabilitySettings: {
                useDedicatedNodes: !!useDedicatedNodes
              },
              instanceSettings: {
                arch: universeData?.info?.arch,
                instanceType: primaryCluster?.node_spec.instance_type,
                useSpotInstance: primaryCluster?.use_spot_instance,
                deviceInfo: {
                  volumeSize: storageSpec?.volume_size,
                  numVolumes: storageSpec?.num_volumes,
                  diskIops: storageSpec?.disk_iops,
                  throughput: storageSpec?.throughput,
                  storageClass: storageSpec?.storage_class,
                  storageType: storageSpec?.storage_type
                }
              } as InstanceSettingProps
            },
            {
              setResilienceType: () => {},
              saveResilienceAndRegionsSettings: (data: ResilienceAndRegionsProps) => {},
              saveInstanceSettings: () => {},
              moveToNextPage: () => {},
              moveToPreviousPage: () => {}
            }
          ] as unknown) as createUniverseFormProps
        }
      >
        <InstanceSettings editMode={true} ref={instanceSettingsRef} />
      </CreateUniverseContext.Provider>
    </YBModal>
  );
};
