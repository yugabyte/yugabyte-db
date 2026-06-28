import { omit } from 'lodash';
import { TFunction } from 'i18next';
import { createUniverseFormProps } from '../CreateUniverseContext';
import { ResilienceType } from '../steps/resilence-regions/dtos';
import { OtherAdvancedProps } from '../steps/advanced-settings/dtos';
import {
  CommunicationPortsSpec,
  PlacementRegion,
  UniverseCreateReqBody,
  ClusterNetworkingSpecAllOfEnableExposingService,
  EncryptionInTransitSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ClusterType } from '../../../../helpers/dtos';
import { SecuritySettingsProps, CertType } from '../steps/security-settings/dtos';
import { getEffectiveReplicationFactorForResilience } from './resilienceReplication';
import { getNodeCount, getPlacementRegions } from './placementAndAvailability';
import { effectiveUseDedicatedNodes, getNodeSpec } from './createUniverseNodeSpec';

export const getCreateEITPayload = (
  securitySettings: SecuritySettingsProps,
  cloudType: CloudType
): EncryptionInTransitSpec => {
  const { enableNodeToNodeEncryption, enableClientToNodeEncryption, rootCertificate, certType } =
    securitySettings;
  if (cloudType === CloudType.kubernetes) {
    return {
      enable_node_to_node_encrypt: securitySettings?.enableNodeToNodeEncryption ? true : false,
      enable_client_to_node_encrypt: securitySettings?.enableClientToNodeEncryption ? true : false,
      root_ca: securitySettings?.enableNodeToNodeEncryption
        ? certType === CertType.CUSTOM
          ? securitySettings?.rootCertificate
          : ''
        : '',
      client_root_ca: securitySettings?.enableClientToNodeEncryption
        ? certType === CertType.CUSTOM
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
    if (gflag.MASTER) {
      gflagsMap.master[gflag.Name] = gflag.MASTER.toString();
    }
    if (gflag.TSERVER) {
      gflagsMap.tserver[gflag.Name] = gflag.TSERVER.toString();
    }
  });
  return gflagsMap;
};

function buildPrimaryCloudList(
  generalSettings: NonNullable<createUniverseFormProps['generalSettings']>,
  regionList: PlacementRegion[]
) {
  return [
    {
      code: generalSettings.cloud,
      uuid: generalSettings.providerConfiguration!.uuid!,
      default_region: regionList[0].uuid,
      region_list: regionList
    }
  ];
}

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

  const effectiveRf = getEffectiveReplicationFactorForResilience(
    resilienceAndRegionsSettings,
    nodesAvailabilitySettings
  );

  const regionList: PlacementRegion[] = getPlacementRegions(
    resilienceAndRegionsSettings,
    nodesAvailabilitySettings.availabilityZones,
    nodesAvailabilitySettings
  );

  const gflags = mapGFlags(databaseSettings.gFlags);
  const providerType = generalSettings?.providerConfiguration?.code;

  const primaryCloudList = buildPrimaryCloudList(generalSettings, regionList);

  const payload: UniverseCreateReqBody = {
    arch: instanceSettings.arch,
    spec: {
      name: generalSettings.universeName,
      yb_software_version: generalSettings.databaseVersion!,
      encryption_at_rest_spec: {
        kms_config_uuid: securitySettings.kmsConfig
      },
      encryption_in_transit_spec: getCreateEITPayload(securitySettings, providerType!),
      ycql: {
        ...omit(databaseSettings.ycql, 'confirm_pwd')
      },
      ysql: {
        ...omit(databaseSettings.ysql, 'confirm_pwd'),
        enable_connection_pooling: databaseSettings.enableConnectionPooling ?? false
      },
      networking_spec: {
        assign_public_ip: securitySettings.assignPublicIP,
        assign_static_public_ip: false,
        communication_ports: mapCommunicationPorts(otherAdvancedSettings),
        enable_ipv6: securitySettings.enableIPV6 ?? false
      },
      clusters: [
        {
          replication_factor: effectiveRf,
          cluster_type: ClusterType.PRIMARY,
          use_spot_instance: instanceSettings.useSpotInstance,
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
            instance_tags: otherAdvancedSettings?.instanceTags.reduce(
              (acc, tag) => {
                acc[tag.name] = tag.value;
                return acc;
              },
              {} as Record<string, string>
            )
          }),
          networking_spec: {
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
            cloud_list: primaryCloudList
          },
          partitions_spec: [
            {
              name: 'default',
              default_partition: true,
              replication_factor: effectiveRf,
              tablespace_name: 'default',
              placement: {
                cloud_list: primaryCloudList
              }
            }
          ],
          provider_spec: {
            provider: generalSettings.providerConfiguration!.uuid!,
            region_list: regionList.map((r) => r.uuid!),
            image_bundle_uuid: instanceSettings.imageBundleUUID!,
            access_key_code: otherAdvancedSettings.accessKeyCode,
            aws_instance_profile: otherAdvancedSettings.awsArnString
          }
        }
      ]
    }
  };

  return payload;
};

export const mapPortsKeys: any = () => {
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
    nodeExporterPort: 'node_exporter_port',
    ybControllerrRpcPort: 'yb_controller_rpc_port',
    ybControllerRpcPort: 'yb_controller_rpc_port'
  };
};

export const mapAPIPortValues = (communicationPorts: Partial<CommunicationPortsSpec>) => {
  let portsObj: any = {};
  Object.entries(communicationPorts).forEach(([key, val]) => {
    portsObj[`${mapPortsKeys[key]}`] = val;
  });
  return portsObj;
};

export const getAccessiblePorts = (
  enableYSQL: boolean | undefined,
  enableYCQL: boolean | undefined,
  providerCode: string,
  enableCP: boolean | undefined,
  t: TFunction
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
      disabled: false
    },
    {
      id: 'yqlServerRpcPort',
      visible: enableYCQL, //ycqlEnabled,
      disabled: false
    }
  ].filter((ports) => ports.visible);

  const YSQL_PORTS = [
    { id: 'ysqlServerHttpPort', visible: enableYSQL, disabled: false }, //visible: ysqlEnabled,
    {
      id: 'ysqlServerRpcPort',
      visible: enableYSQL,
      disabled: providerCode === CloudType.kubernetes
    },
    {
      id: 'internalYsqlServerRpcPort',
      visible: enableYSQL && enableCP,
      disabled: providerCode === CloudType.kubernetes
    }
  ].filter((ports) => ports.visible);

  const REDIS_PORTS = [
    { id: 'redisServerHttpPort', visible: false, disabled: false },
    { id: 'redisServerRpcPort', visible: false, disabled: false }
  ];

  const OTHER_PORTS = [
    { id: 'nodeExporterPort', visible: providerCode !== CloudType.onprem, disabled: false }, //visible: provider?.code !== CloudType.onprem,
    {
      id: 'ybControllerRpcPort',
      visible: true,
      disabled: false
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
