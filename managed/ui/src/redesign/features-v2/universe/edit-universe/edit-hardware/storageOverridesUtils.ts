import {
  AvailabilityZoneNodeSpec,
  ClusterNodeSpec,
  ClusterNodeSpecAllOfAzNodeSpec,
  ClusterPlacementSpec,
  ClusterSpec,
  ClusterStorageSpec,
  PlacementAZ,
  PlacementCloud,
  PlacementRegion
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AZ_PREFFERED_HIGHEST_RANK } from '../../create-universe/helpers/constants';
import { getPlacementSpecFromCluster } from '../EditUniverseUtils';

export interface StorageOverrideProcessValue {
  numVolumes?: number;
  volumeSize?: number;
  storageClass?: string;
}

export interface StorageOverrideRowFormValue {
  azUuid: string;
  azName: string;
  tserver: StorageOverrideProcessValue;
  master: StorageOverrideProcessValue;
}

export interface StorageOverridesFormValue {
  overrides: StorageOverrideRowFormValue[];
}

export interface AzOption {
  uuid: string;
  name: string;
}

export interface StorageOverrideZoneChip {
  azUuid: string;
  azName: string;
  regionCode: string;
  regionName: string;
  isPreferred: boolean;
}

const storageSpecToProcessValue = (
  spec: ClusterStorageSpec | undefined
): StorageOverrideProcessValue => ({
  ...(spec?.num_volumes !== undefined && spec?.num_volumes !== null
    ? { numVolumes: spec.num_volumes }
    : {}),
  ...(spec?.volume_size !== undefined && spec?.volume_size !== null
    ? { volumeSize: spec.volume_size }
    : {}),
  ...(spec?.storage_class ? { storageClass: spec.storage_class } : {})
});

const processValueToStorageSpec = (
  value: StorageOverrideProcessValue
): ClusterStorageSpec | null => {
  const numVolumes = value.numVolumes;
  const volumeSize = value.volumeSize;
  const storageClass = value.storageClass?.trim();

  return {
    num_volumes: numVolumes as any,
    volume_size: volumeSize as any,
    storage_class: storageClass
  };
};

export const getEmptyStorageOverrideRow = (): StorageOverrideRowFormValue => ({
  azUuid: '',
  azName: '',
  tserver: {},
  master: {}
});

export const getAzListFromCluster = (cluster: ClusterSpec | undefined): AzOption[] => {
  if(!cluster) return [];

  const placementSpec = getPlacementSpecFromCluster(cluster);
  if (!placementSpec) return [];

  return placementSpec.cloud_list.flatMap((cloud: PlacementCloud) =>
    cloud.region_list?.flatMap((region: PlacementRegion) =>
      region.az_list?.map((az: PlacementAZ) => ({
        uuid: az.uuid,
        name: az.name
      })) ?? []
    ) ?? []
  );
};

export const azNodeSpecToRows = (
  azNodeSpec: ClusterNodeSpecAllOfAzNodeSpec | undefined,
  azList: AzOption[]
): StorageOverrideRowFormValue[] => {
  if (!azNodeSpec) return [];

  const placementUuidSet = new Set(azList.map((az) => az.uuid));

  return Object.keys(azNodeSpec)
    .filter((azUuid) => placementUuidSet.has(azUuid))
    .map((azUuid) => {
      const entry = azNodeSpec[azUuid];
      const azName = azList.find((az) => az.uuid === azUuid)?.name ?? azUuid;
      return azNodeSpecEntryToRow(azUuid, azName, entry);
    });
};

const azNodeSpecEntryToRow = (
  azUuid: string,
  azName: string,
  entry: AvailabilityZoneNodeSpec | undefined
): StorageOverrideRowFormValue => ({
  azUuid,
  azName,
  tserver: storageSpecToProcessValue(entry?.tserver?.storage_spec),
  master: storageSpecToProcessValue(entry?.master?.storage_spec)
});

export const rowsToAzNodeSpec = (
  rows: StorageOverrideRowFormValue[],
  options?: { includeMaster?: boolean }
): ClusterNodeSpecAllOfAzNodeSpec => {
  const includeMaster = options?.includeMaster !== false;
  const result: ClusterNodeSpecAllOfAzNodeSpec = {};

  rows.forEach((row) => {
    if (!row.azUuid?.trim()) return;

    const tserverStorage = processValueToStorageSpec(row.tserver);
    const masterStorage = includeMaster ? processValueToStorageSpec(row.master) : null;
    const perAz: AvailabilityZoneNodeSpec = {};

    if (tserverStorage) {
      perAz.tserver = { storage_spec: tserverStorage };
    }
    if (masterStorage) {
      perAz.master = { storage_spec: masterStorage };
    }

    if (Object.keys(perAz).length > 0) {
      result[row.azUuid] = perAz;
    }
  });

  return result;
};

export const buildNodeSpecWithAzOverrides = (
  currentNodeSpec: ClusterNodeSpec,
  azNodeSpec: ClusterNodeSpecAllOfAzNodeSpec
): ClusterNodeSpec => {
  return {
    ...currentNodeSpec,
    az_node_spec: azNodeSpec
  };
};

export const hasStorageOverrides = (cluster: ClusterSpec | undefined): boolean => {
  if(!cluster) return false;
  return Object.keys(cluster.node_spec?.az_node_spec ?? {}).length > 0;
};

export const getStorageOverrideZoneChips = (
  cluster: ClusterSpec | undefined
): StorageOverrideZoneChip[] => {
  const azNodeSpec = cluster?.node_spec?.az_node_spec;
  if (!azNodeSpec) return [];

  const placementSpec = getPlacementSpecFromCluster(cluster);
  const placementAzByUuid = new Map<
    string,
    { az: PlacementAZ; region: PlacementRegion }
  >();

  placementSpec?.cloud_list.forEach((cloud: PlacementCloud) =>
    cloud.region_list?.forEach((region: PlacementRegion) =>
      region.az_list?.forEach((az: PlacementAZ) => {
        placementAzByUuid.set(az.uuid, { az, region });
      })
    )
  );

  return Object.keys(azNodeSpec).map((azUuid) => {
    const placement = placementAzByUuid.get(azUuid);
    return {
      azUuid,
      azName: placement?.az.name ?? azUuid,
      regionCode: placement?.region.code ?? '',
      regionName: placement?.region.name ?? placement?.region.code ?? '',
      isPreferred:
        placement?.az.leader_preference !== undefined &&
        placement.az.leader_preference === AZ_PREFFERED_HIGHEST_RANK
    };
  });
};
