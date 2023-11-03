import { differenceInDays, Interval, intervalToDuration, intlFormat, subDays, subHours } from 'date-fns';
import { PASSWORD_MIN_LENGTH } from '@app/helpers/const';
import {
  ClusterFaultTolerance
} from '@app/api/src';
import i18next from 'i18next';
import type { WeekDay } from '@app/components/YBDaypicker/YBDaypicker';
import { capitalize } from 'lodash';

export const convertMBtoGB = (value: string | number, round = true): number => {
  const result = Number(value) / 1024;
  return round ? Math.round(result) : result;
};

// generate a random password that meets password strength regex
export const generatePassword = (length: number): string => {
  if (length < PASSWORD_MIN_LENGTH) throw Error('Password length is too short');

  // Note dictionary is 64 characters which is evenly divisible by 256 which provides even distribution.
  const dictionary = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_';

  const random = new Uint8Array(length);
  window.crypto.getRandomValues(random);

  const chars = [];
  for (const i of random) {
    chars.push(dictionary.charAt(i % dictionary.length));
  }
  return chars.join('');
};

/**
 * Rounds to hundredth decimal place with EPSILON used to round 1.005 correctly.
 * @param num
 * @returns Number rounded to two decimal places.
 */
export const roundDecimal = (num: number): number => Math.round((num + Number.EPSILON) * 100) / 100;

export const formatCurrency = (num: number, decimals?: number): string => {
  return new Intl.NumberFormat(i18next.languages[0], {
    style: 'currency',
    currency: 'USD',
    minimumFractionDigits: decimals ?? (num % 1 ? 2 : 0)
  })
    .formatToParts(num)
    .map((val) => val.value)
    .join('');
};

export const areIdArraysEqual = (a: number[], b: number[]): boolean => {
  if (a.length !== b.length) {
    return false;
  }
  const setA = new Set(a);
  return b.every((x) => setA.has(x));
};

// export const isClusterCreating = (cluster?: ClusterData): boolean => {
//   return !!cluster?.info.metadata?.created_on && cluster.info.metadata.created_on === cluster.info.metadata.updated_on;
// };

// export const isFeatureFlagEnabled = (appConfigs?: AppConfigResponseDataItem[], path?: string): boolean | undefined => {
//   const selectedConfig = appConfigs?.find((appConfig) => appConfig.path === path);
//   if (selectedConfig) {
//     return selectedConfig?.value === 'true';
//   }
//   return undefined;
// };

export enum RelativeInterval {
  LastHour = 'lasthour',
  Last6Hours = 'last6hours',
  Last12hours = 'last12hours',
  Last24hours = 'last24hours',
  Last7days = 'last7days'
}

export type ClusterType = 'PRIMARY' | 'READ_REPLICA';

// convert relative interval like "last 6 hours" to exact pair of start/end date objects
export const getInterval = (relativeInterval: RelativeInterval): Interval => {
  let start: Date;
  const end = new Date();
  switch (relativeInterval) {
    case RelativeInterval.LastHour:
      start = subHours(end, 1);
      break;
    case RelativeInterval.Last6Hours:
      start = subHours(end, 6);
      break;
    case RelativeInterval.Last12hours:
      start = subHours(end, 12);
      break;
    case RelativeInterval.Last24hours:
      start = subHours(end, 24);
      break;
    case RelativeInterval.Last7days:
      start = subDays(end, 7);
      break;
  }

  return { start, end };
};

export const timeFormatterWithStartEnd = (start: number | Date, end: number | Date): ((value: unknown) => string) => {
  const timeFormatter = (value: unknown): string => {
    if (typeof value === 'number') {
      const date = new Date(value * 1000); // the "value" is in seconds and constructor requires milliseconds
      const showFullDate = differenceInDays(end, start) >= 1;
      return intlFormat(date, {
        month: showFullDate ? 'numeric' : undefined,
        day: showFullDate ? 'numeric' : undefined,
        hour: 'numeric',
        minute: 'numeric',
        // @ts-ignore: Parameter is not yet supported by `date-fns` but
        // is supported by underlying Intl.DateTimeFormat. CLOUDGA-5283
        hourCycle: 'h23'
      });
    }
    return '';
  };
  return timeFormatter;
};

