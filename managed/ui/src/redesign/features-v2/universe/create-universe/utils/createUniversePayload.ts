import { omit, isEmpty } from 'lodash';
import { TFunction } from 'i18next';
import { createUniverseFormProps } from '../CreateUniverseContext';
import { ResilienceFormMode, ResilienceType } from '../steps/resilence-regions/dtos';
import { OtherAdvancedProps } from '../steps/advanced-settings/dtos';
import {
  CommunicationPortsSpec,
  PlacementRegion,
  UniverseCreateReqBody,
  ClusterNetworkingSpecAllOfEnableExposingService,
  EncryptionInTransitSpec,
  ClusterPlacementSpec,
  ClusterPerProviderSpec,
  ClusterNetworkingSpec,
  PlacementCloud
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType, Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { isCloudVendorCloudType } from '@app/components/configRedesign/providerRedesign/utils';
import { ClusterType } from '../../../../helpers/dtos';
import { SecuritySettingsProps, CertType } from '../steps/security-settings/dtos';
import { getEffectiveReplicationFactorForResilience } from './resilienceReplication';
import { getNodeCount, getPlacementRegions } from './placementAndAvailability';
import {
  effectiveUseDedicatedNodes,
  getNodeSpec,
  getNodeSpecFromInstanceSettings,
  toProviderNodesSpecs,
  useDedicatedNodesForProvider
} from './createUniverseNodeSpec';
import {
  DEFAULT_CONNECTION_POOLING_PORTS,
  shouldKeepCustomInternalYsqlPort
} from '../helpers/syncConnectionPoolingPorts';
import { DEFAULT_COMMUNICATION_PORTS } from '../helpers/constants';
import {
  getPrimaryProvider,
  getProviderConfigurations,
  isMultiProvider,
  primaryCloudCode
} from '../helpers/providerConfigurations';
import { withEffectiveRegions } from '../helpers/placementsByProvider';
import { getEffectiveAvailabilityZones } from '../helpers/nodesByProvider';
import { ProviderType } from '../steps/general-settings/dtos';
import { InstanceSettingProps } from '../steps/hardware-settings/dtos';

export const getCreateEITPayload = (
  securitySettings: SecuritySettingsProps,
  cloudType: CloudType
): EncryptionInTransitSpec => {
  const { enableNodeToNodeEncryption, enableClientToNodeEncryption, rootCertificate, certType } =
    securitySettings;
  if (cloudType === CloudType.kubernetes) {
    // enableBothEncryption is the K8s EIT master toggle; turning it off leaves the
    // N2N/C2N checkboxes at their previous (default true) values.
    const nToN = !!securitySettings.enableBothEncryption && !!enableNodeToNodeEncryption;
    const cToN = !!securitySettings.enableBothEncryption && !!enableClientToNodeEncryption;
    const caCert = certType === CertType.CUSTOM ? rootCertificate : '';
    return {
      root_and_client_root_ca_same: true,
      enable_node_to_node_encrypt: nToN,
      enable_client_to_node_encrypt: cToN,
      root_ca: nToN ? caCert : '',
      client_root_ca: cToN ? caCert : ''
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
      root_and_client_root_ca_same: useSameCertificate,
      enable_node_to_node_encrypt: useSameCertificate
        ? enableBothEncryption
          ? true
          : false
        : enableNodeToNodeEncryption,
      enable_client_to_node_encrypt: useSameCertificate
        ? enableBothEncryption
          ? true
          : false
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

export const mapCommunicationPorts = (
  otherSettings: Partial<OtherAdvancedProps>
): CommunicationPortsSpec => {
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
    yb_controller_rpc_port: otherSettings.ybControllerRpcPort,
    internal_ysql_server_rpc_port: otherSettings.internalYsqlServerRpcPort
  };
};

export const mapGFlags = (
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
    // Keep falsy values (0, false, ""); truthy checks would drop them.
    if (Object.hasOwn(gflag, 'MASTER')) {
      gflagsMap.master[gflag.Name] = String(gflag.MASTER);
    }
    if (Object.hasOwn(gflag, 'TSERVER')) {
      gflagsMap.tserver[gflag.Name] = String(gflag.TSERVER);
    }
  });
  return gflagsMap;
};

export function buildPrimaryCloudList(
  generalSettings: NonNullable<createUniverseFormProps['generalSettings']>,
  regionList: PlacementRegion[]
) {
  return [
    {
      code: primaryCloudCode(generalSettings)!,
      uuid: getPrimaryProvider(generalSettings)!.uuid!,
      default_region: regionList[0].uuid,
      region_list: regionList
    }
  ];
}

function regionCodeToProviderUuid(regions: Region[]): Map<string, string> {
  const map = new Map<string, string>();
  for (const region of regions) {
    if (region.code && region.providerUuid) {
      map.set(region.code, region.providerUuid);
    }
  }
  return map;
}

function partitionPlacementRegionsByProvider(
  regionList: PlacementRegion[],
  providers: ProviderType[],
  formRegions: Region[]
): Map<string, PlacementRegion[]> {
  const codeToProvider = regionCodeToProviderUuid(formRegions);
  const byProvider = new Map<string, PlacementRegion[]>();
  for (const provider of providers) {
    byProvider.set(provider.uuid, []);
  }
  for (const placementRegion of regionList) {
    const providerUuid = placementRegion.code
      ? codeToProvider.get(placementRegion.code)
      : undefined;
    if (!providerUuid || !byProvider.has(providerUuid)) continue;
    byProvider.get(providerUuid)!.push(placementRegion);
  }
  return byProvider;
}

export function buildPlacementCloudList(
  generalSettings: NonNullable<createUniverseFormProps['generalSettings']>,
  regionList: PlacementRegion[],
  formRegions: Region[]
): PlacementCloud[] {
  if (!isMultiProvider(generalSettings)) {
    return buildPrimaryCloudList(generalSettings, regionList);
  }

  const providers = getProviderConfigurations(generalSettings);
  const byProvider = partitionPlacementRegionsByProvider(regionList, providers, formRegions);
  return providers.map((provider) => {
    const regions = byProvider.get(provider.uuid) ?? [];
    return {
      code: provider.code,
      uuid: provider.uuid,
      region_list: regions
    };
  });
}

function mapInstanceTags(
  otherAdvancedSettings: OtherAdvancedProps,
  providerCode: string | undefined
): Record<string, string> | undefined {
  // Tags are only editable for cloud vendors; drop any left over from a provider switch.
  if (!isCloudVendorCloudType(providerCode)) return undefined;
  return otherAdvancedSettings.instanceTags.reduce(
    (acc, tag) => {
      if (tag.name && tag.value) acc[tag.name] = tag.value;
      return acc;
    },
    {} as Record<string, string>
  );
}

function mapClusterNetworkingSpec(
  securitySettings: SecuritySettingsProps,
  proxySettings: NonNullable<createUniverseFormProps['proxySettings']>
): ClusterNetworkingSpec {
  return {
    enable_lb: false,
    enable_exposing_service: securitySettings?.enableExposingService
      ? ClusterNetworkingSpecAllOfEnableExposingService.EXPOSED
      : ClusterNetworkingSpecAllOfEnableExposingService.UNEXPOSED,
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
            ...(proxySettings.byPassProxyList
              ? { no_proxy_list: proxySettings.byPassProxyListValues ?? [] }
              : {})
          }
        }
      : {})
  };
}

