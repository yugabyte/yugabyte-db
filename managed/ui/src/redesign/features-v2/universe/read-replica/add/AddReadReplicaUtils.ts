import { UniverseRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { TFunction } from 'i18next';
import _ from 'lodash';
import { AddRRContextProps } from './AddReadReplicaContext';

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

export const getInitialValues = (data: UniverseRespResponse): Partial<AddRRContextProps> => {
  const primaryClusterSpec = _.get(data, 'spec.clusters[0]');
  const storageSpec = primaryClusterSpec?.node_spec.storage_spec;
  const cloudVolumeEncryption = storageSpec?.cloud_volume_encryption;
  const instanceSettings = {
    inheritPrimaryInstance: true,
    arch: data?.info?.arch,
    instanceType: primaryClusterSpec?.node_spec.instance_type,
    useSpotInstance: primaryClusterSpec?.use_spot_instance,
    deviceInfo: {
      volumeSize: storageSpec?.volume_size,
      numVolumes: storageSpec?.num_volumes,
      diskIops: storageSpec?.disk_iops,
      throughput: storageSpec?.throughput,
      storageClass: storageSpec?.storage_class,
      storageType: storageSpec?.storage_type
    },
    enableEbsVolumeEncryption: cloudVolumeEncryption?.enable_volume_encryption ?? false,
    ebsKmsConfigUUID: cloudVolumeEncryption?.kms_config_uuid ?? null
  };
  return {
    databaseSettings: {
      gFlags: primaryClusterSpec?.specificGFlags
        ? transformSpecificGFlagToFlagsArray(primaryClusterSpec?.specificGFlags)
        : transformGFlagToFlagsArray(
            primaryClusterSpec?.gflags?.master,
            primaryClusterSpec?.gflags?.tserver
          ),
      customizeRRFlags: false
    },
    ...(data?.info?.arch && {
      instanceSettings,
      instanceSettingsInitial: instanceSettings
    })
  };
};

export const getDBVersion = (data: UniverseRespResponse) => {
  return _.get(data, 'spec.yb_software_version', '');
};