export const getMemorySizeUnits = (bytes: number): string => {
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  if (bytes === 0) return '0 B';
  if (bytes === null || isNaN(bytes)) return '-';
  const i = parseInt(String(Math.floor(Math.log(bytes) / Math.log(1024))));
  return `${Math.round(bytes / Math.pow(1024, i))} ${sizes[i]}`;
};

export const getBinaryStrArray = (num: string, size?: number): string[] => {
  let binaryArr = (parseInt(num, 10) >>> 0).toString(2).split('');
  if (size && binaryArr.length < size) {
    binaryArr = [...(Array(size - binaryArr.length).fill('0') as string[]), ...binaryArr];
  }
  return binaryArr;
};

// convert country ISO code into a unicode symbol
export const countryToFlag = (isoCode?: string): string => {
  const COUNTRY_CODE_SALT = 127397;
  if (isoCode) {
    return isoCode.toUpperCase().replace(/./g, (char) => String.fromCodePoint(char.charCodeAt(0) + COUNTRY_CODE_SALT));
  }
  return '';
};

export const getFaultTolerance = (type: ClusterFaultTolerance | undefined, t: (key: string) => string): string => {
  switch (type) {
    case ClusterFaultTolerance.Region:
      return t('clusterWizard.faultToleranceRegion')
    case ClusterFaultTolerance.Zone:
      return t('clusterWizard.faultToleranceAZ');
    case ClusterFaultTolerance.Node:
      return t('clusterWizard.faultToleranceInstance');
    case ClusterFaultTolerance.None:
      return t('clusterWizard.faultToleranceNone');
    default:
      return '';
  }
};

export const getDefaultWeekDays = (t: (key: string) => string): WeekDay[] => {
  return [
    {
      name: t('weekday.short.Sun'),
      value: 0,
      selected: false
    },
    {
      name: t('weekday.short.Mon'),
      value: 1,
      selected: false
    },
    {
      name: t('weekday.short.Tue'),
      value: 2,
      selected: false
    },
    {
      name: t('weekday.short.Wed'),
      value: 3,
      selected: false
    },
    {
      name: t('weekday.short.Thu'),
      value: 4,
      selected: false
    },
    {
      name: t('weekday.short.Fri'),
      value: 5,
      selected: false
    },
    {
      name: t('weekday.short.Sat'),
      value: 6,
      selected: false
    }
  ];
};

// convert hour[single digit] or any number >> 4 to double digit >> 04
export const formatSingleToDoubleDigit = (input: number): string => {
  return ('0' + input.toString()).slice(-2);
};

// export const saveTextFile = (filename: string, data: string | undefined): void => {
//   const blob = new Blob([data ?? ''], { type: 'text/html' });
//   if ('msSaveOrOpenBlob' in window.navigator) {
//     window.navigator.msSaveBlob(blob, filename);
//   } else {
//     const elem = window.document.createElement('a');
//     elem.href = window.URL.createObjectURL(blob);
//     elem.download = filename;
//     document.body.appendChild(elem);
//     elem.click();
//     document.body.removeChild(elem);
//   }
// };

export const isObjectEmpty = (obj: Record<string, unknown>): boolean => {
  return Object.keys(obj).length === 0 && obj.constructor === Object;
};

// export const isLoggedInUserAdmin = (
//   roleList: UserAccountRoleData[] | undefined,
//   rolesData: RoleData[] | undefined
// ): boolean | null => {
//   if (roleList && rolesData) {
//     // assuming user belongs to a single account and have a single role associated
//     // this will change in future
//     const role = rolesData?.filter((roleItem) => roleList[0] && roleItem.id === roleList[0].role_ids[0]);
//     return role?.[0]?.name === 'account_admin';
//   }
//   return null;
// };

// export const checkIfAllowListChanged = (sessionKey: string, list?: NetworkAllowListData[]): boolean => {
//   if (!list) {
//     return false;
//   }
//   const allowListMap = browserStorage.getSessionAllowListOps(sessionKey);
//   const changesApplied = list.every((element) => {
//     const id = element?.info?.id;
//     if (id && id in allowListMap) {
//       if (allowListMap[id].operation === 'ADD') {
//         delete allowListMap[id];
//         return true;
//       }
//       if (allowListMap[id].operation === 'DELETE') {
//         delete allowListMap[id];
//         return false;
//       }
//       return false;
//     }
//     return true;
//   });
//   if (!changesApplied) {
//     return false;
//   }
//   return Object.values(allowListMap).every((entry) => entry.operation === 'DELETE');
// };

