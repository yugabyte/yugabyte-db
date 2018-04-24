// Copyright (c) YugaByte, Inc.

import { isNonEmptyArray, isNonEmptyObject } from "./ObjectUtils";

export function isNodeRemovable(nodeState) {
  return nodeState === "To Be Added";
}

// Given a list of cluster objects, return the Primary cluster. If clusters is malformed or if no
// primary cluster found or multiple are found, return null.
export function getPrimaryCluster(clusters) {
  if (isNonEmptyArray(clusters)) {
    const foundClusters = clusters.filter((cluster) => cluster.clusterType === 'PRIMARY');
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
}

export function getReadOnlyCluster(clusters) {
  if (isNonEmptyArray(clusters)) {
    const foundClusters = clusters.filter((cluster) => cluster.clusterType === 'ASYNC');
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
}

export function getClusterByType(clusters, clusterType) {
  if (isNonEmptyArray(clusters)) {
    const foundClusters = clusters.filter((cluster) => cluster.clusterType.toLowerCase() === clusterType.toLowerCase());
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
}

export function getPlacementRegions(cluster) {
  const placementCloud = getPlacementCloud(cluster);
  if (isNonEmptyObject(placementCloud)) {
    return placementCloud.regionList;
  }
  return [];
}

export function getPlacementCloud(cluster) {
  if (isNonEmptyObject(cluster) &&
      isNonEmptyObject(cluster.placementInfo) &&
      isNonEmptyArray(cluster.placementInfo.cloudList)) {
    return cluster.placementInfo.cloudList[0];
  }
  return null;
}
