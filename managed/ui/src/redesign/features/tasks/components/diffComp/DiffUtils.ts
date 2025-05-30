/*
 * Created on Wed Jun 05 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { isEqual, keys, pick } from 'lodash';
import { DiffOperation, GFlagDiff } from './dtos';
import { PlacementAZ } from '../../../universe/universe-form/utils/dto';

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

// returns the operation for a GFlagDiff object
export const getGFlagOperation = (gFlag: GFlagDiff) => {
  return gFlag.old === null
    ? DiffOperation.ADDED
    : gFlag.new === null
    ? DiffOperation.REMOVED
    : DiffOperation.CHANGED;
};

export type FieldOperations = { [key in keyof PlacementAZ | string]: DiffOperation | undefined };

// checks which fields have changed in a PlacementAZ object
// returns an object with the fields as keys and the operations as values
// isAfterCard is used to determine if we are going to display in before card or after card.
// is before card, we want to stirke through and display the fields in red that are changed.
// but in after card, we want to display the fields in green.
export const getFieldOpertions = (
  beforePlacement?: PlacementAZ,
  afterPlacement?: PlacementAZ,
  isAfterCard = false
): FieldOperations => {
  const fields: FieldOperations = {
    name: undefined,
    numNodesInAZ: undefined,
    isAffinitized: undefined
    // replicationFactor: undefined
  };

  if (!beforePlacement) {
    return {
      ...keys(fields).reduce((acc, key) => ({ ...acc, [key]: DiffOperation.ADDED }), {})
    };
  }
  if (!afterPlacement) {
    return {
      ...keys(fields).reduce((acc, key) => ({ ...acc, [key]: DiffOperation.REMOVED }), {})
    };
  }
  keys(fields).forEach((key) => {
    if ((beforePlacement as any)[key] !== (afterPlacement as any)[key]) {
      fields[key] = isAfterCard ? DiffOperation.ADDED : DiffOperation.CHANGED;
    } else {
      fields[key] = DiffOperation.UNCHANGED;
    }
  });
  return fields;
};
