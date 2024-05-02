import { YBSoftwareMetadata } from "../../../utils/dto";

export const sortVersionStrings = (arr: Record<string, string>[]) => {
  const regExp = /^(\d+).(\d+).(\d+).(\d+)(?:-[a-z]+)?(\d+)?/;
  const matchedVersions = arr.filter((a) => a.label.match(regExp));
  const abnormalVersions = arr.filter((a) => !a.label.match(regExp));
  return matchedVersions
    .sort((a, b) => {
      const a_arr = a.label.split(regExp).filter(Boolean);
      const b_arr = b.label.split(regExp).filter(Boolean);
      for (let idx = 0; idx < a_arr.length; idx++) {
        if (a_arr[idx] !== b_arr[idx]) {
          return parseInt(b_arr[idx], 10) - parseInt(a_arr[idx], 10);
        }
      }
      return 0;
    })
    .concat(abnormalVersions.sort((a, b) => a.label.localeCompare(b.label)));
};

export const getActiveDBVersions = (releasesMetadata: Record<string, YBSoftwareMetadata>) => {
    return Object.entries(releasesMetadata).filter((r) => {
        return (r[1] as YBSoftwareMetadata)?.state === 'ACTIVE'
    }).map((r) => ({
        label: r[0],
        value: r[0]
      }));
};

