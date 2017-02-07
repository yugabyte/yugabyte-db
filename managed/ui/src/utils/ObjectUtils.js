// Copyright (c) YugaByte, Inc.

var _ = require('lodash');

export function isDefinedNotNull(obj) {
  return (typeof obj !== "undefined" && obj !== null);
}
// TODO: Deprecated. Change all references to use isDefinedNotNull instead.
export var isValidObject = isDefinedNotNull;

export function isValidArray(arr) {
  return (isDefinedNotNull(arr) && arr.length && arr.length > 0 && Array.isArray(arr));
}

export function isValidFunction(func) {
  return (typeof func === "function");
}

// TODO: Rename to isValidObject after changing previous isValidObject references to isDefinedNotNull.
export function isProperObject(obj) {
  return (typeof obj === "object" && obj !== null);
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

export function groupWithCounts(array) {
  var counts = {};
  array.forEach(function(item) {
    counts[item] = counts[item] || 0;
    counts[item]++;
  });
  return counts;
}

export function sortedGroupCounts(array) {
  var counts = groupWithCounts(array);
  return Object.keys(counts).sort().map(function(item) {
    return {
      value: item,
      count: counts[item],
    };
  });
}
