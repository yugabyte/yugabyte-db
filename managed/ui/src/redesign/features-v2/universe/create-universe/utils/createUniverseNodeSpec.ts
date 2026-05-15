import { createUniverseFormProps } from '../CreateUniverseContext';
import {
  ClusterNodeSpec,
  ClusterStorageSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType, DeviceInfo } from '@app/redesign/features/universe/universe-form/utils/dto';

/**
 * Builds API storage_spec from form device info. Omits storage_type when unset (e.g. Kubernetes).
 * Matches legacy fillNodeSpec: num_volumes fixed to 1, volume_size = numVolumes * volumeSize from deviceInfo.
 */
export const buildStorageSpecFromDeviceInfo = (
  deviceInfo: DeviceInfo,
  enableEbsVolumeEncryption?: boolean,
  ebsKmsConfigUUID?: string | null
): ClusterStorageSpec => {
  const numVol = Number(deviceInfo.numVolumes);
  const volSize = Number(deviceInfo.volumeSize);
  const storage_spec: ClusterStorageSpec = {
    num_volumes: 1,
    volume_size:
      (Number.isFinite(numVol) && numVol > 0 ? numVol : 1) *
      (Number.isFinite(volSize) && volSize > 0 ? volSize : 1),
    ...(deviceInfo.storageClass ? { storage_class: deviceInfo.storageClass } : {}),
    ...(deviceInfo.diskIops !== undefined && deviceInfo.diskIops !== null
      ? { disk_iops: deviceInfo.diskIops }
      : {}),
    ...(deviceInfo.throughput !== undefined && deviceInfo.throughput !== null
      ? { throughput: deviceInfo.throughput }
      : {}),
    ...(deviceInfo.storageType ? { storage_type: deviceInfo.storageType } : {})
  };

  if (enableEbsVolumeEncryption) {
    storage_spec.cloud_volume_encryption = {
      enable_volume_encryption: enableEbsVolumeEncryption,
      kms_config_uuid: ebsKmsConfigUUID ?? ''
    };
  }

  return storage_spec;
};

const fillNodeSpec = (
  deviceType?: string | null,
  deviceInfo?: DeviceInfo | null,
  enableEbsVolumeEncryption?: boolean,
  ebsKmsConfigUUID?: string | null
): ClusterNodeSpec => {
  if (!deviceInfo || !deviceType) {
    throw new Error('Instance settings are required to fill node spec');
  }
  return {
    instance_type: deviceType,
    storage_spec: buildStorageSpecFromDeviceInfo(
      deviceInfo,
      enableEbsVolumeEncryption,
      ebsKmsConfigUUID
    )
  };
};

/**
 * Dedicated-nodes toggle is hidden for Kubernetes; still treat K8s as dedicated for node_spec and API payload.
 */
export const effectiveUseDedicatedNodes = (formContext: createUniverseFormProps): boolean => {
  const { generalSettings, nodesAvailabilitySettings } = formContext;
  if (!nodesAvailabilitySettings) {
    return false;
  }
  if (generalSettings?.cloud === CloudType.kubernetes) {
    return true;
  }
  return nodesAvailabilitySettings.useDedicatedNodes;
};

export const getNodeSpec = (formContext: createUniverseFormProps): ClusterNodeSpec => {
  const { generalSettings, instanceSettings, nodesAvailabilitySettings } = formContext;
  if (!instanceSettings || !nodesAvailabilitySettings) {
    throw new Error('Missing required form values to get node spec');
  }
  const useDedicated = effectiveUseDedicatedNodes(formContext);
  if (!useDedicated) {
    return fillNodeSpec(
      instanceSettings.instanceType,
      instanceSettings.deviceInfo,
      instanceSettings.enableEbsVolumeEncryption,
      instanceSettings.ebsKmsConfigUUID
    );
  }

  const ebsEnc = instanceSettings.enableEbsVolumeEncryption;
  const ebsKms = instanceSettings.ebsKmsConfigUUID;

  // Kubernetes: K8s custom resources may have null instanceType; fillNodeSpec would throw.
  // When master/tserver settings match, both pods use tserver resource spec.
  if (generalSettings?.cloud === CloudType.kubernetes) {
    const tserverK8s = instanceSettings.tserverK8SNodeResourceSpec;
    const masterK8s = instanceSettings.keepMasterTserverSame
      ? tserverK8s
      : instanceSettings.masterK8SNodeResourceSpec;
    const k8sSpec: ClusterNodeSpec = {
      k8s_master_resource_spec: {
        cpu_core_count: masterK8s?.cpuCoreCount,
        memory_gib: masterK8s?.memoryGib
      },
      k8s_tserver_resource_spec: {
        cpu_core_count: tserverK8s?.cpuCoreCount,
        memory_gib: tserverK8s?.memoryGib
      }
    };
    if (instanceSettings.deviceInfo) {
      k8sSpec.storage_spec = buildStorageSpecFromDeviceInfo(
        instanceSettings.deviceInfo,
        ebsEnc,
        ebsKms
      );
      if (instanceSettings.instanceType) {
        k8sSpec.instance_type = instanceSettings.instanceType;
      }
    }
    return k8sSpec;
  }
  if (instanceSettings.keepMasterTserverSame) {
    const shared = fillNodeSpec(
      instanceSettings.instanceType,
      instanceSettings.deviceInfo,
      ebsEnc,
      ebsKms
    );
    // Top-level storage_spec is required so UserIntentMapper populates userIntent.deviceInfo.
    return {
      ...shared,
      master: { ...shared },
      tserver: { ...shared }
    };
  }
  const tserverSpec = fillNodeSpec(
    instanceSettings.instanceType,
    instanceSettings.deviceInfo,
    ebsEnc,
    ebsKms
  );
  return {
    ...tserverSpec,
    master: fillNodeSpec(
      instanceSettings.masterInstanceType,
      instanceSettings.masterDeviceInfo,
      ebsEnc,
      ebsKms
    ),
    tserver: tserverSpec
  };
};
