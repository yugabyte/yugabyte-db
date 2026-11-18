import { createUniverseFormProps } from '../CreateUniverseContext';
import {
  ClusterNodeSpec,
  ClusterStorageSpec,
  ProviderNodeSpec,
  ProviderRootNodesSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType, DeviceInfo } from '@app/redesign/features/universe/universe-form/utils/dto';
import { InstanceSettingProps } from '../steps/hardware-settings/dtos';
import {
  getPrimaryProvider,
  getProviderConfigurations,
  includesKubernetesCloud
} from '../helpers/providerConfigurations';

/**
 * Builds API storage_spec from form device info. Omits storage_type when unset (e.g. Kubernetes).
 * Maps num_volumes / volume_size 1:1 from deviceInfo (same as edit-universe).
 */
export const buildStorageSpecFromDeviceInfo = (
  deviceInfo: DeviceInfo,
  enableEbsVolumeEncryption?: boolean,
  ebsKmsConfigUUID?: string | null
): ClusterStorageSpec => {
  const numVol = Number(deviceInfo.numVolumes);
  const volSize = Number(deviceInfo.volumeSize);
  const storage_spec: ClusterStorageSpec = {
    num_volumes: Number.isFinite(numVol) && numVol > 0 ? numVol : 1,
    volume_size: Number.isFinite(volSize) && volSize > 0 ? volSize : 1,
    ...(deviceInfo.storageClass ? { storage_class: deviceInfo.storageClass } : {}),
    ...(deviceInfo.diskIops !== undefined && deviceInfo.diskIops !== null
      ? { disk_iops: deviceInfo.diskIops }
      : {}),
    ...(deviceInfo.throughput !== undefined && deviceInfo.throughput !== null
      ? { throughput: deviceInfo.throughput }
      : {}),
    ...(deviceInfo.storageType ? { storage_type: deviceInfo.storageType } : {}),
    ...(deviceInfo.mountPoints ? { mount_points: deviceInfo.mountPoints } : {})
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

/** Dedicated for a specific provider code (K8s is always dedicated). */
export const useDedicatedNodesForProvider = (
  providerCode: string | undefined,
  useDedicatedNodes: boolean
): boolean => {
  if (providerCode === CloudType.kubernetes) return true;
  return useDedicatedNodes;
};

/**
 * Dedicated-nodes toggle is hidden for Kubernetes; still treat K8s as dedicated for node_spec and API payload.
 */
export const effectiveUseDedicatedNodes = (formContext: createUniverseFormProps): boolean => {
  const { generalSettings, nodesAvailabilitySettings } = formContext;
  if (!nodesAvailabilitySettings) {
    return false;
  }
  if (
    includesKubernetesCloud(generalSettings?.cloud) ||
    getProviderConfigurations(generalSettings).some(
      (provider) => provider.code === CloudType.kubernetes
    )
  ) {
    return true;
  }
  return nodesAvailabilitySettings.useDedicatedNodes;
};

/** Build ClusterNodeSpec from a single provider's instance settings. */
export const getNodeSpecFromInstanceSettings = (
  instanceSettings: InstanceSettingProps,
  providerCode: string | undefined,
  useDedicated: boolean
): ClusterNodeSpec => {
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
  if (providerCode === CloudType.kubernetes) {
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
    const masterDeviceInfo = instanceSettings.keepMasterTserverSame
      ? instanceSettings.deviceInfo
      : (instanceSettings.masterDeviceInfo ?? instanceSettings.deviceInfo);
    if (masterDeviceInfo) {
      k8sSpec.master = {
        storage_spec: buildStorageSpecFromDeviceInfo(masterDeviceInfo, ebsEnc, ebsKms)
      };
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

export const getNodeSpec = (formContext: createUniverseFormProps): ClusterNodeSpec => {
  const { generalSettings, instanceSettings, nodesAvailabilitySettings } = formContext;
  if (!instanceSettings || !nodesAvailabilitySettings) {
    throw new Error('Missing required form values to get node spec');
  }
  const providerCode =
    getPrimaryProvider(generalSettings)?.code ??
    (typeof generalSettings?.cloud === 'string' ? generalSettings.cloud : undefined);
  return getNodeSpecFromInstanceSettings(
    instanceSettings,
    providerCode,
    effectiveUseDedicatedNodes(formContext)
  );
};

function toProviderNodeSpec(
  partial: {
    instance_type?: string;
    storage_spec?: ClusterStorageSpec;
    k8s_node_resource_spec?: ProviderNodeSpec['k8s_node_resource_spec'];
  }
): ProviderNodeSpec {
  return {
    ...(partial.instance_type ? { instance_type: partial.instance_type } : {}),
    ...(partial.storage_spec ? { storage_spec: partial.storage_spec } : {}),
    ...(partial.k8s_node_resource_spec
      ? { k8s_node_resource_spec: partial.k8s_node_resource_spec }
      : {})
  };
}

/** Map ClusterNodeSpec → ProviderRootNodesSpec for multicloud provider_specs[].nodes_specs. */
export const toProviderNodesSpecs = (
  nodeSpec: ClusterNodeSpec,
  providerCode: string | undefined,
  useDedicated: boolean
): ProviderRootNodesSpec => {
  if (providerCode === CloudType.kubernetes) {
    return {
      tserver_specification: toProviderNodeSpec({
        instance_type: nodeSpec.instance_type,
        storage_spec: nodeSpec.storage_spec,
        k8s_node_resource_spec: nodeSpec.k8s_tserver_resource_spec
      }),
      master_specification: toProviderNodeSpec({
        instance_type: nodeSpec.instance_type,
        storage_spec: nodeSpec.master?.storage_spec ?? nodeSpec.storage_spec,
        k8s_node_resource_spec: nodeSpec.k8s_master_resource_spec
      })
    };
  }

  if (!useDedicated) {
    return {
      tserver_specification: toProviderNodeSpec({
        instance_type: nodeSpec.instance_type,
        storage_spec: nodeSpec.storage_spec
      })
    };
  }

  const tserver = nodeSpec.tserver ?? {
    instance_type: nodeSpec.instance_type,
    storage_spec: nodeSpec.storage_spec
  };
  const master = nodeSpec.master ?? tserver;
  return {
    tserver_specification: toProviderNodeSpec({
      instance_type: tserver.instance_type,
      storage_spec: tserver.storage_spec
    }),
    master_specification: toProviderNodeSpec({
      instance_type: master.instance_type,
      storage_spec: master.storage_spec
    })
  };
};
