import { CloudType, Region } from '@app/redesign/helpers/dtos';
import { filter, isEmpty, keys, some } from 'lodash';
import { useContext } from 'react';
import { TFunction } from 'i18next';
import {
  ClusterGFlags,
  ClusterPartitionSpec,
  ClusterPlacementSpec,
  ClusterSpec,
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo,
  NodeProxyConfig,
  PlacementRegion,
  Universe
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  AWS_CLOUD_OPTION,
  AZURE_CLOUD_OPTION,
  GCP_CLOUD_OPTION,
  K8S_CLOUD_OPTION,
  ON_PREM_CLOUD_OPTION
} from '@yugabyte-ui-library/core';
import { EditUniverseContext } from './EditUniverseContext';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../create-universe/steps/resilence-regions/dtos';
import { GFlag } from '../create-universe/steps/database-settings/dtos';
import { ProxyAdvancedProps } from '../create-universe/steps/advanced-settings/dtos';

export const getClusterByType = (universeData: Universe, type: ClusterSpecClusterType) => {
  return universeData.spec?.clusters.find((cluster) => cluster.cluster_type === type);
};

export const getProviderIcon = (providerCode?: string) => {
  if (!providerCode) return null;

  switch (providerCode) {
    case CloudType.aws:
      return AWS_CLOUD_OPTION.icon;
    case CloudType.gcp:
      return GCP_CLOUD_OPTION.icon;
    case CloudType.azu:
      return AZURE_CLOUD_OPTION.icon;
    case CloudType.onprem:
      return ON_PREM_CLOUD_OPTION.icon;
    case CloudType.kubernetes:
      return K8S_CLOUD_OPTION.icon;
    default:
      return null;
  }
};

export function useEditUniverseContext() {
  const context = useContext(EditUniverseContext);
  if (!context) {
    throw new Error('useEditUniverseContext must be used within an EditUniverseProvider');
  }
  return context;
}

export const countRegionsAzsAndNodes = (placementSpec: ClusterPlacementSpec) => {
  let totalRegions = 0;
  let totalAzs = 0;
  let totalNodes = 0;

  const regions = placementSpec.cloud_list.map((cloud) => cloud.region_list).flat() ?? [];
  totalRegions += regions.length;
  regions.forEach((region) => {
    const azs = region?.az_list ?? [];
    totalAzs += azs.length;
    azs.forEach((az) => {
      const numNodes = az?.num_nodes_in_az ?? 0;
      totalNodes += numNodes;
    });
  });
  return { totalRegions, totalAzs, totalNodes };
};

export const getResilientType = (
  primaryRegionStats: ReturnType<typeof countRegionsAzsAndNodes>,
  t: TFunction
) => {
  if (primaryRegionStats.totalRegions > 1) {
    return t('regionResilient', {
      count: primaryRegionStats.totalRegions - 1,
      keyPrefix: 'editUniverse.general'
    });
  } else if (primaryRegionStats.totalAzs > 1) {
    return t('azResilient', {
      count: primaryRegionStats.totalAzs - 1,
      keyPrefix: 'editUniverse.general'
    });
  } else if (primaryRegionStats.totalNodes > 1) {
    return t('nodeResilient', {
      count: primaryRegionStats.totalNodes - 1,
      keyPrefix: 'editUniverse.general'
    });
  } else {
    return t('notResilient', {
      keyPrefix: 'editUniverse.general'
    });
  }
};

export const mapUniversePayloadToResilienceAndRegionsProps = (
  providerRegions: Region[],
  stats: ReturnType<typeof countRegionsAzsAndNodes>,
  cluster: ClusterSpec | ClusterPartitionSpec
): ResilienceAndRegionsProps => {
  const regionAndResilience: ResilienceAndRegionsProps = {
    singleAvailabilityZone: '',
    faultToleranceType:
      stats.totalRegions > 1
        ? FaultToleranceType.REGION_LEVEL
        : stats.totalAzs > 1
        ? FaultToleranceType.AZ_LEVEL
        : stats.totalNodes > 1
        ? FaultToleranceType.NODE_LEVEL
        : FaultToleranceType.NONE,
    resilienceType: ResilienceType.REGULAR,
    replicationFactor: cluster.replication_factor ?? 1,
    resilienceFormMode: ResilienceFormMode.GUIDED,
    nodeCount: stats.totalNodes,
    regions: []
  };
  if ('provider_spec' in cluster && cluster.provider_spec?.region_list) {
    regionAndResilience['regions'] = cluster.placement_spec?.cloud_list.flatMap((cloud) =>
      cloud.region_list?.map((region) => {
        const matchedRegion = providerRegions.find((r) => r.uuid === region.uuid);
        return matchedRegion!;
      })
    ) as Region[];
  }
  return regionAndResilience;
};

