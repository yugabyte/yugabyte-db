import _ from 'lodash';
import { timeFormatterISO8601 } from './dateUtils';

export function isDefinedNotNull(obj: any) {
  return typeof obj !== 'undefined' && obj !== null;
}

export function isEmptyArray(arr: any) {
  return _.isArray(arr) && arr.length === 0;
}

export function isNonEmptyArray(arr: any) {
  return _.isArray(arr) && arr.length > 0;
}

export function isNullOrEmpty(obj: any) {
  // eslint-disable-next-line eqeqeq
  if (obj == null) {
    return true;
  }
  return _.isObject(obj) && Object.keys(obj).length === 0;
}

export function isEmptyObject(obj: any) {
  if (typeof obj === 'undefined') {
    return true;
  }
  return _.isObject(obj) && Object.keys(obj).length === 0;
}

export function isNonEmptyObject(obj: any) {
  return _.isObject(obj) && Object.keys(obj).length > 0;
}

export function isNonEmptyString(str: string) {
  return _.isString(str) && str.trim().length > 0;
}

export function isEmptyString(str: string) {
  return _.isString(str) && str.trim().length === 0;
}

export const isValidObject = isDefinedNotNull;

export function removeNullProperties(obj: any) {
  for (const propName in obj) {
    if (obj[propName] === null || obj[propName] === undefined) {
      delete obj[propName];
    }
  }
}

export function isYAxisGreaterThanThousand(dataArray: any) {
  for (let counter = 0; counter < dataArray.length; counter++) {
    if (isNonEmptyArray(dataArray[counter].y)) {
      for (let idx = 0; idx < dataArray[counter].y.length; idx++) {
        if (Number(dataArray[counter].y[idx]) > 1000) {
          return true;
        }
      }
    }
  }
  return false;
}

export function divideYAxisByThousand(dataArray: any) {
  for (let counter = 0; counter < dataArray.length; counter++) {
    if (isNonEmptyArray(dataArray[counter].y)) {
      for (let idx = 0; idx < dataArray[counter].y.length; idx++) {
        dataArray[counter].y[idx] = Number(dataArray[counter].y[idx]) / 1000;
      }
    }
  }
  return dataArray;
}

// Function to convert the time values in x-axis of metrics panels to a specific timezone
//  as a workaround. Plotly does not support specifying timezones in layout.
export function timeFormatXAxis(dataArray: any, timezone: string | undefined) {
  for (let counter = 0; counter < dataArray.length; counter++) {
    if (isNonEmptyArray(dataArray[counter].x)) {
      for (let idx = 0; idx < dataArray[counter].x.length; idx++) {
        dataArray[counter].x[idx] = timeFormatterISO8601(
          dataArray[counter].x[idx],
          undefined
        );
      }
    }
  }
  return dataArray;
}
