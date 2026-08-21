import { describe, it, expect } from 'vitest';
import type { YbdbRelease } from '../dtos';
import { ReleaseState, ReleaseYbType } from '../dtos';
import { buildVersionOptions, getReleaseSeries } from './versionUtils';

describe('getReleaseSeries', () => {
  it('extracts the first two components from a version string', () => {
    expect(getReleaseSeries('2.18.1.0-b123')).toBe('2.18');
    expect(getReleaseSeries('2025.2.1.0-b134')).toBe('2025.2');
    expect(getReleaseSeries('')).toBe('');
  });
});

describe('buildVersionOptions', () => {
  type ReleaseType = YbdbRelease['release_type'];
  type Architecture = NonNullable<YbdbRelease['artifacts']>[number]['architecture'];

  const createReleases = (
    pairs: Array<
      | [version: string, release_type: ReleaseType]
      | [version: string, release_type: ReleaseType, architectures: Architecture[]]
    >
  ): YbdbRelease[] =>
    pairs.map(([version, release_type, architectures]) => ({
      release_uuid: `release-uuid-${version}`,
      version,
      yb_type: ReleaseYbType.YBDB,
      release_type,
      state: ReleaseState.ACTIVE,
      release_tag: `tag-${version}`,
      artifacts: (architectures ?? ['x86_64', 'aarch64']).map((architecture) => ({
        platform: 'LINUX' as const,
        architecture
      }))
    }));

  /** Extract just the version strings from options for concise assertions. */
  const versionsOf = (options: ReturnType<typeof buildVersionOptions>) =>
    options.map((option) => option.version);

  it('returns empty array when no releases are provided', () => {
    expect(buildVersionOptions([], '2.18.0.0-b123', undefined, false)).toEqual([]);
  });

  it('attaches the full release info to every option', () => {
    const releases = createReleases([
      ['2.18.1.0-b5', 'STS'],
      ['2.18.2.0-b3', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.18.1.0-b5', undefined, false);
    const releaseByVersion = Object.fromEntries(releases.map((r) => [r.version, r]));

    options.forEach((option) => {
      expect(option.releaseInfo).toEqual(releaseByVersion[option.version]);
    });
  });

  it('labels the latest stable and current-series options for a stable version', () => {
    const releases = createReleases([
      ['2.18.1.0-b123', 'STS'],
      ['2.18.1.0-b5', 'STS'],
      ['2.18.2.0-b3', 'STS'],
      ['2.18.3.0-b1', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.18.1.0-b123', undefined, false);

    // When current is stable and within the same series, both promoted options
    // point to the highest version in that series.
    expect(options).toEqual(
      expect.arrayContaining([
        expect.objectContaining({ version: '2.18.3.0-b1', series: 'Latest stable release' }),
        expect.objectContaining({
          version: '2.18.3.0-b1',
          series: 'Latest release from the current series'
        })
      ])
    );
  });

  it('includes all releases sorted descending when skipVersionChecks is true', () => {
    const releases = createReleases([
      ['2.17.0.0-b123', 'PREVIEW'],
      ['2025.2.1.0-b23', 'LTS'],
      ['2.17.1.0-b2', 'PREVIEW'],
      ['2.18.2.0-b4', 'STS'],
      ['2.17.0.0-b523', 'PREVIEW'],
      ['2.18.1.0-b6', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.17.0.0-b123', undefined, true);

    expect(versionsOf(options)).toEqual([
      '2025.2.1.0-b23', // Latest stable release
      '2.17.1.0-b2', // Latest release from the current series
      '2025.2.1.0-b23',
      '2.18.2.0-b4',
      '2.18.1.0-b6',
      '2.17.1.0-b2',
      '2.17.0.0-b523',
      '2.17.0.0-b123'
    ]);
  });

  it('excludes stable versions and omits "Latest Stable" when current is preview', () => {
    const releases = createReleases([
      ['2.17.0.0-b1', 'PREVIEW'],
      ['2.17.1.0-b5', 'PREVIEW'],
      ['2.17.2.0-b3', 'PREVIEW'],
      ['2.18.0.0-b1', 'STS'],
      ['2.18.1.0-b2', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.17.0.0-b1', undefined, false);
    const versions = versionsOf(options);

    // Stable versions should be excluded
    expect(versions).not.toContain('2.18.0.0-b1');
    expect(versions).not.toContain('2.18.1.0-b2');

    // Preview versions >= current should be included
    expect(versions).toEqual(expect.arrayContaining(['2.17.2.0-b3', '2.17.1.0-b5', '2.17.0.0-b1']));

    // No "Latest stable release" promoted option
    const seriesLabels = options.map((o) => o.series);
    expect(seriesLabels).not.toContain('Latest stable release');

    // "Latest release from the current series" should still be present
    expect(options).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          version: '2.17.2.0-b3',
          series: 'Latest release from the current series'
        })
      ])
    );
  });

  it('excludes preview versions when the current version is stable', () => {
    const releases = createReleases([
      ['2.17.1.0-b1', 'PREVIEW'],
      ['2.18.0.0-b1', 'STS'],
      ['2.18.1.0-b5', 'STS'],
      ['2.18.2.0-b3', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.18.0.0-b1', undefined, false);

    expect(versionsOf(options)).not.toContain('2.17.1.0-b1');
    expect(versionsOf(options)).toEqual(
      expect.arrayContaining(['2.18.2.0-b3', '2.18.1.0-b5', '2.18.0.0-b1'])
    );
  });

  it('handles releases without build suffixes when current version has one', () => {
    const releases = createReleases([
      ['2025.2.1.0', 'LTS'],
      ['2025.2.2.0', 'LTS']
    ]);
    const options = buildVersionOptions(releases, '2025.2.1.0-b132', undefined, false);

    expect(versionsOf(options)).toEqual(expect.arrayContaining(['2025.2.2.0', '2025.2.1.0']));
  });

  it('treats different build numbers of the same version as separate options', () => {
    const releases = createReleases([
      ['2.18.1.0-b3', 'STS'],
      ['2.18.1.0-b5', 'STS'],
      ['2.18.1.0-b10', 'STS'],
      ['2.18.2.0-b1', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.18.1.0-b3', undefined, false);

    expect(versionsOf(options)).toEqual(
      expect.arrayContaining(['2.18.2.0-b1', '2.18.1.0-b10', '2.18.1.0-b5', '2.18.1.0-b3'])
    );

    // Latest from current series should be the highest in 2.18
    expect(options).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          version: '2.18.2.0-b1',
          series: 'Latest release from the current series'
        })
      ])
    );
  });

  it('excludes same-type versions older than the current version', () => {
    const releases = createReleases([
      ['2.18.0.0-b1', 'STS'],
      ['2.18.1.0-b5', 'STS'],
      ['2.18.2.0-b1', 'STS'],
      ['2.18.3.0-b1', 'STS']
    ]);
    const options = buildVersionOptions(releases, '2.18.2.0-b1', undefined, false);
    const versions = versionsOf(options);

    // Older stable versions should be excluded
    expect(versions).not.toContain('2.18.0.0-b1');
    expect(versions).not.toContain('2.18.1.0-b5');

    // Current and newer should be included
    expect(versions).toEqual(expect.arrayContaining(['2.18.3.0-b1', '2.18.2.0-b1']));
  });

  it('assigns correct series labels to grouped options', () => {
    const releases = createReleases([
      ['2.17.0.0-b1', 'PREVIEW'],
      ['2.17.1.0-b2', 'PREVIEW'],
      ['2.18.0.0-b1', 'STS'],
      ['2.18.1.0-b3', 'STS']
    ]);
    // skipVersionChecks=true so both stable and preview appear in the grouped list
    const options = buildVersionOptions(releases, '2.17.0.0-b1', undefined, true);

    // Stable releases get "(Stable)" suffix
    expect(options).toEqual(
      expect.arrayContaining([
        expect.objectContaining({ version: '2.18.1.0-b3', series: 'v2.18 Series (Stable)' }),
        expect.objectContaining({ version: '2.18.0.0-b1', series: 'v2.18 Series (Stable)' })
      ])
    );

    // Preview releases get "(Preview)" suffix
    expect(options).toEqual(
      expect.arrayContaining([
        expect.objectContaining({ version: '2.17.1.0-b2', series: 'v2.17 Series (Preview)' }),
        expect.objectContaining({ version: '2.17.0.0-b1', series: 'v2.17 Series (Preview)' })
      ])
    );
  });

  describe('architecture filtering', () => {
    it('excludes releases without an artifact matching the current architecture', () => {
      const releases = createReleases([
        ['2.18.1.0-b1', 'STS', ['x86_64', 'aarch64']],
        ['2.18.2.0-b1', 'STS', ['x86_64']],
        ['2.18.3.0-b1', 'STS', ['aarch64']]
      ]);
      const options = buildVersionOptions(releases, '2.18.1.0-b1', 'aarch64', false);

      expect(versionsOf(options)).not.toContain('2.18.2.0-b1');
      expect(versionsOf(options)).toEqual(
        expect.arrayContaining(['2.18.3.0-b1', '2.18.1.0-b1'])
      );
    });

    it('does not filter by architecture when currentReleaseArchitecture is undefined', () => {
      const releases = createReleases([
        ['2.18.1.0-b1', 'STS', ['x86_64']],
        ['2.18.2.0-b1', 'STS', ['aarch64']]
      ]);
      const options = buildVersionOptions(releases, '2.18.1.0-b1', undefined, false);

      expect(versionsOf(options)).toEqual(
        expect.arrayContaining(['2.18.2.0-b1', '2.18.1.0-b1'])
      );
    });

    it('skips arch-mismatched releases when picking the latest stable promoted option', () => {
      const releases = createReleases([
        ['2.18.1.0-b1', 'STS', ['x86_64', 'aarch64']],
        ['2.18.5.0-b1', 'STS', ['x86_64']],
        ['2.18.3.0-b1', 'STS', ['aarch64']]
      ]);
      const options = buildVersionOptions(releases, '2.18.1.0-b1', 'aarch64', false);

      expect(options).toEqual(
        expect.arrayContaining([
          expect.objectContaining({ version: '2.18.3.0-b1', series: 'Latest stable release' })
        ])
      );
      expect(versionsOf(options)).not.toContain('2.18.5.0-b1');
    });

    it('excludes releases with no artifacts when an architecture is specified', () => {
      const releases: YbdbRelease[] = [
        {
          release_uuid: 'r1',
          version: '2.18.2.0-b1',
          yb_type: ReleaseYbType.YBDB,
          release_type: 'STS',
          state: ReleaseState.ACTIVE
        }
      ];
      const options = buildVersionOptions(releases, '2.18.1.0-b1', 'x86_64', false);

      expect(options).toEqual([]);
    });
  });
});
