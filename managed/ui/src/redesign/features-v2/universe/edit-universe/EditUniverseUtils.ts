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
import { getInferredOutageCount, inferResilience } from '../create-universe/CreateUniverseUtils';
import { NodeAvailabilityProps } from '../create-universe/steps/nodes-availability/dtos';
import { REPLICATION_FACTOR } from '../create-universe/fields/FieldNames';

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

/** Maps API placement to nodes-availability form `availabilityZones` (by region code). */
export const placementSpecToAvailabilityZones = (
  placement: ClusterPlacementSpec
): NodeAvailabilityProps['availabilityZones'] =>
  placement.cloud_list.reduce<NodeAvailabilityProps['availabilityZones']>((acc, cloud) => {
    cloud.region_list?.forEach((region) => {
      const regionKey = region.code ?? region.uuid ?? '';
      if (!regionKey) return;
      acc[regionKey] = (region.az_list ?? []).map((az, index) => ({
        name: az.name ?? '',
        uuid: az.uuid ?? `${regionKey}-${index}`,
        nodeCount: az.num_nodes_in_az ?? 0,
        preffered: (az.leader_preference ?? 0) - 1
      }));
    });
    return acc;
  }, {});

/** Defaults for master-allocation / nodes step from cluster `node_spec` and a placement slice. */
export const getNodeAvailabilityDefaultsFromClusterPlacement = (
  cluster: ClusterSpec,
  placement: ClusterPlacementSpec,
  replicationFactorOverride?: number
): NodeAvailabilityProps => ({
  availabilityZones: placementSpecToAvailabilityZones(placement),
  useDedicatedNodes: !!cluster.node_spec?.dedicated_nodes,
  [REPLICATION_FACTOR]: replicationFactorOverride ?? cluster.replication_factor ?? 1
});

const noopT: TFunction = ((key: string) => key) as TFunction;

/**
 * Cluster-level T-Server / master counts. Sums the per-region values so the top-level total
 * stays consistent with the per-region rows shown in `ClusterInstanceCard`. For geo partitions
 * that don't host masters, this correctly yields 0 instead of falling back to the cluster RF.
 */
export const getDedicatedTserverMasterDisplayCounts = (
  universeData: Universe,
  cluster: ClusterSpec,
  placement: ClusterPlacementSpec
): { tserver: number; master: number } => {
  const placementRegions = placement.cloud_list.flatMap((cloud) => cloud.region_list ?? []);
  return placementRegions.reduce(
    (acc, region) => {
      const counts = getDedicatedCountsForPlacementRegion(universeData, region, cluster);
      return {
        tserver: acc.tserver + counts.tserver,
        master: acc.master + counts.master
      };
    },
    { tserver: 0, master: 0 }
  );
};

/** Per-region counts; when `dedicated_nodes`, T-Server total uses placement AZ sums if node_details lack T-Servers. */
export const getDedicatedCountsForPlacementRegion = (
  universeData: Universe,
  placementRegion: PlacementRegion,
  cluster: ClusterSpec
): { tserver: number; master: number } => {
  const fromDetails = countMasterAndTServerNodesByPlacementRegion(
    universeData,
    placementRegion,
    false,
    noopT
  ) as Partial<Record<NodeDetailsDedicatedTo, number>>;
  const tFromDetails = fromDetails[NodeDetailsDedicatedTo.TSERVER] ?? 0;
  const mFromDetails = fromDetails[NodeDetailsDedicatedTo.MASTER] ?? 0;
  if (!cluster.node_spec?.dedicated_nodes) {
    return { tserver: tFromDetails, master: mFromDetails };
  }
  const tFromPlacement =
    placementRegion.az_list?.reduce((s, az) => s + (az.num_nodes_in_az ?? 0), 0) ?? 0;
  const tserver = tFromDetails > 0 ? tFromDetails : tFromPlacement;
  return { tserver, master: mFromDetails };
};

/** Master nodes in a single AZ from universe node details. */
export const countMasterNodesInAz = (universeData: Universe, azUuid: string | undefined) => {
  if (!azUuid) return 0;
  return filter(universeData?.info?.node_details_set, { az_uuid: azUuid }).filter(
    (node) => node.dedicated_to === NodeDetailsDedicatedTo.MASTER
  ).length;
};

