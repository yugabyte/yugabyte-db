import { TFunction } from 'i18next';
import { find, has, keys, values } from 'lodash';
import { NodeAvailabilityProps, Zone } from './steps/nodes-availability/dtos';
import { createUniverseFormProps } from './CreateUniverseContext';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceType
} from './steps/resilence-regions/dtos';
import { AvailabilityZone, ClusterType, Region } from '../../../helpers/dtos';
import { OtherAdvancedProps } from './steps/advanced-settings/dtos';
import {
  ClusterNodeSpec,
  CommunicationPortsSpec,
  PlacementRegion,
  UniverseCreateReqBody
} from '../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType, DeviceInfo, RunTimeConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import { Provider } from '@app/components/configRedesign/providerRedesign/types';
import { FAULT_TOLERANCE_TYPE, REPLICATION_FACTOR } from './fields/FieldNames';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';

export function getCreateUniverseSteps(t: TFunction, resilienceType?: ResilienceType) {
  return [
    {
      groupTitle: t('general'),
      subSteps: [
        {
          title: t('generalSettings')
        }
      ]
    },
    {
      groupTitle: t('placement'),
      subSteps: [
        {
          title: t('resilienceAndRegions')
        },
        ...(resilienceType === ResilienceType.REGULAR
          ? [
            {
              title: t('nodesAndAvailabilityZone')
            }
          ]
          : [])
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
      groupTitle: t('security'),
      subSteps: [
        {
          title: t('securitySettings')
        }
      ]
    },
    {
      groupTitle: t('advanced'),
      subSteps: [
        {
          title: t('proxySettings')
        },
        {
          title: t('otherAdvancedSettings')
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

export function getFaultToleranceNeeded(replicationFactor: number) {
  return replicationFactor + 2;
}

export function getFaultToleranceNeededForAZ(replicationFactor: number) {
  return replicationFactor * 2 + 1;
}

export const getNodeCount = (availabilityZones: NodeAvailabilityProps['availabilityZones']) => {
  if (keys(availabilityZones).length === 0) {
    return 0;
  }
  return Object.values(availabilityZones).reduce((total, zones) => {
    return total + zones.reduce((sum, zone) => sum + zone.nodeCount, 0);
  }, 0);
};

export const getNodeCountNeeded = (
  totalNodesCount: number,
  totalRegions: number,
  regionIndex: number
) => {
  const base = Math.floor(totalNodesCount / totalRegions);
  const extra = regionIndex < totalNodesCount % totalRegions ? 1 : 0;
  return base + extra;
};

export const assignRegionsAZNodeByReplicationFactor = (
  resilienceAndRegionsSettings: ResilienceAndRegionsProps
): NodeAvailabilityProps['availabilityZones'] => {
  const {
    regions = [],
    replicationFactor,
    resilienceType,
    faultToleranceType,
    singleAvailabilityZone
  } = resilienceAndRegionsSettings;

  if (
    resilienceType === ResilienceType.SINGLE_NODE ||
    faultToleranceType === FaultToleranceType.NONE
  ) {
    let singleZone: Region | AvailabilityZone | undefined;
    if (resilienceType === ResilienceType.SINGLE_NODE) {
      singleZone = singleAvailabilityZone
        ? regions[0].zones.find((region) => region.code === singleAvailabilityZone)
        : regions[0];
    }
    if (resilienceAndRegionsSettings.faultToleranceType === FaultToleranceType.NONE) {
      singleZone = regions[0].zones[0];
    }
    if (singleZone) {
      return {
        [regions[0].code]: [
          {
            ...singleZone,
            nodeCount: 1,
            preffered: 0
          }
        ]
      };
    }
    return {};
  }

  const updatedRegions: NodeAvailabilityProps['availabilityZones'] = {};

  const faultToleranceNeeded =
    faultToleranceType === FaultToleranceType.AZ_LEVEL
      ? getFaultToleranceNeededForAZ(replicationFactor)
      : getFaultToleranceNeeded(replicationFactor);

  values(regions).forEach((region, index) => {
    const nodeCount = getNodeCountNeeded(faultToleranceNeeded, regions.length, index);

    updatedRegions[region.code] = [];

    region.zones.forEach((zone, index) => {
      const nodesNeededForAZ = Math.floor(nodeCount / region.zones.length);

      const remainder = nodeCount % region.zones.length;

      const nodeCountForAz = nodesNeededForAZ + (index < remainder ? 1 : 0);

      if (nodeCountForAz === 0) return;

      updatedRegions[region.code].push({
        ...zone,
        nodeCount: nodeCountForAz,
        preffered: index
      });
    });
  });

  return updatedRegions;
};

export const canSelectMultipleRegions = (resilienceType?: ResilienceType) => {
  return resilienceType !== ResilienceType.SINGLE_NODE;
};

export const mapCreateUniversePayload = (
  formValues: createUniverseFormProps
): UniverseCreateReqBody => {
  const {
    generalSettings,
    resilienceAndRegionsSettings,
    nodesAvailabilitySettings,
    instanceSettings,
    databaseSettings,
    securitySettings,
    proxySettings,
    otherAdvancedSettings
  } = formValues;

  if (
    !generalSettings ||
    !resilienceAndRegionsSettings ||
    !nodesAvailabilitySettings ||
    !instanceSettings ||
    !databaseSettings ||
    !securitySettings ||
    !proxySettings ||
    !otherAdvancedSettings
  ) {
    throw new Error('Missing required form values to create universe payload');
  }

  const regionList: PlacementRegion[] = getPlacementRegions(resilienceAndRegionsSettings);

  const gflags = mapGFlags(databaseSettings.gFlags);

  const payload: UniverseCreateReqBody = {
    arch: instanceSettings.arch,
    spec: {
      name: generalSettings.universeName,
      yb_software_version: generalSettings.databaseVersion!,
      encryption_at_rest_spec: {
        kms_config_uuid: securitySettings.kmsConfig
      },
      encryption_in_transit_spec: {
        root_ca: securitySettings.rootCertificate,
        client_root_ca: securitySettings.rootCToNCertificate,
        enable_client_to_node_encrypt: securitySettings.enableClientToNodeEncryption,
        enable_node_to_node_encrypt: securitySettings.enableNodeToNodeEncryption
      },
      use_time_sync: otherAdvancedSettings.useTimeSync,
      ycql: {
        ...databaseSettings.ycql
      },
      ysql: {
        ...databaseSettings.ysql,
        enable_connection_pooling: databaseSettings.enableConnectionPooling ?? false
      },
      networking_spec: {
        assign_public_ip: securitySettings.assignPublicIP,
        assign_static_public_ip: false,
        communication_ports: mapCommunicationPorts(otherAdvancedSettings),
        enable_ipv6: false
      },
      clusters: [
        {
          replication_factor: resilienceAndRegionsSettings.replicationFactor,
          cluster_type: ClusterType.PRIMARY,
          use_spot_instance: instanceSettings.useSpotInstance,
          audit_log_config: {
            universe_logs_exporter_config: []
          },
          gflags: {
            az_gflags: {},
            master: {
              ...gflags.master
            },
            tserver: {
              ...gflags.tserver
            },

            ...(databaseSettings.enablePGCompatibitilty && {
              ['gflag_groups']: ['ENHANCED_POSTGRES_COMPATIBILITY']
            })
          },
          instance_tags: otherAdvancedSettings.instanceTags.reduce((acc, tag) => {
            acc[tag.name] = tag.value;
            return acc;
          }, {} as Record<string, string>),
          networking_spec: {
            enable_lb: true,
            enable_exposing_service: 'UNEXPOSED',
            ...(proxySettings.enableProxyServer
              ? {
                proxy_config: {
                  http_proxy:
                    proxySettings.enableProxyServer && proxySettings.webProxy
                      ? `${proxySettings.webProxyServer}:${proxySettings.webProxyPort}`
                      : '',
                  https_proxy: proxySettings.secureWebProxy
                    ? `${proxySettings.secureWebProxyServer}:${proxySettings.secureWebProxyPort}`
                    : '',
                  no_proxy_list: proxySettings.byPassProxyListValues ?? []
                }
              }
              : {})
          },
          num_nodes: getNodeCount(nodesAvailabilitySettings.availabilityZones),
          node_spec: {
            instance_type: instanceSettings.instanceType!,
            dedicated_nodes: nodesAvailabilitySettings.useDedicatedNodes,
            storage_spec: {
              num_volumes: 1,
              storage_type: instanceSettings.deviceInfo!.storageType!,
              storage_class: instanceSettings.deviceInfo!.storageClass!,
              volume_size:
                instanceSettings.deviceInfo!.numVolumes * instanceSettings.deviceInfo!.volumeSize!,
              disk_iops: instanceSettings.deviceInfo!.diskIops!,
              throughput: instanceSettings.deviceInfo!.throughput!
            }
          },
          placement_spec: {
            cloud_list: [
              {
                code: generalSettings.cloud,
                uuid: generalSettings.providerConfiguration.uuid,
                default_region: regionList[0].uuid,
                region_list: regionList
              }
            ]
          },
          provider_spec: {
            provider: generalSettings.providerConfiguration.uuid,
            region_list: regionList.map((r) => r.uuid!),
            image_bundle_uuid: instanceSettings.imageBundleUUID!,
            access_key_code: otherAdvancedSettings.accessKeyCode
          }
        }
      ]
    }
  };

  return payload;
};

export const getPlacementRegions = (resilienceAndRegionsSettings: ResilienceAndRegionsProps) => {
  const azs = assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);
  const regionList: PlacementRegion[] = keys(azs).map((regionuuid) => {
    const region = find(resilienceAndRegionsSettings.regions, { code: regionuuid });
    if (!region) {
      throw new Error(
        `Region with code ${regionuuid} not found in resilience and regions settings`
      );
    }
    return {
      uuid: region.uuid,
      name: region.code,
      code: region.name,
      az_list: azs[regionuuid].map((az) => {
        const azFromRegion = find(region.zones, { uuid: az.uuid });
        return {
          uuid: az.uuid,
          name: azFromRegion!.name,
          num_nodes_in_az: az.nodeCount,
          subnet: azFromRegion!.subnet,
          leader_affinity: true,
          replication_factor: resilienceAndRegionsSettings.replicationFactor,
          ...(az.preffered !== undefined ? { leader_preference: az.preffered + 1 } : {})
        };
      })
    };
  });
  return regionList;
};

const mapCommunicationPorts = (otherSettings: OtherAdvancedProps): CommunicationPortsSpec => {
  return {
    master_http_port: otherSettings.masterHttpPort,
    master_rpc_port: otherSettings.masterRpcPort,
    tserver_http_port: otherSettings.tserverHttpPort,
    tserver_rpc_port: otherSettings.tserverRpcPort,
    yql_server_http_port: otherSettings.yqlServerHttpPort,
    yql_server_rpc_port: otherSettings.yqlServerRpcPort,
    ysql_server_http_port: otherSettings.ysqlServerHttpPort,
    ysql_server_rpc_port: otherSettings.ysqlServerRpcPort,
    redis_server_http_port: otherSettings.redisServerHttpPort,
    redis_server_rpc_port: otherSettings.redisServerRpcPort,
    node_exporter_port: otherSettings.nodeExporterPort,
    yb_controller_rpc_port: otherSettings.ybControllerrRpcPort
  };
};

const mapGFlags = (
  gflags: {
    Name: string;
    MASTER?: string | boolean | number;
    TSERVER?: string | boolean | number;
  }[]
) => {
  const gflagsMap: { master: Record<string, string>; tserver: Record<string, string> } = {
    master: {},
    tserver: {}
  };
  gflags.forEach((gflag) => {
    if (gflag.MASTER) {
      gflagsMap.master[gflag.Name] = gflag.MASTER.toString();
    }
    if (gflag.TSERVER) {
      gflagsMap.tserver[gflag.Name] = gflag.TSERVER.toString();
    }
  });
  return gflagsMap;
};

const fillNodeSpec = (deviceType?: string | null, deviceInfo?: DeviceInfo | null) => {
  if (!deviceInfo || !deviceType) {
    throw new Error('Instance settings are required to fill node spec');
  }
  return {
    instance_type: deviceType,
    storage_spec: {
      num_volumes: 1,
      storage_type: deviceInfo.storageType!,
      storage_class: deviceInfo.storageClass!,
      volume_size: deviceInfo.numVolumes * deviceInfo.volumeSize!,
      disk_iops: deviceInfo.diskIops!,
      throughput: deviceInfo.throughput!
    }
  };
};

export const getNodeSpec = (formContext: createUniverseFormProps): ClusterNodeSpec => {
  const { generalSettings, instanceSettings, nodesAvailabilitySettings } = formContext;
  if (!instanceSettings || !nodesAvailabilitySettings) {
    throw new Error('Missing required form values to get node spec');
  }
  if (!nodesAvailabilitySettings.useDedicatedNodes) {
    return fillNodeSpec(instanceSettings.instanceType, instanceSettings.deviceInfo);
  }

  if (nodesAvailabilitySettings.useDedicatedNodes) {
    if (instanceSettings.keepMasterTserverSame) {
      return {
        master: fillNodeSpec(instanceSettings.instanceType, instanceSettings.deviceInfo),
        tserver: fillNodeSpec(instanceSettings.instanceType, instanceSettings.deviceInfo)
      };
    }
    if (generalSettings?.cloud === CloudType.kubernetes) {
      return {
        k8s_master_resource_spec: {
          cpu_core_count: instanceSettings.masterK8SNodeResourceSpec?.cpuCoreCount,
          memory_gib: instanceSettings.masterK8SNodeResourceSpec?.memoryGib
        },
        k8s_tserver_resource_spec: {
          cpu_core_count: instanceSettings.tserverK8SNodeResourceSpec?.cpuCoreCount,
          memory_gib: instanceSettings.tserverK8SNodeResourceSpec?.memoryGib
        }
      };
    }
  }
  return {
    master: fillNodeSpec(instanceSettings.masterInstanceType, instanceSettings.masterDeviceInfo),
    tserver: fillNodeSpec(instanceSettings.instanceType, instanceSettings.deviceInfo)
  };
};

export const computeFaultToleranceTypeFromProvider = (
  provider: Provider
): {
  [FAULT_TOLERANCE_TYPE]: FaultToleranceType;
  [REPLICATION_FACTOR]: number;
} => {
  const numOfRegions = provider.regions.length;
  const numOfAZs = ((provider.regions as unknown) as Region[]).reduce(
    (acc, region) => acc + region.zones.length,
    0
  );
  if (numOfRegions >= 3) {
    return {
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.REGION_LEVEL,
      [REPLICATION_FACTOR]: numOfRegions >= 7 ? 3 : numOfRegions > 4 ? 2 : 1
    };
  }

  if (numOfAZs >= 3) {
    return {
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [REPLICATION_FACTOR]: numOfAZs >= 7 ? 3 : numOfAZs > 5 ? 2 : 1
    };
  }

  return {
    [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
    [REPLICATION_FACTOR]: 3
  };
};

export const isV2CreateEditUniverseEnabled = (runtimeConfigs: RunTimeConfig) => {
  return runtimeConfigs?.configEntries?.find(
    (config) => config.key === RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI
  )?.value === 'true';
};
