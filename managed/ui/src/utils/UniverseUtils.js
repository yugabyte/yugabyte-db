// Copyright (c) YugaByte, Inc.
import _ from 'lodash';

import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from './ObjectUtils';
import { PROVIDER_TYPES, IN_DEVELOPMENT_MODE } from '../config';
import { NodeState } from '../redesign/helpers/dtos';

export const nodeInClusterStates = [
  NodeState.Live,
  NodeState.Stopping,
  NodeState.Stopped,
  NodeState.Starting,
  NodeState.Unreachable,
  NodeState.MetricsUnavailable
];

export function isNodeRemovable(nodeState) {
  return nodeState === 'To Be Added';
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
    const foundClusters = clusters.filter(
      (cluster) => cluster.clusterType.toLowerCase() === clusterType.toLowerCase()
    );
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
  if (
    isNonEmptyObject(cluster) &&
    isNonEmptyObject(cluster.placementInfo) &&
    isNonEmptyArray(cluster.placementInfo.cloudList)
  ) {
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

/**
 * @returns The number of nodes in "Live" or "Stopped" state.
 * Restricted to a particular cluster if provided.
 */
export const getUniverseNodeCount = (nodeDetailsSet, cluster = null) => {
  const nodes = nodeDetailsSet ?? [];
  return nodes.filter(
    (node) =>
      (cluster === null || node.placementUuid === cluster.uuid) &&
      _.includes(nodeInClusterStates, node.state)
  ).length;
};

export function getProviderMetadata(provider) {
  return PROVIDER_TYPES.find((providerType) => providerType.code === provider.code);
}

export function getClusterIndex(nodeDetails, clusters) {
  const cluster = clusters.find((cluster) => cluster.uuid === nodeDetails.placementUuid);
  if (!cluster) {
    // Move orphaned nodes to end of list
    return Number.MAX_SAFE_INTEGER;
  }
  return cluster.index;
}

export function nodeComparisonFunction(nodeDetailsA, nodeDetailsB, clusters) {
  const aClusterIndex = getClusterIndex(nodeDetailsA, clusters);
  const bClusterIndex = getClusterIndex(nodeDetailsB, clusters);
  if (aClusterIndex !== bClusterIndex) {
    return aClusterIndex - bClusterIndex;
  }
  return nodeDetailsA.nodeIdx - nodeDetailsB.nodeIdx;
}

export function hasLiveNodes(universe) {
  if (isNonEmptyObject(universe) && isNonEmptyObject(universe.universeDetails)) {
    const {
      universeDetails: { nodeDetailsSet }
    } = universe;
    if (isNonEmptyArray(nodeDetailsSet)) {
      return nodeDetailsSet.some((nodeDetail) => nodeDetail.state === 'Live');
    }
  }
  return false;
}

export function isKubernetesUniverse(currentUniverse) {
  return (
    isDefinedNotNull(currentUniverse.universeDetails) &&
    isDefinedNotNull(getPrimaryCluster(currentUniverse.universeDetails.clusters)) &&
    getPrimaryCluster(currentUniverse.universeDetails.clusters).userIntent.providerType ===
    'kubernetes'
  );
}

export const isYbcEnabledUniverse = (universeDetails) => {
  return universeDetails.enableYbc;
}

/**
 * Returns an array of unique regions in the universe
 */
export const getUniverseRegions = (clusters) => {
  const primaryCluster = getPrimaryCluster(clusters);
  const readOnlyCluster = getReadOnlyCluster(clusters);

  const universeRegions = getPlacementRegions(primaryCluster).concat(
    getPlacementRegions(readOnlyCluster)
  );
  return _.uniqBy(universeRegions, 'uuid');
};

export const isUniverseType = (universe, type) => {
  const cluster = getPrimaryCluster(universe?.universeDetails?.clusters);
  return cluster?.userIntent?.providerType === type;
};

export const isOnpremUniverse = (universe) => {
  return isUniverseType(universe, 'onprem');
};

export const isPausableUniverse = (universe) => {
  return isUniverseType(universe, 'aws') || isUniverseType(universe, 'gcp') || isUniverseType(universe, 'azu');
};

// Reads file and passes content into Promise.resolve
export const readUploadedFile = (inputFile, isRequired) => {
  const fileReader = new FileReader();
  return new Promise((resolve, reject) => {
    fileReader.onloadend = () => {
      resolve(fileReader.result);
    };
    // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
    if (isDefinedNotNull(inputFile)) {
      fileReader.readAsText(inputFile);
    }
    if (!isRequired && !isDefinedNotNull(inputFile)) {
      resolve(null);
    }
  });
};

export const getProxyNodeAddress = (universeUUID, customer, nodeIp, nodePort) => {
  let href = '';
  if (IN_DEVELOPMENT_MODE || !!customer.INSECURE_apiToken) {
    href = `http://${nodeIp}:${nodePort}`;
  } else {
    href = `/universes/${universeUUID}/proxy/${nodeIp}:${nodePort}/`;
  }
  return href;
};
