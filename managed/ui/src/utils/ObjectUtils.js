// Copyright (c) YugaByte, Inc.

var _ = require('lodash');

export function isValidObject(obj) {
  if (typeof obj !== "undefined" && obj !== null) {
    return true;
  } else {
    return false;
  }
}

export function isValidArray(arr) {
  if (typeof arr !== "undefined" && arr.length > 0) {
    return true;
  } else {
    return false;
  }
}

export function removeNullProperties(obj) {
  for (var propName in obj) {
    if (obj[propName] === null || obj[propName] === undefined) {
      delete obj[propName];
    }
  }
}

export function sortByLengthOfArrayProperty(array, propertyName) {
  function arrayLengthComparator(item) {
    return item[propertyName] ? item[propertyName].length : 0;
  }
  return _.sortBy(array, arrayLengthComparator);
}
