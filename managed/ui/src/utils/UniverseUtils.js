// Copyright (c) YugaByte, Inc.

import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from "./ObjectUtils";
import { PROVIDER_TYPES } from "../config";

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

export function getClusterProviderUUIDs(clusters) {
  const providers = [];
  if (isNonEmptyArray(clusters)) {
    const primaryCluster = getPrimaryCluster(clusters);
    const readOnlyCluster = getReadOnlyCluster(clusters);
    if (isNonEmptyObject(primaryCluster)) {
      providers.push(primaryCluster.userIntent.provider);
    }
    if (isNonEmptyObject(readOnlyCluster)) {
      providers.push(readOnlyCluster.userIntent.provider);
    }
  }
  return providers;
}

export function getUniverseNodes(clusters) {
  const primaryCluster = getPrimaryCluster(clusters);
  const readOnlyCluster = getReadOnlyCluster(clusters);
  let numNodes = 0;
  if (isNonEmptyObject(primaryCluster) && isNonEmptyObject(primaryCluster.userIntent) &&
      isDefinedNotNull(primaryCluster.userIntent.numNodes)) {
    numNodes += primaryCluster.userIntent.numNodes;
  }
  if (isNonEmptyObject(readOnlyCluster) && isNonEmptyObject(readOnlyCluster.userIntent) &&
      isDefinedNotNull(readOnlyCluster.userIntent.numNodes)) {
    numNodes += readOnlyCluster.userIntent.numNodes;
  }

  return numNodes;
}

export function getProviderMetadata(provider) {
  return PROVIDER_TYPES.find((providerType) => providerType.code === provider.code);
}