export const convertGFlagApiRespToFormValues = (gflags: ClusterGFlags | undefined): GFlag[] => {
  const gFlagArray: GFlag[] = [];
  keys(gflags?.master).forEach((Gflag) => {
    gFlagArray.push({
      Name: Gflag,
      MASTER: gflags?.master?.[Gflag],
      TSERVER: gflags?.tserver?.[Gflag]
    });
  });
  return gFlagArray;
};

export const convertProxySettingsToFormValues = (
  proxyConfig: NodeProxyConfig
): ProxyAdvancedProps => {
  const secureWebProxy = proxyConfig.https_proxy?.split(':');
  const webProxy = proxyConfig.http_proxy?.split(':');
  return {
    secureWebProxy: !!proxyConfig.https_proxy,
    webProxy: !!proxyConfig.http_proxy,
    byPassProxyList: (proxyConfig.no_proxy_list?.length ?? 0) > 0,
    byPassProxyListValues: proxyConfig.no_proxy_list ?? [],
    enableProxyServer: proxyConfig.http_proxy || proxyConfig.https_proxy ? true : false,
    secureWebProxyPort: secureWebProxy?.[1] ? parseInt(secureWebProxy[1]) : undefined,
    secureWebProxyServer: secureWebProxy?.[0] ?? '',
    webProxyPort: webProxy?.[1] ? parseInt(webProxy[1]) : undefined,
    webProxyServer: webProxy?.[0] ?? ''
  };
};

export const hasDedicatedNodes = (universeData: Universe): boolean => {
  return some(universeData?.info?.node_details_set, (node) => node.dedicated_to);
};

export const countMasterAndTServerNodesByPlacementRegion = (
  universeData: Universe,
  placementRegion: PlacementRegion,
  render = true,
  t: TFunction
) => {
  const dedicatedNodesCount: Partial<Record<NodeDetailsDedicatedTo, number>> = {
    [NodeDetailsDedicatedTo.MASTER]: 0,
    [NodeDetailsDedicatedTo.TSERVER]: 0
  };

  placementRegion?.az_list?.forEach((az) => {
    const nodeDetailsaz = filter(universeData?.info?.node_details_set, { az_uuid: az.uuid });
    if (!isEmpty(nodeDetailsaz)) {
      nodeDetailsaz.forEach((node) => {
        node.dedicated_to === NodeDetailsDedicatedTo.MASTER
          ? (dedicatedNodesCount[NodeDetailsDedicatedTo.MASTER]! += 1)
          : (dedicatedNodesCount[NodeDetailsDedicatedTo.TSERVER]! += 1);
      });
    }
  });

  if (!render) return dedicatedNodesCount;

  return t('totalNodesTServerMaster', {
    total_nodes:
      dedicatedNodesCount[NodeDetailsDedicatedTo.TSERVER]! +
      dedicatedNodesCount[NodeDetailsDedicatedTo.MASTER]!,
    tservers: dedicatedNodesCount[NodeDetailsDedicatedTo.TSERVER] ?? 0,
    masters: dedicatedNodesCount[NodeDetailsDedicatedTo.MASTER] ?? 0,
    keyPrefix: 'editUniverse.placement'
  });
};

export const countMasterAndTServerNodes = (
  universeData: Universe,
  placement: ClusterPlacementSpec
) => {
  const dedicatedNodesCount: Partial<Record<NodeDetailsDedicatedTo, number>> = {
    [NodeDetailsDedicatedTo.MASTER]: 0,
    [NodeDetailsDedicatedTo.TSERVER]: 0
  };
  const azLists = placement?.cloud_list.flatMap((cloud) =>
    cloud!.region_list!.flatMap((region) => region.az_list)
  );

  azLists?.forEach((az) => {
    const nodeDetailsaz = filter(universeData?.info?.node_details_set, { az_uuid: az?.uuid });
    if (!isEmpty(nodeDetailsaz)) {
      nodeDetailsaz.forEach((node) => {
        node.dedicated_to === NodeDetailsDedicatedTo.MASTER
          ? (dedicatedNodesCount[NodeDetailsDedicatedTo.MASTER]! += 1)
          : (dedicatedNodesCount[NodeDetailsDedicatedTo.TSERVER]! += 1);
      });
    }
  });
  return dedicatedNodesCount;
};
