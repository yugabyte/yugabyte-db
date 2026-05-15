import {
  ClusterSpec,
  ClusterSpecClusterType,
  UniverseRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { TFunction } from 'i18next';
import _ from 'lodash';
import { AddRRContextProps } from './AddReadReplicaContext';
import type { RRInstanceSettingsProps } from './steps/RRInstanceSettings/RRInstanceSettings';
import {
  getDefaultRRRegionsAndAZ,
  RRPlacementRegionForm,
  RRRegionsAndAZSettings
} from './steps/RRRegionsAndAZ/dtos';
import { getClusterByType } from '../../edit-universe/EditUniverseUtils';
import {
  getClusterPlacementRegions,
  getReadOnlyCluster
} from '@app/redesign/utils/universeUtils';
import {
  buildRRInstanceSettingsFromCluster,
  readReplicaHardwareMatchesPrimary
} from '../readReplicaUtils';

export function getRRSteps(t: TFunction) {
  return [
    {
      groupTitle: t('placement'),
      subSteps: [
        {
          title: t('regionsAndAZ')
        }
      ]
    },
    {
      groupTitle: t('hardware'),
      subSteps: [
        {
          title: t('instanceSettings')
        }
      ]
    },
    {
      groupTitle: t('database'),
      subSteps: [
        {
          title: t('databaseSettings')
        }
      ]
    },
    {
      groupTitle: t('review'),
      subSteps: [
        {
          title: t('summaryAndCost')
        }
      ]
    }
  ];
}

export const transformGFlagToFlagsArray = (
  masterGFlags: Record<string, any> = {},
  tserverGFlags: Record<string, any> = {}
) => {
  // convert { flagname:value } to => { Name:flagname, TSERVER: value , MASTER: value }
  const tranformFlagsByFlagType = (gFlags: Record<string, any>, flagType: string) => [
    ...Object.keys(gFlags).map((key: string) => ({
      Name: key,
      [flagType]: gFlags[key]
    }))
  ];

  //merge tserver and master glags value into single object if flag Name is same
  return _.values(
    _.merge(
      _.keyBy(tranformFlagsByFlagType(masterGFlags, 'MASTER'), 'Name'),
      _.keyBy(tranformFlagsByFlagType(tserverGFlags, 'TSERVER'), 'Name')
    )
  );
};

export const transformSpecificGFlagToFlagsArray = (specificGFlags: Record<string, any> = {}) => {
  const masterFlags = specificGFlags?.perProcessFlags?.value?.MASTER;
  const tserverFlags = specificGFlags?.perProcessFlags?.value?.TSERVER;
  return transformGFlagToFlagsArray(masterFlags, tserverFlags);
};

const getPrimaryReplicationFactor = (data: UniverseRespResponse) => {
  const primary = getClusterByType(data, ClusterSpecClusterType.PRIMARY) as
    | { replication_factor?: number; replicationFactor?: number }
    | undefined;
  const rf = primary?.replication_factor ?? primary?.replicationFactor;
  return typeof rf === 'number' && rf > 0 ? rf : 3;
};

/**
 * Maps the existing read-replica (ASYNC) cluster placement into wizard form state.
 */
export function getRegionsAndAZFromReadReplicaCluster(
  cluster: ClusterSpec,
  primaryRf: number
): RRRegionsAndAZSettings | null {
  const placementRegions = getClusterPlacementRegions(cluster);
  if (!placementRegions.length) {
    return null;
  }
  const rf = Math.max(1, primaryRf || 1);
  const regions: RRPlacementRegionForm[] = placementRegions.map((pr) => ({
    regionUuid: pr.uuid ?? null,
    isNew: false,
    zones: (pr.az_list ?? []).map((az) => ({
      zoneUuid: az.uuid ?? null,
      nodeCount: Math.max(1, az.num_nodes_in_az ?? 1),
      dataCopies: Math.max(1, az.replication_factor ?? rf)
    }))
  }));
  if (
    !regions.length ||
    regions.some((r) => !r.regionUuid || !r.zones.length || r.zones.some((z) => !z.zoneUuid))
  ) {
    return null;
  }
  return { regions };
}

export const getInitialValues = (data: UniverseRespResponse): Partial<AddRRContextProps> => {
  const primaryClusterSpec = getClusterByType(data, ClusterSpecClusterType.PRIMARY) as
    | ClusterSpec
    | undefined;
  const primaryRf = getPrimaryReplicationFactor(data);
  const asyncCluster = getReadOnlyCluster(data.spec?.clusters ?? []);
  const inheritPrimaryInstance =
    !asyncCluster || readReplicaHardwareMatchesPrimary(primaryClusterSpec, asyncCluster);

  const hardwareCluster =
    (inheritPrimaryInstance ? primaryClusterSpec : asyncCluster) ?? primaryClusterSpec;

  const arch = data?.info?.arch;
  const instanceSettings: RRInstanceSettingsProps =
    hardwareCluster && arch
      ? buildRRInstanceSettingsFromCluster(hardwareCluster, arch, inheritPrimaryInstance)
      : {
          inheritPrimaryInstance: true,
          arch,
          instanceType: primaryClusterSpec?.node_spec?.instance_type ?? null,
          useSpotInstance: primaryClusterSpec?.use_spot_instance ?? false,
          deviceInfo: null,
          enableEbsVolumeEncryption: false,
          ebsKmsConfigUUID: null
        };

  const fromReplica =
    asyncCluster && getRegionsAndAZFromReadReplicaCluster(asyncCluster, primaryRf);
  const regionsAndAZ = fromReplica ?? getDefaultRRRegionsAndAZ();

  const primaryWithOptionalSpecificGFlags = primaryClusterSpec as ClusterSpec & {
    specificGFlags?: Record<string, unknown>;
  };

  return {
    databaseSettings: {
      gFlags: primaryWithOptionalSpecificGFlags?.specificGFlags
        ? transformSpecificGFlagToFlagsArray(primaryWithOptionalSpecificGFlags.specificGFlags)
        : transformGFlagToFlagsArray(
            primaryClusterSpec?.gflags?.master,
            primaryClusterSpec?.gflags?.tserver
          ),
      customizeRRFlags: false
    },
    regionsAndAZ,
    regionsAndAZBaseline: _.cloneDeep(regionsAndAZ),
    readReplicaPlacementFromUniverse: Boolean(fromReplica),
    ...(Boolean(arch) && {
      instanceSettings,
      instanceSettingsInitial: _.cloneDeep(instanceSettings)
    })
  };
};

export const getDBVersion = (data: UniverseRespResponse) => {
  return _.get(data, 'spec.yb_software_version', '');
};