function buildProviderSpecs(
  providers: ProviderType[],
  formValues: createUniverseFormProps,
  regionListByProvider: Map<string, PlacementRegion[]>,
  sharedNetworking: ClusterNetworkingSpec
): ClusterPerProviderSpec[] {
  const { nodesAvailabilitySettings, otherAdvancedSettings, instanceByProvider } = formValues;
  const useDedicatedToggle = !!nodesAvailabilitySettings?.useDedicatedNodes;

  return providers.map((provider) => {
    const settings: InstanceSettingProps | undefined = instanceByProvider?.[provider.uuid];
    if (!settings) {
      throw new Error(`Missing instance settings for provider ${provider.uuid}`);
    }
    const useDedicated = useDedicatedNodesForProvider(provider.code, useDedicatedToggle);
    const nodeSpec = getNodeSpecFromInstanceSettings(settings, provider.code, useDedicated);
    const regions = regionListByProvider.get(provider.uuid) ?? [];
    const tags = mapInstanceTags(otherAdvancedSettings!, provider.code);

    return {
      provider: provider.uuid,
      region_list: regions.map((region) => region.uuid!).filter(Boolean),
      image_bundle_uuid: settings.imageBundleUUID!,
      access_key_code: otherAdvancedSettings!.accessKeyCodeByProvider?.[provider.uuid] ?? '',
      aws_instance_profile: otherAdvancedSettings!.awsArnString,
      ...(provider.code === CloudType.kubernetes && {
        ...(!isEmpty(otherAdvancedSettings!.universeOverrides) && {
          helm_overrides: otherAdvancedSettings!.universeOverrides
        }),
        ...(!isEmpty(otherAdvancedSettings!.azOverrides) && {
          az_helm_overrides: otherAdvancedSettings!.azOverrides
        })
      }),
      nodes_specs: toProviderNodesSpecs(nodeSpec, provider.code, useDedicated),
      networking_spec: sharedNetworking,
      ...(tags && { instance_tags: tags })
    };
  });
}

