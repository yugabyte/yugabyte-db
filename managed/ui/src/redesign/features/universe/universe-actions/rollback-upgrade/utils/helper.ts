import _ from 'lodash';
import { sortVersion } from '../../../../../../components/releases';

export const fetchLatestStableVersion = (releaseMetaData: any) => {
  let latestSeries = Object.entries(releaseMetaData)
    .filter((e) => Number(e[0].split('.')[1]) % 2 === 0)
    .map((e) => e[0])
    .sort(sortVersion);
  return releaseMetaData[latestSeries[0]];
};

export const fetchCurrentLatestVersion = (releaseMetaData: any, currentVersion: any) => {
  let currentSeries = Object.entries(releaseMetaData)
    .filter((e) => Number(e[0].split('.')[1]) === Number(currentVersion?.split('.')[1]))
    .map((e) => e[0])
    .sort(sortVersion);
  return releaseMetaData[currentSeries[0]];
};

export const fetchReleaseSeries = (releaseMetaData: any) => {
  let sortedReleases = Object.entries(releaseMetaData)
    .map((e) => e[0])
    .sort(sortVersion);
  let releaseSeriesMap = {};
  sortedReleases.forEach((r) => {
    const seriesKey = [r.split('.')[0], r.split('.')[1]].join('.');
    if (releaseSeriesMap.hasOwnProperty(seriesKey)) {
      releaseSeriesMap[seriesKey].push(releaseMetaData[r]);
    } else {
      releaseSeriesMap[seriesKey] = [releaseMetaData[r]];
    }
  });
  return releaseSeriesMap;
};