export const getResilientType = (
  placementSpec: ClusterPlacementSpec,
  replicationFactor: number | undefined,
  t: TFunction
) => {
  const availabilityZones = placementSpecToAvailabilityZones(placementSpec);
  const placementRegions = placementSpec.cloud_list.flatMap((cloud) => cloud.region_list ?? []);
  const resilienceRegions = Array.from({ length: placementRegions.length }, () => ({} as Region));
  const effectiveReplicationFactor = replicationFactor ?? 1;
  const inferredResilience = inferResilience(
    {
      regions: resilienceRegions,
      resilienceType: ResilienceType.REGULAR,
      resilienceFormMode: ResilienceFormMode.GUIDED,
      faultToleranceType: FaultToleranceType.NONE,
      resilienceFactor: 1,
      nodeCount: 0,
      singleAvailabilityZone: ''
    },
    {
      availabilityZones,
      useDedicatedNodes: false,
      replicationFactor: effectiveReplicationFactor
    }
  );

  if (!inferredResilience || inferredResilience === FaultToleranceType.NONE) {
    return t('notResilient', {
      keyPrefix: 'editUniverse.general'
    });
  }

  const outageCount = getInferredOutageCount(
    inferredResilience,
    effectiveReplicationFactor,
    availabilityZones
  );

  if (outageCount <= 0) {
    return t('notResilient', {
      keyPrefix: 'editUniverse.general'
    });
  }

  const key =
    inferredResilience === FaultToleranceType.REGION_LEVEL
      ? 'regionResilient'
      : inferredResilience === FaultToleranceType.AZ_LEVEL
      ? 'azResilient'
      : 'nodeResilient';

  return t(key, {
    count: outageCount,
    keyPrefix: 'editUniverse.general'
  });
};

const getPlacementSpecForCluster = (
  cluster: ClusterSpec | ClusterPartitionSpec
): ClusterPlacementSpec | undefined => {
  if ('placement_spec' in cluster && cluster.placement_spec) {
    return cluster.placement_spec;
  }
  if ('placement' in cluster && cluster.placement) {
    return cluster.placement;
  }
  return undefined;
};

const matchProviderRegionForPlacement = (
  providerRegions: Region[],
  placementRegion: PlacementRegion
): Region | undefined => {
  const byUuid = providerRegions.find((r) => r.uuid === placementRegion.uuid);
  if (byUuid) return byUuid;
  const byCode = providerRegions.filter((r) => r.code === placementRegion.code);
  if (byCode.length === 1) return byCode[0];
  return undefined;
};

/** Guided edit defaults: inverts getGuidedNodesStepReplicationFactor for non-NONE FT. */
const guidedResilienceFactorFromReplication = (
  replicationFactor: number | undefined,
  faultToleranceType: FaultToleranceType
): number => {
  const rf = replicationFactor ?? 1;
  if (faultToleranceType === FaultToleranceType.NONE) {
    return rf;
  }
  return Math.max(0, Math.floor((rf - 1) / 2));
};

export const mapUniversePayloadToResilienceAndRegionsProps = (
  providerRegions: Region[],
  stats: ReturnType<typeof countRegionsAzsAndNodes>,
  cluster: ClusterSpec | ClusterPartitionSpec
): ResilienceAndRegionsProps => {
  const placement = getPlacementSpecForCluster(cluster);
  const regionsFromPlacement =
    placement?.cloud_list?.flatMap((cloud) =>
      (cloud.region_list ?? [])
        .map((placementRegion) =>
          matchProviderRegionForPlacement(providerRegions, placementRegion)
        )
        .filter((r): r is Region => r !== undefined)
    ) ?? [];

  const replicationFactor = cluster.replication_factor ?? 1;
  const faultToleranceType =
    replicationFactor <= 1
      ? FaultToleranceType.NONE
      : stats.totalRegions === replicationFactor
      ? FaultToleranceType.REGION_LEVEL
      : stats.totalRegions < replicationFactor && stats.totalAzs === replicationFactor
      ? FaultToleranceType.AZ_LEVEL
      : stats.totalRegions < replicationFactor && stats.totalAzs < replicationFactor
      ? FaultToleranceType.NODE_LEVEL
      : FaultToleranceType.NONE;

  const regionAndResilience: ResilienceAndRegionsProps = {
    singleAvailabilityZone: '',
    faultToleranceType,
    resilienceType: ResilienceType.REGULAR,
    resilienceFactor: guidedResilienceFactorFromReplication(replicationFactor, faultToleranceType),
    resilienceFormMode: ResilienceFormMode.GUIDED,
    nodeCount: stats.totalNodes,
    regions: regionsFromPlacement
  };

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
