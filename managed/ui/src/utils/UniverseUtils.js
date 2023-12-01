// Copyright (c) YugaByte, Inc.
import _ from 'lodash';

import {
  isNonEmptyArray,
  isNonEmptyObject,
  isDefinedNotNull,
  isNonEmptyString,
  isEmptyString
} from './ObjectUtils';
import { PROVIDER_TYPES, BASE_URL } from '../config';
import { NodeState } from '../redesign/helpers/dtos';

export const MULTILINE_GFLAGS_ARRAY = ['ysql_hba_conf_csv', 'ysql_ident_conf_csv'];

const LDAP_KEYS = [
  'ldapserver',
  'ldapport',
  'ldapscheme',
  'ldaptls',
  'ldapprefix',
  'ldapsuffix',
  'ldapbasedn',
  'ldapbinddn',
  'ldapbindpasswd',
  'ldapsearchattribute',
  'ldapsearchfilter',
  'ldapurl'
];

export const CONST_VALUES = {
  LDAP: 'ldap',
  EMPTY_STRING: '',
  SPACE_SEPARATOR: ' ',
  DOUBLE_QUOTES_SEPARATOR: '"',
  DOUBLE_DOUBLE_QUOTES_SEPARATOR: '""',
  SINGLE_QUOTES_SEPARATOR: "'",
  COMMA_SEPARATOR: ',',
  EQUALS: '='
};

export const GFLAG_EDIT = 'EDIT';

export const nodeInClusterStates = [
  NodeState.Live,
  NodeState.Stopping,
  NodeState.Stopped,
  NodeState.Starting,
  NodeState.Unreachable,
  NodeState.MetricsUnavailable
];

export const MultilineGFlags = {
  YSQL_HBA_CONF_CSV: 'ysql_hba_conf_csv',
  YSQL_IDENT_CONF_CSV: 'ysql_ident_conf_csv'
};

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

export const getUniverseDedicatedNodeCount = (nodeDetailsSet, cluster = null) => {
  const nodes = nodeDetailsSet ?? [];
  const numTserverNodes = nodes.filter(
    (node) =>
      (cluster === null || node.placementUuid === cluster.uuid) &&
      _.includes(nodeInClusterStates, node.state) &&
      node.dedicatedTo === 'TSERVER'
  ).length;
  const numMasterNodes = nodes.filter(
    (node) =>
      (cluster === null || node.placementUuid === cluster.uuid) &&
      _.includes(nodeInClusterStates, node.state) &&
      node.dedicatedTo === 'MASTER'
  ).length;
  return {
    numTserverNodes,
    numMasterNodes
  };
};

export const isDedicatedNodePlacement = (currentUniverse) => {
  let isDedicatedNodes = false;
  if (!currentUniverse?.universeDetails) return isDedicatedNodes;

  const clusters = currentUniverse.universeDetails.clusters;
  const primaryCluster = clusters && getPrimaryCluster(clusters);
  isDedicatedNodes = primaryCluster.userIntent.dedicatedNodes;
  return isDedicatedNodes;
};

export function getProviderMetadata(provider) {
  return PROVIDER_TYPES.find((providerType) => providerType.code === provider.code);
}

export function getClusterIndex(nodeDetails, clusters) {
  const universeCluster = clusters.find((cluster) => cluster.uuid === nodeDetails.placementUuid);
  if (!universeCluster) {
    // Move orphaned nodes to end of list
    return Number.MAX_SAFE_INTEGER;
  }
  return universeCluster.index;
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
  return universeDetails?.enableYbc;
};

export const isYbcInstalledInUniverse = (universeDetails) => {
  return universeDetails?.ybcInstalled;
};

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
  return (
    isUniverseType(universe, 'aws') ||
    isUniverseType(universe, 'gcp') ||
    isUniverseType(universe, 'azu')
  );
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

export const getProxyNodeAddress = (universeUUID, nodeIp, nodePort) => {
  return `${BASE_URL}/universes/${universeUUID}/proxy/${nodeIp}:${nodePort}/`;
};

export const isLDAPKeywordExist = (GFlagInput) => {
  return GFlagInput.includes(CONST_VALUES.LDAP);
};

/**
 * Unformat LDAP Configuration string to ensure they are split into rows accordingly in EDIT mode
 *
 * @param GFlagInput The entire Gflag configuration enetered that needs to be unformatted
 */
