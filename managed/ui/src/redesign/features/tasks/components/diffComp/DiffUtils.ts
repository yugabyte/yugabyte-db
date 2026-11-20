/*
 * Created on Wed Jun 05 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { isEqual, keys, pick, union } from 'lodash';
import { DiffOperation, GFlagDiff, GFlagsDiffProps } from './dtos';
import { UniverseDetails } from '../../../universe/universe-form/utils/dto';
import { getPrimaryCluster } from '../../../universe/universe-form/utils/helpers';
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

// The gflags of the primary cluster that the task changed, from the before/after values rebuilt
// from the taskDiff. A flag that did not change is absent on both sides.
export const getGFlagDiffs = (
  beforeData: unknown,
  afterData: unknown,
  gflagsAudit?: GFlagsDiffProps['gflags']
) => {
  const flagsOf = (data: unknown, process: 'MASTER' | 'TSERVER') => {
    const intent = getPrimaryCluster(data as UniverseDetails)?.userIntent;
    return {
      ...(process === 'MASTER' ? intent?.masterGFlags : intent?.tserverGFlags),
      ...intent?.specificGFlags?.perProcessFlags?.value?.[process]
    } as Record<string, string>;
  };
  const diffsOf = (process: 'MASTER' | 'TSERVER'): GFlagDiff[] => {
    const before = flagsOf(beforeData, process);
    const after = flagsOf(afterData, process);
    const audited = gflagsAudit?.[process === 'MASTER' ? 'master' : 'tserver'];
    return union(keys(before), keys(after))
      .filter((name) => before[name] !== after[name])
      // The audit entries compare the effective flags, so a flag missing from them did not change
      // (e.g. legacy flags that moved into specificGFlags look added in the delta).
      .filter((name) => !audited || audited.some((entry) => entry.name === name))
      .map((name) => ({
        name,
        old: before[name] ?? audited?.find((entry) => entry.name === name)?.old ?? null,
        new: after[name] ?? null,
        default: audited?.find((entry) => entry.name === name)?.default ?? ''
      }));
  };
  return { master: diffsOf('MASTER'), tserver: diffsOf('TSERVER') };
};
