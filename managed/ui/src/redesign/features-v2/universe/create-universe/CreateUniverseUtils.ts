import { TFunction } from 'i18next';
import { find, keys, values } from 'lodash';
import { NodeAvailabilityProps } from './steps/nodes-availability/dtos';
import { createUniverseFormProps } from './CreateUniverseContext';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './steps/resilence-regions/dtos';
import { AvailabilityZone, ClusterType, Region } from '../../../helpers/dtos';
import { OtherAdvancedProps } from './steps/advanced-settings/dtos';
import {
  ClusterNodeSpec,
  ClusterStorageSpec,
  CommunicationPortsSpec,
  PlacementRegion,
  UniverseCreateReqBody,
  ClusterNetworkingSpecAllOfEnableExposingService,
  EncryptionInTransitSpec
} from '../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  CloudType,
  DeviceInfo,
  RunTimeConfig
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { Provider } from '@app/components/configRedesign/providerRedesign/types';
import {
  FAULT_TOLERANCE_TYPE,
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from './fields/FieldNames';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { SecuritySettingsProps, CertType } from './steps/security-settings/dtos';

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
    resilienceFactor,
    resilienceType,
    faultToleranceType,
    singleAvailabilityZone
  } = resilienceAndRegionsSettings;

  if (resilienceType === ResilienceType.SINGLE_NODE) {
    const singleZone: Region | AvailabilityZone | undefined = singleAvailabilityZone
      ? regions[0]?.zones.find((region) => region.code === singleAvailabilityZone)
      : regions[0];
    if (singleZone && regions[0]) {
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

  // None and node-level resilience: only the first region and a single AZ (same placement shape).
  if (
    faultToleranceType === FaultToleranceType.NONE ||
    faultToleranceType === FaultToleranceType.NODE_LEVEL
  ) {
    const firstRegion = regions[0];
    const singleZone = firstRegion?.zones?.[0];
    if (!firstRegion || !singleZone) {
      return {};
    }
    const nodeCount =
      faultToleranceType === FaultToleranceType.NODE_LEVEL
        ? getFaultToleranceNeeded(resilienceFactor)
        : 1;
    return {
      [firstRegion.code]: [
        {
          ...singleZone,
          nodeCount,
          preffered: 0
        }
      ]
    };
  }

  const updatedRegions: NodeAvailabilityProps['availabilityZones'] = {};

  const faultToleranceNeeded = getFaultToleranceNeeded(resilienceFactor);

  if (faultToleranceType === FaultToleranceType.AZ_LEVEL) {
    // AZ-level defaults: split required AZs across regions (same remainder pattern as
    // getNodeCountNeeded), then spill forward when a region has fewer zones than its target.
    const selectedZonesByRegion: NodeAvailabilityProps['availabilityZones'] = {};
    const regionCount = regions.length;
    const N = faultToleranceNeeded;

    regions.forEach((region) => {
      selectedZonesByRegion[region.code] = [];
    });

    regions.forEach((region, index) => {
      const targetAzs = getNodeCountNeeded(N, regionCount, index);
      const toTake = Math.min(targetAzs, region.zones.length);
      for (let i = 0; i < toTake; i++) {
        selectedZonesByRegion[region.code].push({
          ...region.zones[i],
          nodeCount: 1, // placeholder; distributed below.
          preffered: i
        });
      }
    });

    let selectedAzCount = values(selectedZonesByRegion).reduce(
      (sum, zones) => sum + zones.length,
      0
    );

    while (selectedAzCount < N) {
      let progressed = false;
      for (const region of regions) {
        if (selectedAzCount >= N) break;
        const already = selectedZonesByRegion[region.code].length;
        if (already >= region.zones.length) continue;
        const zoneIndex = already;
        selectedZonesByRegion[region.code].push({
          ...region.zones[zoneIndex],
          nodeCount: 1,
          preffered: zoneIndex
        });
        selectedAzCount += 1;
        progressed = true;
        break;
      }
      if (!progressed) break;
    }

    if (selectedAzCount === 0) {
      return selectedZonesByRegion;
    }

    let nodesRemaining = faultToleranceNeeded;
    values(selectedZonesByRegion).forEach((zones) => {
      zones.forEach((zone) => {
        zone.nodeCount = 1;
        nodesRemaining -= 1;
      });
    });

    // Distribute any remaining nodes one-by-one in stable order.
    while (nodesRemaining > 0) {
      let assignedInPass = false;
      values(selectedZonesByRegion).forEach((zones) => {
        zones.forEach((zone) => {
          if (nodesRemaining <= 0) return;
          zone.nodeCount += 1;
          nodesRemaining -= 1;
          assignedInPass = true;
        });
      });
      if (!assignedInPass) break;
    }

    return selectedZonesByRegion;
  }

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

export type ExpertNodesStepDefaultPlacement = {
  replicationFactor: number;
  availabilityZones: NodeAvailabilityProps['availabilityZones'];
};

const EXPERT_SINGLE_REGION_DEFAULT_RF = 3;
const EXPERT_DEFAULT_RFS = [3, 5, 7] as const;

function expertMultiRegionRfAndAzCount(regionCount: number): { rf: number; azCount: number } | null {
  const rf = EXPERT_DEFAULT_RFS.find((v) => v >= regionCount);
  if (rf === undefined) return null;
  const azCount = regionCount === 2 ? rf : regionCount;
  return { rf, azCount };
}

function pickZonesRoundRobin(
  regions: ResilienceAndRegionsProps[typeof REGIONS_FIELD],
  azCount: number
): NodeAvailabilityProps['availabilityZones'] {
  const result: NodeAvailabilityProps['availabilityZones'] = {};
  const usedSlots = new Map<string, number>();

  regions.forEach((r) => {
    result[r.code] = [];
    usedSlots.set(r.code, 0);
  });

  let preferred = 0;
  let added = 0;
  let rIdx = 0;
  const maxIter = Math.max(azCount * regions.length * 20, 100);
  let iter = 0;

  while (added < azCount && iter < maxIter) {
    iter += 1;
    const region = regions[rIdx % regions.length];
    rIdx += 1;
    const used = usedSlots.get(region.code) ?? 0;
    // Expert mode defaults should not pre-select AZ names; create empty AZ slots
    // and let users choose specific AZs.
    if (used >= region.zones.length) continue;
    usedSlots.set(region.code, used + 1);
    result[region.code].push({
      name: '',
      uuid: '',
      nodeCount: 1,
      preffered: preferred++
    });
    added += 1;
  }

  return result;
}

function distributeNodesUntilTotalAtLeastRf(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  rf: number
): void {
  const entries: { code: string; zi: number }[] = [];
  Object.entries(availabilityZones).forEach(([code, zones]) => {
    zones.forEach((_, zi) => entries.push({ code, zi }));
  });
  if (entries.length === 0) return;

  let total = entries.reduce(
    (s, e) => s + availabilityZones[e.code][e.zi].nodeCount,
    0
  );
  let i = 0;
  const maxBoost = rf * entries.length + 20;
  let boost = 0;
  while (total < rf && boost < maxBoost) {
    const e = entries[i % entries.length];
    availabilityZones[e.code][e.zi].nodeCount += 1;
    total += 1;
    i += 1;
    boost += 1;
  }
}

/**
 * Decrements per-AZ node counts (never below 1) until total nodes are at most `rf`.
 * Mutates `availabilityZones`. Picks decrements from zones with the highest preferred rank
 * among zones with more than one node; tie-breaks by larger nodeCount, region code, then index.
 */
export function reduceExpertNodeCountsToAtMostRf(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  rf: number
): void {
  let total = getNodeCount(availabilityZones);
  const maxIter = Math.max(total * 50, 100);
  let iter = 0;

  while (total > rf && iter < maxIter) {
    iter += 1;
    let best: {
      regionCode: string;
      zi: number;
      preferred: number;
      nodeCount: number;
    } | null = null;

    for (const regionCode of Object.keys(availabilityZones)) {
      const zones = availabilityZones[regionCode];
      if (!zones?.length) continue;
      for (let zi = 0; zi < zones.length; zi += 1) {
        const zone = zones[zi];
        const nc = zone.nodeCount;
        if (typeof nc !== 'number' || nc <= 1) {
          continue;
        }
        const preferred = typeof zone.preffered === 'number' ? zone.preffered : -1;

        if (!best) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (preferred > best.preferred) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (preferred < best.preferred) {
          continue;
        }
        if (nc > best.nodeCount) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (nc < best.nodeCount) {
          continue;
        }
        if (regionCode > best.regionCode) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (regionCode < best.regionCode) {
          continue;
        }
        if (zi > best.zi) {
          best = { regionCode, zi, preferred, nodeCount: nc };
        }
      }
    }

    if (!best) {
      break;
    }

    availabilityZones[best.regionCode][best.zi].nodeCount -= 1;
    total -= 1;
  }
}

/**
 * Expert-mode defaults when landing on Nodes & availability with no prior zone selection
 * Applies when fault tolerance is AZ-level or region-level; node-level and none use {@link assignRegionsAZNodeByReplicationFactor}.
 * Returns null when defaults do not apply.
 */
export function getExpertNodesStepDefaultPlacement(
  resilience: ResilienceAndRegionsProps
): ExpertNodesStepDefaultPlacement | null {
  if (resilience[RESILIENCE_FORM_MODE] !== ResilienceFormMode.EXPERT_MODE) {
    return null;
  }
  if (resilience[RESILIENCE_TYPE] !== ResilienceType.REGULAR) {
    return null;
  }
  const ft = resilience[FAULT_TOLERANCE_TYPE];
  if (ft !== FaultToleranceType.AZ_LEVEL && ft !== FaultToleranceType.REGION_LEVEL) {
    return null;
  }

  const regions = resilience[REGIONS_FIELD] ?? [];
  if (regions.length === 0) {
    return null;
  }

  if (regions.length === 1) {
    const region = regions[0];
    const zl = region.zones?.length ?? 0;
    if (zl === 0) {
      return null;
    }

    if (zl > 2) {
      const azToUse = Math.min(EXPERT_SINGLE_REGION_DEFAULT_RF, zl);
      const availabilityZones: NodeAvailabilityProps['availabilityZones'] = {
        [region.code]: region.zones.slice(0, azToUse).map((_, index) => ({
          name: '',
          uuid: '',
          nodeCount: 1,
          preffered: index
        }))
      };
      distributeNodesUntilTotalAtLeastRf(availabilityZones, EXPERT_SINGLE_REGION_DEFAULT_RF);
      return { replicationFactor: EXPERT_SINGLE_REGION_DEFAULT_RF, availabilityZones };
    }

    return {
      replicationFactor: EXPERT_SINGLE_REGION_DEFAULT_RF,
      availabilityZones: {
        [region.code]: [
          {
            name: '',
            uuid: '',
            nodeCount: EXPERT_SINGLE_REGION_DEFAULT_RF,
            preffered: 0
          }
        ]
      }
    };
  }

  const spec = expertMultiRegionRfAndAzCount(regions.length);
  if (!spec) {
    return null;
  }

  const availabilityZones = pickZonesRoundRobin(regions, spec.azCount);
  distributeNodesUntilTotalAtLeastRf(availabilityZones, spec.rf);

  return { replicationFactor: spec.rf, availabilityZones };
}

export const canSelectMultipleRegions = (resilienceType?: ResilienceType) => {
  return resilienceType !== ResilienceType.SINGLE_NODE;
};

export const getCreateEITPayload = (
  securitySettings: SecuritySettingsProps,
  cloudType: CloudType
): EncryptionInTransitSpec => {
  const {
    enableNodeToNodeEncryption,
    enableClientToNodeEncryption,
    rootCertificate,
    certType
  } = securitySettings;
  if (cloudType === CloudType.kubernetes) {
    return {
      enable_node_to_node_encrypt: securitySettings?.enableNodeToNodeEncryption ? true : false,
      enable_client_to_node_encrypt: securitySettings?.enableClientToNodeEncryption ? true : false,
      root_ca: securitySettings?.enableNodeToNodeEncryption
        ? certType === CertType?.CUSTOM
          ? securitySettings?.rootCertificate
          : ''
        : '',
      client_root_ca: securitySettings?.enableClientToNodeEncryption
        ? certType === CertType?.CUSTOM
          ? securitySettings?.rootCertificate
          : ''
        : ''
    };
  } else {
    const {
      useSameCertificate,
      enableBothEncryption,
      rootCToNCertificate,
      rootNToNCertificate,
      certType,
      certTypeCToN,
      certTypeNtoN
    } = securitySettings;
    return {
      enable_node_to_node_encrypt: useSameCertificate
        ? enableBothEncryption
        : enableNodeToNodeEncryption,
      enable_client_to_node_encrypt: useSameCertificate
        ? enableBothEncryption
        : enableClientToNodeEncryption,
      root_ca: useSameCertificate
        ? certType === CertType.CUSTOM
          ? rootCertificate
          : ''
        : certTypeNtoN === CertType.CUSTOM
        ? rootNToNCertificate
        : '',
      client_root_ca: useSameCertificate
        ? certType === CertType.CUSTOM
          ? rootCertificate
          : ''
        : certTypeCToN === CertType.CUSTOM
        ? rootCToNCertificate
        : ''
    };
  }
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

  const regionList: PlacementRegion[] = getPlacementRegions(
    resilienceAndRegionsSettings,
    nodesAvailabilitySettings.availabilityZones
  );

  const gflags = mapGFlags(databaseSettings.gFlags);
  const providerType = generalSettings?.providerConfiguration?.code;

  const payload: UniverseCreateReqBody = {
    arch: instanceSettings.arch,
    spec: {
      name: generalSettings.universeName,
      yb_software_version: generalSettings.databaseVersion!,
      encryption_at_rest_spec: {
        kms_config_uuid: securitySettings.kmsConfig
      },
      encryption_in_transit_spec: getCreateEITPayload(securitySettings, providerType!),
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
        enable_ipv6: securitySettings.enableIPV6 ?? false,
        ...(otherAdvancedSettings?.enableExposingService && {
          enable_exposing_service: ClusterNetworkingSpecAllOfEnableExposingService.EXPOSED
        })
      },
      clusters: [
        {
          replication_factor: resilienceAndRegionsSettings.resilienceFactor,
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
          ...(providerType !== CloudType.kubernetes && {
            instance_tags: otherAdvancedSettings?.instanceTags.reduce((acc, tag) => {
              acc[tag.name] = tag.value;
              return acc;
            }, {} as Record<string, string>)
          }),
          networking_spec: {
            enable_lb: true,
            enable_exposing_service: otherAdvancedSettings?.enableExposingService ? ClusterNetworkingSpecAllOfEnableExposingService.EXPOSED : ClusterNetworkingSpecAllOfEnableExposingService.UNEXPOSED,
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
          num_nodes:
            resilienceAndRegionsSettings.resilienceType === ResilienceType.SINGLE_NODE
              ? 1
              : getNodeCount(nodesAvailabilitySettings.availabilityZones),
          node_spec: {
            ...getNodeSpec(formValues),
            dedicated_nodes: effectiveUseDedicatedNodes(formValues)
          },
          placement_spec: {
            cloud_list: [
              {
                code: generalSettings.cloud,
                uuid: generalSettings.providerConfiguration!.uuid!,
                default_region: regionList[0].uuid,
                region_list: regionList
              }
            ]
          },
          partitions_spec: [
            {
              name: 'default',
              default_partition: true,
              replication_factor: resilienceAndRegionsSettings.resilienceFactor,
              tablespace_name: 'default',
              placement: {
                cloud_list: [
                  {
                    code: generalSettings.cloud,
                    uuid: generalSettings.providerConfiguration!.uuid!,
                    default_region: regionList[0].uuid,
                    region_list: regionList
                  }
                ]
              }
            }
          ],
          provider_spec: {
            provider: generalSettings.providerConfiguration!.uuid!,
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

export const getPlacementRegions = (
  resilienceAndRegionsSettings: ResilienceAndRegionsProps,
  availabilityZones?: NodeAvailabilityProps['availabilityZones']
) => {
  const { resilienceFactor, resilienceType } = resilienceAndRegionsSettings;

  const azs =
    availabilityZones ?? assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);

  // For single node, resilience factor should be 1 for the single AZ
  if (resilienceType === ResilienceType.SINGLE_NODE) {
    const region = resilienceAndRegionsSettings.regions[0];

    if (!region) {
      throw new Error(
        `Region with code ${resilienceAndRegionsSettings.singleAvailabilityZone} not found in resilience and regions settings`
      );
    }
    const az = find(region.zones, { code: resilienceAndRegionsSettings.singleAvailabilityZone });
    if (!az) {
      throw new Error(
        `AZ with code ${resilienceAndRegionsSettings.singleAvailabilityZone} not found in resilience and regions settings`
      );
    }
    return [
      {
        uuid: region.uuid,
        name: region.name,
        code: region.code,
        az_list: [
          {
            uuid: az.uuid,
            name: az!.name,
            num_nodes_in_az: 1,
            subnet: az!.subnet,
            leader_affinity: true,
            replication_factor: 1
          }
        ]
      }
    ];
  }

  // Filter out AZs with 0 nodes first, then calculate replication factor distribution
  // This ensures we only distribute replicas across AZs that actually have nodes
  const azsWithNodes: NodeAvailabilityProps['availabilityZones'] = {};
  keys(azs).forEach((regionuuid) => {
    azsWithNodes[regionuuid] = azs[regionuuid].filter((az) => az.nodeCount > 0);
  });

  // Calculate total number of AZs with nodes across all regions
  const totalAZsWithNodes = Object.values(azsWithNodes).reduce(
    (sum, zones) => sum + zones.length,
    0
  );

  // Distribute replication factor across AZs that have nodes
  // Each AZ should get at least 1 replica if possible, then distribute remaining evenly
  // Ensure the sum of all AZ replication_factors equals the cluster replication_factor
  const baseReplicasPerAZ =
    totalAZsWithNodes > 0 ? Math.floor(resilienceFactor / totalAZsWithNodes) : 0;
  const extraReplicas = totalAZsWithNodes > 0 ? resilienceFactor % totalAZsWithNodes : 0;

  let replicaIndex = 0;
  const regionList: PlacementRegion[] = keys(azsWithNodes).map((regionuuid) => {
    const region = find(resilienceAndRegionsSettings.regions, { code: regionuuid });
    if (!region) {
      throw new Error(
        `Region with code ${regionuuid} not found in resilience and regions settings`
      );
    }
    return {
      uuid: region.uuid,
      name: region.name,
      code: region.code,
      az_list: azsWithNodes[regionuuid].map((az) => {
        const azFromRegion = find(region.zones, { uuid: az.uuid });
        // Calculate replication factor for this AZ
        // Distribute replicas: each AZ gets baseReplicasPerAZ, first extraReplicas AZs get one more
        // This ensures the sum equals the total replication_factor
        const azReplicationFactor = baseReplicasPerAZ + (replicaIndex < extraReplicas ? 1 : 0);
        replicaIndex++;

        return {
          uuid: az.uuid,
          name: azFromRegion!.name,
          num_nodes_in_az: az.nodeCount,
          subnet: azFromRegion!.subnet,
          leader_affinity: true,
          replication_factor: azReplicationFactor,
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

/**
 * Builds API storage_spec from form device info. Omits storage_type when unset (e.g. Kubernetes).
 * Matches legacy fillNodeSpec: num_volumes fixed to 1, volume_size = numVolumes * volumeSize from deviceInfo.
 */
const buildStorageSpecFromDeviceInfo = (
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

export const computeResilienceTypeFromProvider = (
  provider: Provider
): {
  [FAULT_TOLERANCE_TYPE]: FaultToleranceType;
  [RESILIENCE_FACTOR]: number;
} => {
  const numOfRegions = provider.regions.length;
  const numOfAZs = ((provider.regions as unknown) as Region[]).reduce(
    (acc, region) => acc + region.zones.length,
    0
  );
  if (numOfRegions > 1) {
    return {
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1
    };
  }

  if (numOfAZs >= 3) {
    return {
      [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
      [RESILIENCE_FACTOR]: 1
    };
  }

  return {
    [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL,
    [RESILIENCE_FACTOR]: 1
  };
};

export const isV2CreateEditUniverseEnabled = (runtimeConfigs: RunTimeConfig) => {
  return (
    runtimeConfigs?.configEntries?.find(
      (config) => config.key === RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI
    )?.value === 'true'
  );
};

export const getAZCount = (availabilityZones: NodeAvailabilityProps['availabilityZones']) => {
  return values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0);
};

export const inferResilience = (
  resilience: ResilienceAndRegionsProps,
  nodesAndAvailability: NodeAvailabilityProps
) => {
  const { regions } = resilience;
  const { replicationFactor = 1, availabilityZones } = nodesAndAvailability;

  if (replicationFactor < 1 || regions.length === 0) {
    return null;
  }

  const azCount = getAZCount(availabilityZones);
  if (azCount === 0) {
    return null;
  }

  if (regions.length > replicationFactor) {
    return null;
  }
  if (regions.length === replicationFactor) {
    return 'REGION_LEVEL';
  }
  if (regions.length < replicationFactor && azCount === replicationFactor) {
    return 'AZ_LEVEL';
  }
  if (regions.length < replicationFactor && azCount < replicationFactor) {
    return 'NODE_LEVEL';
  }

  // Config does not match inference formula (e.g. selected AZs > RF).
  return null;
};

/**
 * Computes the outage count shown in the inferred resilience card.
 * For NODE_LEVEL, cap RF-based tolerance with placement-based tolerance so
 * under-provisioned layouts do not over-claim resilience.
 */
export const getInferredOutageCount = (
  inferredResilience: ReturnType<typeof inferResilience>,
  replicationFactor: number,
  availabilityZones: NodeAvailabilityProps['availabilityZones']
) => {
  const rfOutageCount = Math.max(0, Math.floor((replicationFactor - 1) / 2));
  const nodeCount = getNodeCount(availabilityZones);

  // Placement is under-provisioned for this RF; do not claim outage tolerance.
  if (nodeCount < replicationFactor) {
    return 0;
  }

  if (inferredResilience !== FaultToleranceType.NODE_LEVEL) {
    return rfOutageCount;
  }

  const nodePlacementOutageCount = Math.max(0, Math.floor((nodeCount - 1) / 2));
  return Math.min(rfOutageCount, nodePlacementOutageCount);
};