export const unformatLDAPConf = (GFlagInput) => {
  // Regex expression to extract non-quoted comma
  const filteredGFlagInput = GFlagInput.split(/,(?=(?:(?:[^"]*"){2})*[^"]*$)/);

  const unformattedConf = filteredGFlagInput?.map((GFlagRowConf, index) => {
    const hasInputStartQuotes =
      GFlagRowConf.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) ||
      GFlagRowConf.startsWith(CONST_VALUES.SINGLE_QUOTES_SEPARATOR);
    const hasInputEndQuotes =
      GFlagRowConf.endsWith(CONST_VALUES.SINGLE_QUOTES_SEPARATOR) ||
      GFlagRowConf.endsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR);
    let GFlagRowConfSubset = '';

    if (hasInputStartQuotes || hasInputEndQuotes) {
      GFlagRowConfSubset = GFlagRowConf.substring(
        hasInputStartQuotes ? 1 : 0,
        hasInputEndQuotes
          ? GFlagRowConf[0].startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
            GFlagRowConf[GFlagRowConf.length - 2].startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR)
            ? GFlagRowConf.length - 2
            : GFlagRowConf.length - 1
          : GFlagRowConf.length
      );
    }
    return {
      id: `item-${index}`,
      index: index,
      content: isNonEmptyString(GFlagRowConfSubset) ? GFlagRowConfSubset : GFlagRowConf,
      error: false
    };
  });

  return unformattedConf;
};

/**
  * Format LDAP Configuration string based on rules here: 
  * https://docs.yugabyte.com/preview/reference/configuration/yb-tserver/#ysql-hba-conf-csv
  *
  * @param GFlagInput Input entered in the text field

*/
export const formatLDAPConf = (GFlagInput) => {
  const LDAPKeywordLength = CONST_VALUES.LDAP.length;
  const isLDAPExist = isLDAPKeywordExist(GFlagInput);

  if (isLDAPExist) {
    const LDAPKeywordIndex = GFlagInput.indexOf(CONST_VALUES.LDAP);
    const initialLDAPConf = GFlagInput.substring(0, LDAPKeywordIndex + 1 + LDAPKeywordLength);
    // Get the substring of entire configuration which has LDAP attributes
    const LDAPRowWithAttributes = GFlagInput?.substring(
      LDAPKeywordIndex + 1 + LDAPKeywordLength,
      GFlagInput.length
    );

    // Extract LDAP Attributes key/value pair
    const LDAPAttributes = LDAPRowWithAttributes?.match(/"([^"]+)"|""([^""]+)""|[^" ]+/g);
    const appendedLDAPConf = LDAPAttributes?.reduce((accumulator, attribute, index) => {
      if (
        attribute.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        !attribute.startsWith(CONST_VALUES.DOUBLE_DOUBLE_QUOTES_SEPARATOR)
      ) {
        accumulator =
          accumulator +
          CONST_VALUES.DOUBLE_QUOTES_SEPARATOR +
          attribute +
          CONST_VALUES.DOUBLE_QUOTES_SEPARATOR +
          (index === LDAPAttributes.length - 1
            ? CONST_VALUES.EMPTY_STRING
            : CONST_VALUES.SPACE_SEPARATOR);
      } else if (attribute.startsWith(CONST_VALUES.DOUBLE_DOUBLE_QUOTES_SEPARATOR)) {
        accumulator =
          accumulator +
          attribute +
          (index === LDAPAttributes.length - 1
            ? CONST_VALUES.EMPTY_STRING
            : CONST_VALUES.SPACE_SEPARATOR);
      } else {
        accumulator =
          accumulator +
          attribute +
          (attribute.endsWith(CONST_VALUES.EQUALS)
            ? CONST_VALUES.EMPTY_STRING
            : index === LDAPAttributes.length - 1
            ? CONST_VALUES.EMPTY_STRING
            : CONST_VALUES.SPACE_SEPARATOR);
      }
      return accumulator;
    }, CONST_VALUES.EMPTY_STRING);

    return initialLDAPConf + appendedLDAPConf;
  }
  return GFlagInput;
};

/**
  * verifyLDAPAttributes checks for certain validation rules and return a boolean value.
  * to indicate if GFlag Conf does not meet the criteria
  *
  * @param GFlagInput Input entered in the text field

*/
export const verifyLDAPAttributes = (GFlagInput) => {
  let isAttributeInvalid = false;
  let isWarning = false;
  let errorMessage = '';

  const numNonASCII = GFlagInput.match(/[^\x20-\x7F]+/);
  if (numNonASCII && numNonASCII?.length > 0) {
    isAttributeInvalid = false;
    isWarning = true;
    errorMessage = 'universeForm.gFlags.nonASCIIDetected';
  }

  const LDAPKeywordLength = CONST_VALUES.LDAP.length;
  const isLDAPExist = isLDAPKeywordExist(GFlagInput);

  if (isLDAPExist) {
    const LDAPIndex = GFlagInput.indexOf(CONST_VALUES.LDAP);
    const LDAPConf = GFlagInput?.substring(LDAPIndex + 1 + LDAPKeywordLength, GFlagInput.length);
    const LDAPAttributes = LDAPConf?.split(CONST_VALUES.SPACE_SEPARATOR);
    const LDAPRowWithAttributes = LDAPAttributes?.filter((attribute) =>
      attribute.startsWith(CONST_VALUES.LDAP)
    );

    for (let index = 0; index < LDAPRowWithAttributes?.length; index++) {
      const [ldapKey, ...ldapValues] = LDAPRowWithAttributes[index]?.split(CONST_VALUES.EQUALS);
      const ldapValue = ldapValues.join(CONST_VALUES.EQUALS);

      if (!LDAP_KEYS.includes(ldapKey)) {
        isAttributeInvalid = true;
        isWarning = false;
        errorMessage = 'universeForm.gFlags.InvalidLDAPKey';
        break;
      }

      if (isEmptyString(ldapValue)) {
        isAttributeInvalid = true;
        isWarning = false;
        errorMessage = 'universeForm.gFlags.LDAPMissingAttributeValue';
        break;
      }

      const hasNoEndQuote =
        ldapValue.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        !ldapValue.endsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR);

      const hasNoStartQuote =
        !ldapValue.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        ldapValue.endsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR);

      // If a LDAP attribute starts with double quotes and does not end with it, raise a validation error
      if (hasNoEndQuote || hasNoStartQuote) {
        isAttributeInvalid = true;
        isWarning = false;
        errorMessage = 'universeForm.gFlags.LDAPMissingQuote';
        break;
      }
    }
  }
  return { isAttributeInvalid, errorMessage, isWarning };
};

export const optimizeVersion = (version) => {
  if (parseInt(version[version.length - 1], 10) === 0) {
    return optimizeVersion(version.slice(0, version.length - 1));
  } else {
    return version.join('.');
  }
};
