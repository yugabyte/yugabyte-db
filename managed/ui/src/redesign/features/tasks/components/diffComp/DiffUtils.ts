/*
 * Created on Wed Jun 05 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { isEqual, pick } from 'lodash';
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
  return gFlag.old === null ? DiffOperation.ADDED : gFlag.new === null ? DiffOperation.REMOVED : DiffOperation.CHANGED;
};