export const constructPlacements = (formValues: createUniverseFormProps): ClusterPlacementSpec => {
  const {
    generalSettings,
    resilienceAndRegionsSettings,
    nodesAvailabilitySettings,
    placementsByProvider,
    nodesByProvider
  } = formValues;
  if (!generalSettings || !resilienceAndRegionsSettings || !nodesAvailabilitySettings) {
    throw new Error('Missing required form values to consturct placement spec');
  }

  const resilience = withEffectiveRegions(resilienceAndRegionsSettings, placementsByProvider);
  const availabilityZones = getEffectiveAvailabilityZones(
    nodesAvailabilitySettings,
    nodesByProvider
  );
  const regionList: PlacementRegion[] = getPlacementRegions(
    resilience,
    availabilityZones,
    nodesAvailabilitySettings
  );

  return {
    cloud_list: buildPlacementCloudList(generalSettings, regionList, resilience.regions)
  };
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
    otherAdvancedSettings,
    placementsByProvider,
    nodesByProvider,
    instanceByProvider
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

  const multi = isMultiProvider(generalSettings);
  const primaryProvider = getPrimaryProvider(generalSettings);
  const providers = getProviderConfigurations(generalSettings);
  const primaryInstanceSettings: InstanceSettingProps =
    multi && primaryProvider?.uuid && instanceByProvider?.[primaryProvider.uuid]
      ? instanceByProvider[primaryProvider.uuid]
      : instanceSettings;

  const resilience = withEffectiveRegions(resilienceAndRegionsSettings, placementsByProvider);

  const effectiveRf = getEffectiveReplicationFactorForResilience(
    resilience,
    nodesAvailabilitySettings
  );

  const availabilityZones = getEffectiveAvailabilityZones(
    nodesAvailabilitySettings,
    nodesByProvider
  );

  const regionList: PlacementRegion[] = getPlacementRegions(
    resilience,
    availabilityZones,
    nodesAvailabilitySettings
  );

  const gflags = mapGFlags(databaseSettings.gFlags);
  const providerType = primaryProvider?.code;
  const cloudList = buildPlacementCloudList(generalSettings, regionList, resilience.regions);
  const clusterNetworking = mapClusterNetworkingSpec(securitySettings, proxySettings);
  const instanceTags = mapInstanceTags(otherAdvancedSettings, providerType);
  const dedicatedNodes = effectiveUseDedicatedNodes(formValues);
  const numNodes =
    resilienceAndRegionsSettings.resilienceType === ResilienceType.SINGLE_NODE
      ? 1
      : getNodeCount(availabilityZones);

  const regionListByProvider = multi
    ? partitionPlacementRegionsByProvider(regionList, providers, resilience.regions)
    : null;

  const payload: UniverseCreateReqBody = {
    arch: primaryInstanceSettings.arch,
    spec: {
      name: generalSettings.universeName,
      yb_software_version: generalSettings.databaseVersion!,
      encryption_at_rest_spec: {
        kms_config_uuid: securitySettings.kmsConfig
      },
      encryption_in_transit_spec: getCreateEITPayload(securitySettings, providerType!),
      // Auth on a disabled endpoint is rejected at create ("Cannot enable YSQL/YCQL Authentication...").
      ycql: {
        ...omit(databaseSettings.ycql, 'confirm_pwd'),
        enable_auth: !!(databaseSettings.ycql?.enable && databaseSettings.ycql?.enable_auth)
      },
      ysql: {
        ...omit(databaseSettings.ysql, 'confirm_pwd'),
        enable_auth: !!(databaseSettings.ysql?.enable && databaseSettings.ysql?.enable_auth),
        enable_connection_pooling: databaseSettings.enableConnectionPooling ?? false
      },
      networking_spec: {
        assign_public_ip: securitySettings.assignPublicIP,
        assign_static_public_ip: false,
        communication_ports: mapCommunicationPorts(
          // K8s does not support custom deployment ports — always send defaults.
          providerType === CloudType.kubernetes
            ? DEFAULT_COMMUNICATION_PORTS
            : {
                ...otherAdvancedSettings,
                // Custom Internal YSQL applies while CP is on. Turning CP off resets it to default.
                ...(!shouldKeepCustomInternalYsqlPort(databaseSettings, providerType) && {
                  internalYsqlServerRpcPort:
                    DEFAULT_CONNECTION_POOLING_PORTS.internalYsqlServerRpcPort
                })
              }
        ),
        enable_ipv6: securitySettings.enableIPV6 ?? false
      },
      universe_settings: {
        expert_mode:
          resilienceAndRegionsSettings.resilienceFormMode === ResilienceFormMode.EXPERT_MODE
      },
      clusters: [
        {
          replication_factor: effectiveRf,
          cluster_type: ClusterType.PRIMARY,
          use_spot_instance: primaryInstanceSettings.useSpotInstance,
          dedicated_nodes: dedicatedNodes,
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
          ...(instanceTags && { instance_tags: instanceTags }),
          networking_spec: clusterNetworking,
          num_nodes: numNodes,
          placement_spec: {
            cloud_list: cloudList
          },
          partitions_spec: [
            {
              name: 'default',
              default_partition: true,
              replication_factor: effectiveRf,
              tablespace_name: 'default',
              placement: {
                cloud_list: cloudList
              }
            }
          ],
          ...(multi
            ? {
                provider_specs: buildProviderSpecs(
                  providers,
                  formValues,
                  regionListByProvider!,
                  clusterNetworking
                )
              }
            : {
                node_spec: {
                  ...getNodeSpec(formValues),
                  dedicated_nodes: dedicatedNodes
                },
                provider_spec: {
                  provider: primaryProvider!.uuid!,
                  region_list: regionList.map((r) => r.uuid!),
                  image_bundle_uuid: instanceSettings.imageBundleUUID!,
                  access_key_code:
                    otherAdvancedSettings.accessKeyCodeByProvider?.[primaryProvider!.uuid!] ?? '',
                  aws_instance_profile: otherAdvancedSettings.awsArnString,
                  ...(providerType === CloudType.kubernetes && {
                    ...(!isEmpty(otherAdvancedSettings.universeOverrides) && {
                      helm_overrides: otherAdvancedSettings.universeOverrides
                    }),
                    ...(!isEmpty(otherAdvancedSettings.azOverrides) && {
                      az_helm_overrides: otherAdvancedSettings.azOverrides
                    })
                  })
                }
              })
        }
      ]
    }
  };

  return payload;
};

