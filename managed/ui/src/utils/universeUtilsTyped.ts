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

interface compareYBSoftwareVersionsParams {
  versionA: string;
  versionB: string;
  options?: { suppressFormatError?: boolean; requireOrdering?: boolean };
}

interface compareYBSoftwareVersionsWithReleaseTrackParams {
  version: string;
  stableVersion: string;
  previewVersion: string;
  options?: { suppressFormatError?: boolean; requireOrdering?: boolean };
}

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
export const compareYBSoftwareVersions = ({
  versionA,
  versionB,
  options
}: compareYBSoftwareVersionsParams): number => {
  const suppressFormatError = options?.suppressFormatError ?? false;
  const requireOrdering = options?.requireOrdering ?? false;
  // We only consider the first 2 parts of the version string.
  // <version number>-<build>
  // A second '-' and anything that follows will be ignored for the purpose of version
  // comparison.
  const matchA = YBSoftwareVersion.REGEX.exec(versionA?.split('-', 2)?.join('-'));
  const matchB = YBSoftwareVersion.REGEX.exec(versionB?.split('-', 2)?.join('-'));

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

    // If the version core is identical and one of versions is a custom build (no build number),
    // then we will consider the YB software versions as equal if requireOrdering is false since we can't compare them.
    // Sort if required for ordering the version strings
    // Ex: 2.17.3.0-b123 and 2.17.3.0-customBuildName are considered equal.
    return requireOrdering ? (
      sortTags(
        matchA[YBSoftwareVersion.GroupIndex.BUILD],
        matchA[YBSoftwareVersion.GroupIndex.BUILD_NUMBER],
        matchB[YBSoftwareVersion.GroupIndex.BUILD],
        matchB[YBSoftwareVersion.GroupIndex.BUILD_NUMBER])
    ) : 0;
  }

  if (suppressFormatError && !requireOrdering) {
  // If suppressFormatError is true, requireOrdering is false
  // and the YB version strings are unable to be parsed,
  // we'll simply consider the versions as equal.
    return 0;
  }

  if (!requireOrdering) {
    throw new Error('Unable to parse YB software version strings.');
  }

  if (matchA !== null) {
    return 1;
  }
  if (matchB !== null) {
    return -1;
  }
  return 0;

};

/**
 * Sort the build tags for YB versions
 * If the build tag is not well-formed/ does not adhere to the regex for either versions,
 * and requireOrdering is set to true in compareYBSoftwareVersions
 * Sort b{n} tags before custom build tags like mihnea, sergei, or 9b180349b
*/

export const sortTags = (
  buildA: string,
  buildNumberA: string,
  buildB: string,
  buildNumberB: string): number => {
  const buildNumberIntA = parseInt(buildNumberA);
  const buildNumberIntB = parseInt(buildNumberB);
  if (buildNumberIntA || buildNumberIntB) {
    return (isNaN(buildNumberIntA) ? 0 : 1) - (isNaN(buildNumberIntB) ? 0 : 1);
  }
  return buildB.localeCompare(buildA);
};

/**
 * Compares YB software versions with the corresponding release track.
 * For any new feature, a lower stable version and preview version must be provided
 * for comparison.
 * @returns
 * - A positive number when version > stable/preview version
 * - `0` when version is equal to stable/preview version
 * - A negative number when version < stable/preview version
 */
export const compareYBSoftwareVersionsWithReleaseTrack = ({
  version,
  stableVersion,
  previewVersion,
  options
}: compareYBSoftwareVersionsWithReleaseTrackParams): number => {
  const isCurrentVersionStable = isVersionStable(version);
  const comparingVersion = isCurrentVersionStable ? stableVersion : previewVersion;
  return compareYBSoftwareVersions({ versionA: version, versionB: comparingVersion, options });
};

/**
 * Check if the given release version belongs to the Stable track
 *
 * Returns `true` if the minor version is even or if the newer name format (ex: 2024.1.0.0) is followed 
 */
export const isVersionStable = (version: any): boolean => {
  return Number(version?.split?.('.')[1]) % 2 === 0 || version?.split?.('.')[0].length === 4;
};
