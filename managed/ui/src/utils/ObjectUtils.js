// Copyright (c) YugaByte, Inc.

var _ = require('lodash');
var semver = require('semver');

export function isDefinedNotNull(obj) {
  return (typeof obj !== "undefined" && obj !== null);
}

export function isEmptyArray(arr) {
  return _.isArray(arr) && arr.length === 0;
}

export function isNonEmptyArray(arr) {
  return _.isArray(arr) && arr.length > 0;
}

export function isEmptyObject(obj) {
  return _.isObject(obj) && Object.keys(obj).length === 0;
}

export function isNonEmptyObject(obj) {
  return _.isObject(obj) && Object.keys(obj).length > 0;
}

export function removeNullProperties(obj) {
  for (var propName in obj) {
    if (obj[propName] === null || obj[propName] === undefined) {
      delete obj[propName];
    }
  }
}

// TODO: Move functions below to ArrayUtils.js?

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

// TODO: Move these functions to Universe and UserIntent model/class files.

export function areIntentsEqual(userIntent1, userIntent2) {
  return (_.isEqual(userIntent1.numNodes,userIntent2.numNodes)
          && _.isEqual(userIntent1.regionList.sort(), userIntent2.regionList.sort())
          && _.isEqual(userIntent1.deviceInfo, userIntent2.deviceInfo)
          && _.isEqual(userIntent1.replicationFactor, userIntent2.replicationFactor)
          && _.isEqual(userIntent1.provider, userIntent2.provider)
          && _.isEqual(userIntent1.universeName, userIntent2.universeName)
          && _.isEqual(userIntent1.ybSoftwareVersion, userIntent2.ybSoftwareVersion)
          && _.isEqual(userIntent1.accessKeyCode, userIntent2.accessKeyCode))
}

export function areUniverseConfigsEqual(config1, config2) {
  var userIntentsEqual = true;
  var dataObjectsEqual = true;
  if (config1 && config2) {
    if (config1.userIntent && config2.userIntent) {
      userIntentsEqual = areIntentsEqual(config1.userIntent, config2.userIntent);
    }
    dataObjectsEqual = _.isEqual(config1.nodeDetailsSet, config2.nodeDetailsSet) && _.isEqual(config1.placementInfo, config2.placementInfo);
  }
  return dataObjectsEqual && userIntentsEqual;
}

// TODO: Move this function to NumberUtils.js?

export function normalizeToPositiveInt(value) {
  return parseInt(Math.abs(value), 10) || 0;
}

// TODO: Move the functions below to StringUtils.js?

export function trimString(string) {
  return string && string.trim()
}

export function convertSpaceToDash(string) {
  return string && string.replace(/\s+/g, '-');
}

export function sortVersionStrings(arr) {
  return arr.sort((a,b) => semver.valid(a) && semver.valid(b) ? semver.lt(a,b) : a < b);
}

// FIXME: Deprecated. Change all references to use isNonEmptyArray instead.
export var isValidArray = isNonEmptyArray;

// FIXME: isValidObject has never properly checked the object type.
// FIXME: We have renamed isValidObject to isDefinedNotNull, and
// FIXME: this alias is only kept here for backward compatibility
// FIXME: and should be removed after changing all existing uses.
export var isValidObject = isDefinedNotNull;