export const mapPortsKeys: any = () => {
  return {
    master_http_port: 'masterHttpPort',
    master_rpc_port: 'masterRpcPort',
    tserver_http_port: 'tserverHttpPort',
    tserver_rpc_port: 'tserverRpcPort',
    yql_server_http_port: 'yqlServerHttpPort',
    yql_server_rpc_port: 'yqlServerRpcPort',
    ysql_server_http_port: 'ysqlServerHttpPort',
    ysql_server_rpc_port: 'ysqlServerRpcPort',
    internal_ysql_server_rpc_port: 'internalYsqlServerRpcPort',
    redis_server_http_port: 'redisServerHttpPort',
    redis_server_rpc_port: 'redisServerRpcPort',
    node_exporter_port: 'nodeExporterPort',
    yb_controller_rpc_port: 'ybControllerRpcPort'
  };
};

export const mapAPIPortsKeys: any = () => {
  return {
    masterHttpPort: 'master_http_port',
    masterRpcPort: 'master_rpc_port',
    tserverHttpPort: 'tserver_http_port',
    tserverRpcPort: 'tserver_rpc_port',
    yqlServerHttpPort: 'yql_server_http_port',
    yqlServerRpcPort: 'yql_server_rpc_port',
    ysqlServerHttpPort: 'ysql_server_http_port',
    ysqlServerRpcPort: 'ysql_server_rpc_port',
    redisServerHttpPort: 'redis_server_http_port',
    redisServerRpcPort: 'redis_server_rpc_port',
    internalYsqlServerRpcPort: 'internal_ysql_server_rpc_port',
    nodeExporterPort: 'node_exporter_port',
    ybControllerrRpcPort: 'yb_controller_rpc_port',
    ybControllerRpcPort: 'yb_controller_rpc_port'
  };
};

