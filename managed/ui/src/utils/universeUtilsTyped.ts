import { isNonEmptyArray } from './ObjectUtils';

import { Cluster, ClusterType } from '../redesign/helpers/dtos';

/**
 * Given a list of cluster objects, return the Primary cluster.
 *
 * Returns `null` any of the following is true:
 * - primary cluster not found
 * - multiple primary clusters found
 */
export const getPrimaryCluster = (clusters: Cluster[]) => {
  if (isNonEmptyArray(clusters)) {
    const foundClusters = clusters.filter((cluster) => cluster.clusterType === ClusterType.PRIMARY);
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
};

/**
 * Given a list of cluster objects, return the read-only clusters.
 *
 * Returns `null` if no read-only clusters are found.
 */
export const getReadOnlyClusters = (clusters: Cluster[]) => {
  if (isNonEmptyArray(clusters)) {
    return clusters.filter((cluster) => cluster.clusterType === ClusterType.ASYNC);
  }
  return null;
};
