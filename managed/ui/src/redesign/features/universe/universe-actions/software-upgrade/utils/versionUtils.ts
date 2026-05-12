import { compareYBSoftwareVersions, isVersionStable } from '@app/utils/universeUtilsTyped';
import type { YbdbRelease, YbdbReleaseArtifact } from '../dtos';
import type { ReleaseOption } from '../types';

/**
 * Release series: first two components of the version (e.g. "2025.2" from "2025.2.0.1-b134").
 */
export const getReleaseSeries = (version: string): string =>
  version?.split('.').slice(0, 2).join('.') ?? '';

/**
 * Sort version strings in descending order (newest first).
 */
export const sortDesc = (a: string, b: string): number =>
  compareYBSoftwareVersions({
    versionA: b,
    versionB: a,
    options: { suppressFormatError: true, requireOrdering: true }
  });

/** Sort releases by version descending (newest first). */
const sortReleasesDesc = (releases: YbdbRelease[]): YbdbRelease[] =>
  [...releases].sort((a, b) => sortDesc(a.version, b.version));

/**
 * Build the list of version options for the DB upgrade dropdown:
 * - Latest stable (if current is stable or skipVersionChecks)
 * - Latest from current series
 * - All eligible versions grouped by series
 *
 * Releases without an artifact matching `currentReleaseArchitecture` are excluded,
 * since we cannot upgrade to a release that does not ship a binary for the universe's CPU arch.
 * If `currentReleaseArchitecture` is undefined (arch unknown), no architecture filtering is applied.
 */
export const buildVersionOptions = (
  releases: YbdbRelease[],
  currentReleaseVersion: string,
  currentReleaseArchitecture: YbdbReleaseArtifact['architecture'] | undefined,
  skipVersionChecks: boolean
): ReleaseOption[] => {
  if (releases.length === 0) {
    return [];
  }

  const archFilteredReleases = currentReleaseArchitecture
    ? releases.filter((release) =>
        release.artifacts?.some((artifact) => artifact.architecture === currentReleaseArchitecture)
      )
    : releases;

  const options: ReleaseOption[] = [];
  const isCurrentVersionStable = isVersionStable(currentReleaseVersion);
  const sortedReleases = sortReleasesDesc(archFilteredReleases);

  const shouldIncludeLatestStableRelease = isCurrentVersionStable || skipVersionChecks;
  if (shouldIncludeLatestStableRelease) {
    const latestStableRelease = sortedReleases.find((release) => isVersionStable(release.version));
    if (
      latestStableRelease &&
      compareYBSoftwareVersions({
        versionA: latestStableRelease.version,
        versionB: currentReleaseVersion,
        options: { suppressFormatError: true }
      }) >= 0
    ) {
      options.push({
        version: latestStableRelease.version,
        releaseInfo: latestStableRelease,
        label: latestStableRelease.version,
        series: 'Latest stable release'
      });
    }
  }

  const currentSeries = getReleaseSeries(currentReleaseVersion);
  const latestReleaseInCurrentSeries = currentSeries
    ? sortedReleases.find((release) => getReleaseSeries(release.version) === currentSeries)
    : null;
  if (latestReleaseInCurrentSeries) {
    options.push({
      version: latestReleaseInCurrentSeries.version,
      releaseInfo: latestReleaseInCurrentSeries,
      label: latestReleaseInCurrentSeries.version,
      series: 'Latest release from the current series'
    });
  }

  // Add all eligible versions grouped by series.
  const eligible = skipVersionChecks
    ? sortedReleases
    : sortedReleases.filter(
        (release) =>
          isVersionStable(release.version) === isCurrentVersionStable &&
          compareYBSoftwareVersions({
            versionA: release.version,
            versionB: currentReleaseVersion ?? '',
            options: { suppressFormatError: true }
          }) >= 0
      );

  return [
    ...options,
    ...eligible.map((release) => ({
      label: release.version,
      version: release.version,
      releaseInfo: release,
      series: `v${getReleaseSeries(release.version)} Series ${
        isVersionStable(release.version) ? '(Stable)' : '(Preview)'
      }`
    }))
  ];
};