export const mapAPIPortValues = (communicationPorts: Partial<CommunicationPortsSpec>) => {
  const portsObj: any = {};
  Object.entries(communicationPorts).forEach(([key, val]) => {
    if (mapPortsKeys()[key]) {
      portsObj[`${mapPortsKeys()[key]}`] = val;
    }
  });
  return portsObj;
};

export const getAccessiblePorts = (
  enableYSQL: boolean | undefined,
  enableYCQL: boolean | undefined,
  providerCode: string,
  enableCP: boolean | undefined,
  t: TFunction,
  isEditMode = false
) => {
  const MASTER_PORTS = [
    { id: 'masterHttpPort', visible: true, disabled: false },
    { id: 'masterRpcPort', visible: true, disabled: false }
  ];

  const TSERVER_PORTS = [
    { id: 'tserverHttpPort', visible: true, disabled: false },
    { id: 'tserverRpcPort', visible: true, disabled: false }
  ];

  const YCQL_PORTS = [
    {
      id: 'yqlServerHttpPort',
      visible: enableYCQL,
      disabled: isEditMode
    },
    {
      id: 'yqlServerRpcPort',
      visible: enableYCQL, //ycqlEnabled,
      disabled: isEditMode
    }
  ].filter((ports) => ports.visible);

  const YSQL_PORTS = [
    { id: 'ysqlServerHttpPort', visible: enableYSQL, disabled: isEditMode }, //visible: ysqlEnabled,
    {
      id: 'ysqlServerRpcPort',
      visible: enableYSQL,
      disabled: isEditMode || providerCode === CloudType.kubernetes,
      helperText: true
    },
    {
      id: 'internalYsqlServerRpcPort',
      visible: enableYSQL && enableCP,
      disabled: isEditMode || providerCode === CloudType.kubernetes,
      helperText: true
    }
  ].filter((ports) => ports.visible);

  const REDIS_PORTS = [
    { id: 'redisServerHttpPort', visible: false, disabled: isEditMode },
    { id: 'redisServerRpcPort', visible: false, disabled: isEditMode }
  ];

  const OTHER_PORTS = [
    { id: 'nodeExporterPort', visible: providerCode !== CloudType.onprem, disabled: false },
    {
      id: 'ybControllerRpcPort',
      visible: true,
      disabled: isEditMode
    }
  ];

  const PORT_GROUPS = [
    {
      name: t('masterGroup'),
      PORTS_LIST: MASTER_PORTS,
      visible: MASTER_PORTS.length > 0
    },
    {
      name: t('tServerGroup'),
      PORTS_LIST: TSERVER_PORTS,
      visible: TSERVER_PORTS.length > 0
    },
    {
      name: t('ysqlGroup'),
      PORTS_LIST: YSQL_PORTS,
      visible: YSQL_PORTS.length > 0
    },
    {
      name: t('ycqlGroup'),
      PORTS_LIST: YCQL_PORTS,
      visible: YCQL_PORTS.length > 0
    },
    {
      name: t('redisGroup'),
      PORTS_LIST: REDIS_PORTS,
      visible: REDIS_PORTS.length > 0
    },
    {
      name: t('othersGroup'),
      PORTS_LIST: OTHER_PORTS,
      visible: OTHER_PORTS.length > 0
    }
  ].filter((pg) => pg.visible);

  return PORT_GROUPS;
};

/** Port field names currently shown in Deployment Ports (same set the UI renders). */
export const getCommunicationPortFieldIds = (
  enableYSQL: boolean | undefined,
  enableYCQL: boolean | undefined,
  providerCode: string,
  enableCP: boolean | undefined,
  isEditMode = false
): string[] => {
  const identityT = ((key: string) => key) as TFunction;
  return getAccessiblePorts(
    enableYSQL,
    enableYCQL,
    providerCode,
    enableCP,
    identityT,
    isEditMode
  ).flatMap((pg) => pg.PORTS_LIST.map((port: { id: string }) => port.id));
};
