import { TFunction } from 'i18next';
import { find, keys, values } from 'lodash';
import { NodeAvailabilityProps, Zone } from './steps/nodes-availability/dtos';
import { createUniverseFormProps } from './CreateUniverseContext';
import { FaultToleranceType, ResilienceAndRegionsProps, ResilienceType } from './steps/resilence-regions/dtos';
import { AvailabilityZone, ClusterType, Region } from '../../helpers/dtos';
import { OtherAdvancedProps } from './steps/advanced-settings/dtos';
import { CommunicationPortsSpec, PlacementRegion, UniverseCreateReqBody } from '../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export function getCreateUniverseSteps(t: TFunction) {
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
        {
          title: t('nodesAndAvailabilityZone')
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

export function getFaultToleranceNeeded(
  replicationFactor: number,
) {
  return replicationFactor + 2;
};

export function getFaultToleranceNeededForAZ(
  replicationFactor: number,
) {
  return replicationFactor * 2 + 1;
};

export const getNodeCount = (availabilityZones: NodeAvailabilityProps['availabilityZones']) => {
  if (keys(availabilityZones).length === 0) {
    return 0;
  }
  return Object.values(availabilityZones).reduce((total, zones) => {
    return total + zones.reduce((sum, zone) => sum + zone.nodeCount, 0);
  }, 0);
};

export const getNodeCountForRegion = (totalNodesCount: number, totalRegions: number, regionIndex: number) => {
  const base = Math.floor(totalNodesCount / totalRegions);
  const extra = regionIndex < (totalNodesCount % totalRegions) ? 1 : 0;
  return base + extra;
};

export const assignRegionsAZNodeByReplicationFactor = (
  resilienceAndRegionsSettings: ResilienceAndRegionsProps
): NodeAvailabilityProps['availabilityZones'] => {

  const { regions = [], replicationFactor, resilienceType, faultToleranceType, singleAvailabilityZone } = resilienceAndRegionsSettings;

  if (resilienceType === ResilienceType.SINGLE_NODE || faultToleranceType === FaultToleranceType.NONE) {
    let singleZone: Region | AvailabilityZone | undefined;
    if (resilienceType === ResilienceType.SINGLE_NODE) {
      singleZone = singleAvailabilityZone ? regions[0].zones.find(region => region.code === singleAvailabilityZone) : regions[0];
    }
    if (resilienceAndRegionsSettings.faultToleranceType === FaultToleranceType.NONE) {
      singleZone = regions[0].zones[0];
    }
    if (singleZone) {
      return {
        [regions[0].code]: [{
          ...singleZone,
          nodeCount: 1,
          preffered: '0'
        }
        ]
      };
    };
    return {};
  }

  const updatedRegions: NodeAvailabilityProps['availabilityZones'] = {};


  const faultToleranceNeeded = faultToleranceType === FaultToleranceType.AZ_LEVEL ? getFaultToleranceNeededForAZ(replicationFactor)
    : faultToleranceType === FaultToleranceType.NODE_LEVEL ? 1
      : getFaultToleranceNeeded(replicationFactor);

  values(regions).forEach((region, index) => {
    const nodeCount = getNodeCountForRegion(faultToleranceNeeded, regions.length, index);
    updatedRegions[region.code] = [];
    for (let i = 0; i < nodeCount; i++) {
      updatedRegions[region.code].push({
        ...region.zones[i % region.zones.length],
        nodeCount: 1,
        preffered: i === 0 ? 'true' : 'false'
      });
    }
  });

  return updatedRegions;
};


export const rebalanceRegionNodes = (
  az: NodeAvailabilityProps['availabilityZones'],
  newTotalNodes: number,
  regions: ResilienceAndRegionsProps['regions']
): NodeAvailabilityProps['availabilityZones'] => {
  // Flatten all zones into a single list for distribution
  const allZones: { region: string; zone: Zone }[] = [];

  for (const region in az) {
    for (const zone of az[region]) {
      allZones.push({ region, zone: { ...zone, nodeCount: 0 } }); // reset node count
    }
  }

  regions.forEach(region => {
    region.zones.forEach(zone => {
      const existingZone = find(allZones, { zone: { uuid: zone.uuid } });
      if (!existingZone) {
        allZones.push({ region: region.code, zone: { ...zone, nodeCount: 0, preffered: "false" } });
      }
    });
  });


  const totalZones = allZones.length;

  // Step 1: Assign 1 node to each zone if we have enough nodes
  for (const item of allZones) {
    if (newTotalNodes > 0) {
      item.zone.nodeCount = 1;
      newTotalNodes--;
    }
  }

  // Step 2: Distribute remaining nodes (round-robin, prefer preferred zones if needed)
  const sortedZones = allZones.sort((a, b) =>
    b.zone.preffered === "true" ? 1 : a.zone.name.localeCompare(b.zone.name) ? 1 : -1
  );

  let index = 0;
  while (newTotalNodes > 0) {
    const item = sortedZones[index % totalZones];
    item.zone.nodeCount++;
    newTotalNodes--;
    index++;
  }


  // Step 3: Reconstruct the structure
  const updatedRegions: NodeAvailabilityProps['availabilityZones'] = {};
  for (const { region, zone } of allZones) {
    if (zone.nodeCount === 0) continue; // Skip zones with no nodes
    if (!updatedRegions[region]) {
      updatedRegions[region] = [];
    }
    updatedRegions[region].push(zone);
  }

  return updatedRegions;
};


export const canSelectMultipleRegions = (resilienceType?: ResilienceType) => {
  return resilienceType !== ResilienceType.SINGLE_NODE;
};


export const mapCreateUniversePayload = (formValues: createUniverseFormProps): UniverseCreateReqBody => {

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

  const azs = assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);

  const regionList: PlacementRegion[] = keys(azs).map((regionuuid) => {
    const region = find(resilienceAndRegionsSettings.regions, { code: regionuuid });
    if (!region) {
      throw new Error(`Region with code ${regionuuid} not found in resilience and regions settings`);
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
        };
      })
    };
  });

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
      use_time_sync: true,
      ycql: {
        ...databaseSettings.ycql,
      },
      ysql: {
        ...databaseSettings.ysql,
      },
      networking_spec: {
        assign_public_ip: true,
        assign_static_public_ip: false,
        communication_ports: mapCommunicationPorts(otherAdvancedSettings),
        enable_ipv6: false,
      },
      clusters: [
        {
          replication_factor: resilienceAndRegionsSettings.replicationFactor,
          cluster_type: ClusterType.PRIMARY,
          use_spot_instance: false,
          audit_log_config: {
            universe_logs_exporter_config: []
          },
          gflags: {
            az_gflags: {},
            master: {},
            tserver: {}
          },
          instance_tags: otherAdvancedSettings.instanceTags.reduce((acc, tag) => {
            acc[tag.name] = tag.value;
            return acc;
          }, {} as Record<string, string>),
          networking_spec: {
            enable_lb: proxySettings.enableProxyServer,
            enable_exposing_service: "UNEXPOSED",
            proxy_config: {
              http_proxy: proxySettings.enableProxyServer ? `${proxySettings.webProxy}` : "",
              https_proxy: proxySettings.secureWebProxy ? `${proxySettings.secureWebProxyServer}:${proxySettings.secureWebProxyPort}` : "",
              no_proxy_list: proxySettings.byPassProxyListValues.split('\n')
            }
          },
          num_nodes: getNodeCount(nodesAvailabilitySettings.availabilityZones),
          node_spec: {
            instance_type: "c5.large",
            dedicated_nodes: nodesAvailabilitySettings.useDedicatedNodes,
            storage_spec: {
              num_volumes: 1,
              storage_type: "GP3",
              storage_class: "standard",
              volume_size: 250,
              disk_iops: 3000,
              throughput: 125
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
            region_list: regionList.map(r => r.uuid!),
            image_bundle_uuid: 'c997367a-37eb-4c07-afaf-68ff2da04674',
            access_key_code: otherAdvancedSettings.accessKeyCode
          },
        },
      ],
    },
  };

  return payload;
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
    yb_controller_rpc_port: otherSettings.ybControllerrRpcPort,
  };
};
