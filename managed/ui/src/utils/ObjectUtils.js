// Copyright (c) YugaByte, Inc.

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
