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

const JWT_KEYS = ['map', 'jwt_audiences', 'jwt_issuers', 'jwt_matching_claim_key', 'jwks'];

export const CONST_VALUES = {
  JWT: 'jwt',
  LDAP: 'ldap',
  EMPTY_STRING: '',
  SPACE_SEPARATOR: ' ',
  DOUBLE_QUOTES_SEPARATOR: '"',
  DOUBLE_DOUBLE_QUOTES_SEPARATOR: '""',
  SINGLE_QUOTES_SEPARATOR: "'",
  COMMA_SEPARATOR: ',',
  EQUALS: '=',
  JWKS: 'jwks'
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
      node.isTserver
  ).length;
  const numMasterNodes = nodes.filter(
    (node) =>
      (cluster === null || node.placementUuid === cluster.uuid) &&
      _.includes(nodeInClusterStates, node.state) &&
      node.isMaster
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

export const isAsymmetricCluster = (cluster) =>
  isNonEmptyObject(cluster.userIntent.specificGFlags?.perAZ) ||
  (isNonEmptyObject(cluster.userIntent.userIntentOverrides?.azOverrides) &&
    hasAsymmetricOverrides(cluster.userIntent.userIntentOverrides.azOverrides));

const hasAsymmetricOverrides = (azOverrides) =>
  Object.values(azOverrides).some(
    (azOverride) => Object.keys(azOverride).length > 1 || azOverride.proxyConfig === undefined
  );

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

/**
 * Unformat Configuration string to ensure they are split into rows accordingly in EDIT mode
 *
 * @param GFlagInput The entire Gflag configuration enetered that needs to be unformatted
 */
export const unformatConf = (GFlagInput) => {
  // Regex expression to extract non-quoted comma
  const filteredGFlagInput = GFlagInput.split(/,(?=(?:(?:[^"]*"){2})*[^"]*$)/);
  const unformattedConf = filteredGFlagInput?.map((GFlagRowConf, index) => {
    let JWKSToken = '';
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

    // Extract jwks content from the row input if it exists
    if (GFlagRowConfSubset.includes(CONST_VALUES.JWKS)) {
      const JWKSKey = GFlagRowConfSubset.substring(GFlagRowConfSubset.indexOf(CONST_VALUES.JWKS));
      if (isNonEmptyString(JWKSKey)) {
        GFlagRowConfSubset = GFlagRowConfSubset.replace(JWKSKey, '');
        GFlagRowConfSubset = GFlagRowConfSubset.trimEnd();
      }
      JWKSToken = JWKSKey.substring(JWKSKey.indexOf(CONST_VALUES.EQUALS) + 1);
    }

    return {
      id: `item-${index}`,
      index: index,
      content: isNonEmptyString(GFlagRowConfSubset) ? GFlagRowConfSubset : GFlagRowConf,
      error: false,
      showJWKSButton: isNonEmptyString(JWKSToken),
      JWKSToken: JWKSToken
    };
  });

  return unformattedConf;
};

/**
  * Format Configuration string based on rules here: 
  * https://docs.yugabyte.com/preview/reference/configuration/yb-tserver/#ysql-hba-conf-csv
  *
  * @param GFlagInput Input entered in the text field

*/
export const formatConf = (GFlagInput, searchTerm, JWKSToken) => {
  const keywordLength = searchTerm.length;
  const isKeywordExist = GFlagInput.includes(searchTerm);

  if (isKeywordExist) {
    const keywordIndex = GFlagInput.indexOf(searchTerm);
    const initialLDAPConf = GFlagInput.substring(0, keywordIndex + 1 + keywordLength);
    // Get the substring of entire configuration which has LDAP or JWT attributes
    const keywordConf = GFlagInput?.substring(keywordIndex + 1 + keywordLength, GFlagInput.length);

    // Extract LDAP or JWT Attributes key/value pair
    const attributes = keywordConf?.match(/"([^"]+)"|""([^""]+)""|[^" ]+/g);
    let JWKS = '';
    const appendedLDAPConf = attributes?.reduce((accumulator, attribute, index) => {
      if (
        attribute.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        !attribute.startsWith(CONST_VALUES.DOUBLE_DOUBLE_QUOTES_SEPARATOR)
      ) {
        accumulator =
          accumulator +
          CONST_VALUES.DOUBLE_QUOTES_SEPARATOR +
          attribute +
          CONST_VALUES.DOUBLE_QUOTES_SEPARATOR +
          (index === attributes.length - 1
            ? CONST_VALUES.EMPTY_STRING
            : CONST_VALUES.SPACE_SEPARATOR);
      } else if (attribute.startsWith(CONST_VALUES.DOUBLE_DOUBLE_QUOTES_SEPARATOR)) {
        accumulator =
          accumulator +
          attribute +
          (index === attributes.length - 1
            ? CONST_VALUES.EMPTY_STRING
            : CONST_VALUES.SPACE_SEPARATOR);
      } else {
        accumulator =
          accumulator +
          attribute +
          (attribute.endsWith(CONST_VALUES.EQUALS)
            ? CONST_VALUES.EMPTY_STRING
            : index === attributes.length - 1
            ? CONST_VALUES.EMPTY_STRING
            : CONST_VALUES.SPACE_SEPARATOR);
      }
      return accumulator;
    }, CONST_VALUES.EMPTY_STRING);

    if (searchTerm === CONST_VALUES.JWT && isNonEmptyString(JWKSToken)) {
      JWKS = ` jwks=${JWKSToken}`;
    }

    return initialLDAPConf + appendedLDAPConf + JWKS;
  }
  return GFlagInput;
};

/**
  * verifyAttributes checks for certain validation rules and return a boolean value.
  * to indicate if GFlag Conf does not meet the criteria
  *
  * @param GFlagInput Input entered in the text field

*/
export const verifyAttributes = (GFlagInput, searchTerm, JWKSKeyset, isOIDCSupported) => {
  let isAttributeInvalid = false;
  let isWarning = false;
  let errorMessageKey = '';

  // If string contains any non-ASCII character
  const numNonASCII = GFlagInput.match(/[^\x20-\x7F]+/);
  const isNonASCII = numNonASCII && numNonASCII?.length > 0;
  if (isNonASCII) {
    isAttributeInvalid = false;
    isWarning = true;
    errorMessageKey = 'universeForm.gFlags.nonASCIIDetected';
  }

  if (!searchTerm || isNonASCII) {
    return { isAttributeInvalid, errorMessageKey, isWarning };
  }

  // Raise error when there is jwt keyword but is no JWKS keyset associated with it
  if (searchTerm === CONST_VALUES.JWT && (isEmptyString(JWKSKeyset) || !JWKSKeyset)) {
    isAttributeInvalid = true;
    isWarning = false;
    errorMessageKey = isOIDCSupported
      ? 'universeForm.gFlags.uploadKeyset'
      : 'universeForm.gFlags.jwksNotSupported';
    return { isAttributeInvalid, errorMessageKey, isWarning };
  }

  const keywordLength = searchTerm.length;
  const isKeywordExist = GFlagInput.includes(searchTerm);

  // Search term can either be JWT or LDAP
  if (isKeywordExist) {
    const searchTermRegex = searchTerm === CONST_VALUES.LDAP ? /\sldap\s+/ : /\sjwt\s+/;
    const keywordList = GFlagInput.match(searchTermRegex);
    const keywordIndex = GFlagInput.indexOf(keywordList?.[0]);
    const keywordConf = GFlagInput?.substring(keywordIndex + 1 + keywordLength, GFlagInput.length);
    const attributes = keywordConf?.match(/(?:[^\s"|""]+|""[^"|""]*"|")+/g);

    for (let index = 0; index < attributes?.length; index++) {
      const [attributeKey, ...attributeValues] = attributes[index]?.split(CONST_VALUES.EQUALS);
      const attributeValue = attributeValues.join(CONST_VALUES.EQUALS);

      const hasNoAndStartQuote =
        !attributeValue.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        !attributeValue.endsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR);
      const hasNoEndQuote =
        attributeValue.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        !attributeValue.endsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR);

      const hasNoStartQuote =
        !attributeValue.startsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR) &&
        attributeValue.endsWith(CONST_VALUES.DOUBLE_QUOTES_SEPARATOR);

      if (searchTerm === CONST_VALUES.LDAP && keywordList?.length > 0) {
        // Raise error when the attribute key has any spelling mistake
        if (!LDAP_KEYS.includes(attributeKey)) {
          isAttributeInvalid = true;
          isWarning = false;
          errorMessageKey = 'universeForm.gFlags.invalidKey';
          break;
        }
        // Raise error when there are no proper quotation around attribute value
        if (hasNoEndQuote || hasNoStartQuote) {
          isAttributeInvalid = true;
          isWarning = false;
          errorMessageKey = 'universeForm.gFlags.missingQuoteAttributeValue';
          break;
        }
      } else if (searchTerm === CONST_VALUES.JWT && keywordList?.length > 0) {
        // Raise error when attribute key has any spelling mistake
        if (!JWT_KEYS.includes(attributeKey)) {
          isAttributeInvalid = true;
          isWarning = false;
          errorMessageKey = 'universeForm.gFlags.invalidKey';
          break;
        }
        // Raise error when there are no proper quotation around attribute value
        if (hasNoEndQuote || hasNoStartQuote || hasNoAndStartQuote) {
          isAttributeInvalid = true;
          isWarning = false;
          errorMessageKey = 'universeForm.gFlags.missingQuoteAttributeValue';
          break;
        }
      }

      if (isEmptyString(attributeValue)) {
        isAttributeInvalid = true;
        isWarning = false;
        errorMessageKey = 'universeForm.gFlags.missingAttributeValue';
        break;
      }
    }
  }

  return { isAttributeInvalid, errorMessageKey, isWarning };
};

export const optimizeVersion = (version) => {
  if (parseInt(version[version.length - 1], 10) === 0) {
    return optimizeVersion(version.slice(0, version.length - 1));
  } else {
    return version.join('.');
  }
};
