import _ from 'lodash';

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
