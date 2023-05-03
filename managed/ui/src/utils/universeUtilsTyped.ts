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

/**
 * Regex for YB Software Version
 * ```
 * `
 * Group 1 - Version core string (ex: 2.17.3.0)
 *   Group 2 - Major release number
 *   Group 3 - Minor release number
 *   Group 4 - Patch release number
 *   Group 5 - Revision release number
 * Group 6 - Build string (ex: b103, customBuild)
 *   Group 7 - Build number (when not a custom build)
 * `
 * ```
 */
const YBSoftwareVersion = {
  REGEX: /^((0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))(?:-(b([1-9]+\d*)|\S+))?$/,
  GroupIndex: {
    VERSION_CORE: 1,
    MAJOR_RELEASE: 2,
    MINOR_RELEASE: 3,
    PATCH_RELEASE: 4,
    REVISION_RELEASE: 5,
    BUILD: 6,
    BUILD_NUMBER: 7
  }
} as const;

/**
 * Compares YB software versions.
 * @returns
 * - A positive number when versionA > versionB
 * - `0` when versionA is equal to versionB
 * - A negative number when versionA < versionB
 *
 * We also consider versions to be equal when any of the following is true:
 * - The version core is equal and one of the provided software versions does not have
 *   a build number (custom/local build)
 * - Unable to parse the provided YB software versions and `suppressFormatError = true`
 */
export const compareYBSoftwareVersions = (
  versionA: string,
  versionB: string,
  suppressFormatError = false
): number => {
  // We only consider the first 2 parts of the version string.
  // <version number>-<build>
  // A second '-' and anything that follows will be ignored for the purpose of version
  // comparison.
  const matchA = YBSoftwareVersion.REGEX.exec(versionA.split('-', 2).join('-'));
  const matchB = YBSoftwareVersion.REGEX.exec(versionB.split('-', 2).join('-'));

  if (matchA && matchB) {
    for (let i = 2; i < 6; i++) {
      const releaseNumberA = parseInt(matchA[i]);
      const releaseNumberB = parseInt(matchB[i]);
      if (releaseNumberA !== releaseNumberB) {
        return releaseNumberA - releaseNumberB;
      }
    }

    if (
      matchA[YBSoftwareVersion.GroupIndex.BUILD_NUMBER] &&
      matchB[YBSoftwareVersion.GroupIndex.BUILD_NUMBER]
    ) {
      const buildNumberA = parseInt(matchA[YBSoftwareVersion.GroupIndex.BUILD_NUMBER]);
      const buildNumberB = parseInt(matchB[YBSoftwareVersion.GroupIndex.BUILD_NUMBER]);
      return buildNumberA - buildNumberB;
    }

    // If the version core is indentical and one of versions is a custom build (no build number),
    // then we will consider the YB software versions as equal since we can't compare them.
    // Ex: 2.17.3.0-b123 and 2.17.3.0-customBuildName are considered equal.
    return 0;
  }

  if (suppressFormatError) {
    // If suppressFormatError is true and the YB version strings are unable to be parsed,
    // we'll simply consider the versions as equal.
    return 0;
  }

  throw new Error('Unable to parse YB software version strings.');
};