export const getTextWidth = (text: string, element: Element | undefined = undefined): number => {
  const ctx = document.createElement('canvas').getContext('2d');
  let width = 100;
  if (ctx) {
    if (element) {
      ctx.font = getFont(element);
    }
    width = ctx.measureText(text).width;
  }
  return width;
};

export const getFont = (element: Element): string => {
  const prop = ['font-style', 'font-variant', 'font-weight', 'font-size', 'font-family'];
  let font = '';
  for (const x of prop) font += window.getComputedStyle(element, null).getPropertyValue(x) + ' ';
  return font;
};

// Adding this because textTransform: 'capitalize' not working when all text capital
// for e.g. 'HELLO WORLD' => 'Hello World' not working using css
export const getCapitalizeWords = (text: string): string => {
  return text ? text.replace('_', ' ').split(' ').map(capitalize).join(' ') : '';
};

//DATE
export const getTimeInterval = (date1: string, date2?: string): Record<string, number> => {
  return intervalToDuration({
    start: new Date(date1),
    end: date2 ? new Date(date2) : new Date()
  });
};

export const getHumanInterval = (date1: string, date2?: string): string => {
  const { days, hours, minutes } = getTimeInterval(date1, date2);
  if (days === 0 && hours === 0) return minutes > 0 ? `${minutes} mins` : '< 1 min';

  if (days === 0 && hours > 0) {
    return `${hours}h ${minutes} m`;
  }

  return hours > 0 ? `${days}d ${hours}h` : `${days}d`;
};

// export const isClusterEditingDisabled = (cluster: ClusterData | undefined): boolean => {
//   if (!cluster) return false;
//   return (
//     cluster &&
//     (cluster.info.state === CLUSTER_STATE.Failed ||
//       cluster.info.state === CLUSTER_STATE.CreateFailed ||
//       cluster?.info.state === CLUSTER_STATE.Queued ||
//       cluster.info.state === CLUSTER_STATE.Updating ||
//       cluster?.info.state === CLUSTER_STATE.Pausing ||
//       cluster?.info.state === CLUSTER_STATE.Paused ||
//       cluster?.info.state === CLUSTER_STATE.Maintenance ||
//       cluster?.info.state === CLUSTER_STATE.Resuming)
//   );
// };

export const getTimeZoneAbbreviated = (date: Date): string => {
  const { 1: tz } = (/\((.+)\)/.exec(date.toString()) as unknown) as RegExpMatchArray;

  // In Chrome browser, new Date().toString() is
  // "Thu Aug 06 2020 16:21:38 GMT+0530 (India Standard Time)"

  // In Safari browser, new Date().toString() is
  // "Thu Aug 06 2020 16:24:03 GMT+0530 (IST)"

  if (tz.includes(' ')) {
    return tz
      .split(' ')
      .map(([first]) => first)
      .join('');
  }
  return tz;
};

const regionCountryCodes: { [k: string]: string } = {
  'us-east-2':      'us',
  'us-east-1':      'us',
  'us-west-1':      'us',
  'us-west-2':      'us',
  'af-south-1':     'za',
  'ap-south-1':     'in',
  'ap-northeast-3': 'jp',
  'ap-southeast-1': 'sg',
  'ap-southeast-2': 'au',
  'ap-northeast-1': 'jp',
  'ca-central-1':   'ca',
  'eu-central-1':   'de',
  'eu-west-1':      'ie',
  'eu-west-2':      'gb',
  'eu-south-1':     'it',
  'eu-west-3':      'fr',
  'eu-north-1':     'se',
  'me-south-1':     'bh',
  'sa-east-1':      'br',
  'us-gov-east-1':  'us',
  'us-gov-west-1':  'us',
}

export const getRegionCode = ({ region, zone }: { region?: string, zone?: string }) => 
  Object.entries(regionCountryCodes).find(([key]) => zone?.startsWith(key) || (region && key.startsWith(region)))?.[1]