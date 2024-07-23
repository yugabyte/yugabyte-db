/*
 * Created on Wed Jun 05 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { difference, isEqual, keys, pick, union } from 'lodash';
import { DiffOperation, DiffProps } from './dtos';
import { PlacementAZ } from '../../../universe/universe-form/utils/dto';

/**
 * Compares two objects and returns the differences between them.
 * @param obj1 - The first object to compare.
 * @param obj2 - The second object to compare.
 * @returns An object containing the differences between the two objects.
 */
export const getDiffsInObject = (obj1: Record<string, any>, obj2: Record<string, any>) => {
  const diffs = {
    [DiffOperation.ADDED]: [] as Partial<DiffProps>[],
    [DiffOperation.REMOVED]: [] as Partial<DiffProps>[],
    [DiffOperation.CHANGED]: [] as Partial<DiffProps>[]
  };
  const setA = keys(obj1);
  const setB = keys(obj2);

  const keysRemoved = difference(setA, setB);
  const keysAdded = difference(setB, setA);
  const keysUnionUnique = union(setA, setB);

  // iterate over the union of keys
  keysUnionUnique.forEach((key) => {
    // check if the key is in the removed
    if (keysRemoved.includes(key)) {
      diffs[DiffOperation.REMOVED].push({
        operation: DiffOperation.REMOVED,
        beforeData: obj1[key],
        afterData: undefined,
        attribute: key
      });
      // check if the key is in the added
    } else if (keysAdded.includes(key)) {
      diffs[DiffOperation.ADDED].push({
        operation: DiffOperation.ADDED,
        beforeData: undefined,
        afterData: obj2[key],
        attribute: key
      });
      // check if the key is in both objects
      // we are checking if the values are not equal,
      // if they are equal we don't need to add it to the diffs as they are not changed
    } else if (!isEqual(obj1[key], obj2[key])) {
      diffs[DiffOperation.CHANGED].push({
        operation: DiffOperation.CHANGED,
        beforeData: obj1[key],
        afterData: obj2[key],
        attribute: key
      });
    }
  });
  return diffs;
};

/**
 * Checks if two PlacementAZ objects are equal based on specific fields.
 * @param a - The first PlacementAZ object.
 * @param b - The second PlacementAZ object.
 * @returns True if the objects are equal, false otherwise.
 */
export const isAZEqual = (a: PlacementAZ, b: PlacementAZ) => {
  const fields = ['name', 'numNodesInAZ', 'isAffinitized', 'replicationFactor'];
  return isEqual(pick(a, fields), pick(b, fields));
};
