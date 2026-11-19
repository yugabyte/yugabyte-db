/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { isEqual, mapValues, pick } from 'lodash';

// The `taskDiff` of the audit_info response: the universe details, reduced to the changed parts.
// A changed value is a delta node; unchanged fields are absent, including the identity fields
// (clusterType, code, name, uuid) of a modified list element (see withIdentityFromPayload).
export interface DeltaNode {
  $deltaType: 'ADD' | 'DELETE' | 'REPLACE';
  $oldValue?: unknown;
  $newValue?: unknown;
}

export const isDeltaNode = (node: unknown): node is DeltaNode =>
  typeof node === 'object' && node !== null && '$deltaType' in node;

/**
 * Rebuilds the value before ('old') or after ('new') the task from a task diff. The result only
 * has only the changed fields, so a field that is absent on both sides is
 * unchanged. Port of DeltaEvaluator.generateValue.
 */
export const getTaskDiffValue = (node: unknown, side: 'old' | 'new'): any => {
  if (node === null || node === undefined) {
    return undefined;
  }
  if (isDeltaNode(node)) {
    // An ADD has no old value, and a DELETE has no new value.
    return side === 'old' ? (node.$oldValue ?? undefined) : (node.$newValue ?? undefined);
  }
  if (Array.isArray(node)) {
    return node
      .map((element) => getTaskDiffValue(element, side))
      .filter((value) => value !== undefined && value !== null);
  }
  if (typeof node === 'object') {
    const result: Record<string, unknown> = {};
    Object.entries(node as Record<string, unknown>).forEach(([key, value]) => {
      const sideValue = getTaskDiffValue(value, side);
      if (sideValue !== undefined && sideValue !== null) {
        result[key] = sideValue;
      }
    });
    return result;
  }
  return node;
};

/**
 * The backend drops the unchanged clusterType from a modified cluster, so the first cluster of the
 * taskDiff is taken as the primary one when it has no clusterType.
 * shortcut: wrong for a read-replica-only edit; drop this once the backend keeps clusterType.
 */
export const withDefaultClusterType = (taskDiff: Record<string, any>): Record<string, any> => {
  const clusters = taskDiff?.clusters;
  if (!Array.isArray(clusters) || clusters.length === 0) {
    return taskDiff;
  }
  const [first, ...rest] = clusters;
  if (isDeltaNode(first) || first?.clusterType) {
    return taskDiff;
  }
  return { ...taskDiff, clusters: [{ clusterType: 'PRIMARY', ...first }, ...rest] };
};

const IDENTITY_FIELDS = ['clusterType', 'code', 'name', 'uuid'];

// Whether the payload element has the values the modified (delta) element changed to.
const matchesPayload = (delta: any, payload: any): boolean => {
  if (isDeltaNode(delta)) {
    return delta.$deltaType === 'DELETE'
      ? payload === undefined
      : isEqual(delta.$newValue, payload);
  }
  if (Array.isArray(delta)) {
    // Deleted elements are not in the payload. Collisions between changed-to values stay ambiguous.
    return delta.every(
      (d) =>
        (isDeltaNode(d) && d.$deltaType === 'DELETE') ||
        (Array.isArray(payload) && payload.some((p) => matchesPayload(d, p)))
    );
  }
  if (typeof delta === 'object' && delta !== null) {
    return Object.entries(delta).every(([key, value]) => matchesPayload(value, payload?.[key]));
  }
  return isEqual(delta, payload);
};

/**
 * The taskDiff drops the identity fields (clusterType, code, name, uuid) of a modified list
 * element, e.g. the AZ whose leader preference changed. The audit payload has the full details after
 * the task, so take them from it: the modified elements are a subsequence of the payload's list, so
 * match them in order by the values they changed to.
 */
export const withIdentityFromPayload = (taskDiff: any, payload: any): any => {
  if (Array.isArray(taskDiff)) {
    if (!Array.isArray(payload)) {
      return taskDiff;
    }
    let cursor = 0;
    return taskDiff.map((element) => {
      if (isDeltaNode(element) || typeof element !== 'object' || element === null) {
        return element;
      }
      const index = payload.findIndex((p, i) => i >= cursor && matchesPayload(element, p));
      if (index < 0) {
        return element;
      }
      cursor = index + 1;
      const identity = pick(payload[index], IDENTITY_FIELDS);
      return { ...identity, ...withIdentityFromPayload(element, payload[index]) };
    });
  }
  if (typeof taskDiff === 'object' && taskDiff !== null && !isDeltaNode(taskDiff)) {
    return mapValues(taskDiff, (value, key) => withIdentityFromPayload(value, payload?.[key]));
  }
  return taskDiff;
};

/**
 * The taskDiff drops an unchanged userIntent.dedicatedNodes, which the master cards need to tell
 * dedicated from colocated masters. An absent value means unchanged, so the payload's value holds
 * on both sides.
 */
export const withDedicatedNodesFromPayload = (taskDiff: any, payload: any): any => {
  const clusters = taskDiff?.clusters;
  const payloadClusters = payload?.clusters;
  if (!Array.isArray(clusters) || !Array.isArray(payloadClusters)) {
    return taskDiff;
  }
  return {
    ...taskDiff,
    clusters: clusters.map((cluster) => {
      const userIntent = cluster?.userIntent;
      if (
        isDeltaNode(cluster) ||
        !userIntent ||
        isDeltaNode(userIntent) ||
        'dedicatedNodes' in userIntent
      ) {
        return cluster;
      }
      const source =
        payloadClusters.find((c) => c?.clusterType === cluster.clusterType) ?? payloadClusters[0];
      const dedicatedNodes = source?.userIntent?.dedicatedNodes;
      return dedicatedNodes === undefined
        ? cluster
        : { ...cluster, userIntent: { ...userIntent, dedicatedNodes } };
    })
  };
};
