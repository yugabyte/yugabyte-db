import { ClusterSpec, ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

/**
 * Given a list of cluster specifications, return the primary cluster specification.
 *
 * This function returns the primary cluster specification, or null if no primary cluster specification is found.
 */
export const getPrimaryCluster = (clusters: ClusterSpec[]) => {
  if (clusters && clusters.length > 0) {
    const foundClusters = clusters.filter(
      (cluster) => cluster.cluster_type === ClusterSpecClusterType.PRIMARY
    );
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
};

/**
 * Although there can technically be multiple read-only clusters, YBA only officially supports
 * one read-only cluster for now.
 *
 * Given a list of cluster specifications, return the read-only cluster specification.
 *
 * This function returns the read-only cluster specification, or null if no read-only cluster specification is found.
 */
export const getReadOnlyCluster = (clusters: ClusterSpec[]) => {
  if (clusters && clusters.length > 0) {
    const foundClusters = clusters.filter(
      (cluster) => cluster.cluster_type === ClusterSpecClusterType.ASYNC
    );
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
};

/**
 * Given a cluster object, return the placement regions.
 *
 * Returns an empty array if no placement regions are found.
 */
export const getClusterPlacementRegions = (cluster: ClusterSpec | null) => {
  if (!cluster) {
    return [];
  }
  const fromPlacement =
    cluster.placement_spec?.cloud_list?.flatMap((cloud) => cloud.region_list ?? []) ?? [];
  if (fromPlacement.length) {
    return fromPlacement;
  }
  const defaultPartition =
    cluster.partitions_spec?.find((p) => p.default_partition) ?? cluster.partitions_spec?.[0];
  const partitionClouds = defaultPartition?.placement?.cloud_list ?? [];
  return partitionClouds.flatMap((cloud) => cloud.region_list ?? []);
};
