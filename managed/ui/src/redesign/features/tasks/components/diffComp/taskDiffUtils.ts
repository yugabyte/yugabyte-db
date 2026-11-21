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
 * The backend drops the unchanged clusterType from a modified cluster. A cluster that is still
 * without one gets a cluster type of the payload that no other cluster of the taskDiff has; without
 * a payload, the first cluster is the primary one.
 */
export const withDefaultClusterType = (
  taskDiff: Record<string, any>,
  payload?: any
): Record<string, any> => {
  const clusters = taskDiff?.clusters;
  if (!Array.isArray(clusters) || clusters.length === 0) {
    return taskDiff;
  }
  // A request without cluster types (e.g. the v2 resize body) tells nothing about the clusters.
  if (!payload?.clusters?.some((c: any) => c?.clusterType)) {
    payload = undefined;
  }
  // An added cluster is in the target, so its type is taken; a deleted one is not.
  const taken = clusters.map((c) =>
    isDeltaNode(c)
      ? c.$deltaType === 'ADD'
        ? (c.$newValue as any)?.clusterType
        : undefined
      : c?.clusterType
  );
  const available: string[] = (
    Array.isArray(payload?.clusters)
      ? payload.clusters.map((c: any) => c?.clusterType)
      : ['PRIMARY']
  ).filter((type: string | undefined) => type && !taken.includes(type));
  const untyped = clusters.filter((c) => !isDeltaNode(c) && !c?.clusterType).length;
  // More payload types than untyped clusters (e.g. only the read replica changed, and the payload
  // has the primary too): the type cannot be told, so the cluster stays untyped.
  const decidable = !payload || available.length <= untyped;
  // The taskDiff follows the target's order, which is primary first, not the request's order.
  available.sort((a, b) => Number(b === 'PRIMARY') - Number(a === 'PRIMARY'));
  return {
    ...taskDiff,
    clusters: clusters.map((cluster, i) => {
      if (isDeltaNode(cluster) || cluster?.clusterType || !decidable) {
        return cluster;
      }
      const clusterType = available.shift() ?? (i === 0 ? 'PRIMARY' : undefined);
      return clusterType ? { clusterType, ...cluster } : cluster;
    })
  };
};

const IDENTITY_FIELDS = ['clusterType', 'code', 'name', 'uuid'];

const UNVERIFIABLE_FIELDS = ['masterGFlags', 'tserverGFlags', 'specificGFlags'];

// How many of the values the delta element changed to the payload element has, or null if one
// differs. The taskDiff is taken from the task params at freeze, not the request, so a value or list
// the payload lacks (e.g. masterMoveStage) counts for nothing, and so do the gflag fields, which a
// task rewrites with the effective flags.
const matchScore = (delta: any, payload: any, side: 'old' | 'new' = 'new'): number | null => {
  if (isDeltaNode(delta)) {
    const missing = side === 'new' ? 'DELETE' : 'ADD';
    if (delta.$deltaType === missing || payload === undefined) {
      return 0;
    }
    return isEqual(side === 'new' ? delta.$newValue : delta.$oldValue, payload) ? 1 : null;
  }
  if (Array.isArray(delta)) {
    // Deleted elements are not in the payload.
    let total = 0;
    for (const d of delta) {
      if (isDeltaNode(d) && d.$deltaType === (side === 'new' ? 'DELETE' : 'ADD')) continue;
      if (payload === undefined) return 0;
      const scores = (Array.isArray(payload) ? payload : [])
        .map((p) => matchScore(d, p, side))
        .filter((score): score is number => score !== null);
      if (scores.length === 0) return null;
      total += Math.max(...scores);
    }
    return total;
  }
  if (typeof delta === 'object' && delta !== null) {
    let total = 0;
    for (const [key, value] of Object.entries(delta)) {
      if (UNVERIFIABLE_FIELDS.includes(key)) continue;
      const score = matchScore(value, payload?.[key], side);
      if (score === null) return null;
      total += score;
    }
    return total;
  }
  if (payload === undefined) return 0;
  return isEqual(delta, payload) ? 1 : null;
};

/**
 * The taskDiff drops the identity fields (clusterType, code, name, uuid) of a modified list
 * element, e.g. the AZ whose leader preference changed. The audit payload is the request, which
 * is the raw request body, which has the identity fields, so take them from it. Modified AZs, regions and clouds keep their order in
 * the request, so they are matched in order by the values they changed to. Clusters are not: the
 * request can list them in any order, so each one is matched on its own.
 */
export const withIdentityFromPayload = (
  taskDiff: any,
  payload: any,
  before?: any,
  isClusters = false
): any => {
  if (Array.isArray(taskDiff)) {
    if (!Array.isArray(payload)) {
      return taskDiff;
    }
    let cursor = 0;
    return taskDiff.map((element) => {
      if (isDeltaNode(element) || typeof element !== 'object' || element === null) {
        return element;
      }
      const scored = payload
        .map((p, i) => ({ i, score: isClusters || i >= cursor ? matchScore(element, p) : null }))
        .filter(({ score }) => score !== null && score >= 1) as { i: number; score: number }[];
      const best = Math.max(0, ...scored.map(({ score }) => score));
      let candidates = scored.filter(({ score }) => score === best);
      const beforeOf = (i: number) =>
        Array.isArray(before)
          ? (before.find((b) => b?.uuid && b.uuid === payload[i]?.uuid) ?? before[i])
          : undefined;
      if (candidates.length > 1 && isClusters) {
        // The taskDiff only has the changed clusters, so order cannot settle a tie: the values
        // the element changed from must be those of the cluster before the task.
        const fromScores = candidates.map((c) => matchScore(element, beforeOf(c.i), 'old'));
        candidates = candidates.filter((_, k) => fromScores[k] !== null && fromScores[k]! >= 1);
      }
      // Equal candidates (e.g. equal leader preferences) cannot be told apart: leave the element
      // without identity rather than pick the wrong one.
      if (candidates.length !== 1) {
        return element;
      }
      const index = candidates[0].i;
      if (!isClusters) cursor = index + 1;
      const identity = pick(payload[index], IDENTITY_FIELDS);
      return {
        ...identity,
        ...withIdentityFromPayload(element, payload[index], beforeOf(index))
      };
    });
  }
  if (typeof taskDiff === 'object' && taskDiff !== null && !isDeltaNode(taskDiff)) {
    return mapValues(taskDiff, (value, key) =>
      withIdentityFromPayload(value, payload?.[key], before?.[key], key === 'clusters')
    );
  }
  return taskDiff;
};

/**
 * The taskDiff drops unchanged userIntent fields, but the master cards need dedicatedNodes and the
 * K8s override card needs providerType. An absent value means unchanged, so the payload's value
 * holds on both sides.
 */
export const withUnchangedIntentFromPayload = (taskDiff: any, payload: any): any => {
  const clusters = taskDiff?.clusters;
  const payloadClusters = payload?.clusters;
  if (!Array.isArray(clusters) || !Array.isArray(payloadClusters)) {
    return taskDiff;
  }
  return {
    ...taskDiff,
    clusters: clusters.map((cluster) => {
      const userIntent = cluster?.userIntent;
      if (isDeltaNode(cluster) || !userIntent || isDeltaNode(userIntent)) {
        return cluster;
      }
      const source =
        payloadClusters.find((c) => c?.clusterType === cluster.clusterType) ?? payloadClusters[0];
      const restored = pick(source?.userIntent, ['dedicatedNodes', 'providerType']);
      // The AZ overrides card names the AZs from placementInfo, which the delta drops when unchanged.
      const placementInfo = cluster.placementInfo ?? source?.placementInfo;
      return {
        ...cluster,
        ...(placementInfo ? { placementInfo } : {}),
        userIntent: { ...restored, ...userIntent }
      };
    })
  };
};

/**
 * Edit and Resize never remove a cluster, but the target of a task that submits only one cluster
 * (a read replica edit, a primary resize) lacks the other, so the delta marks it as deleted. Drop
 * those nodes.
 */
export const withoutDeletedClusters = (taskDiff: any): any => {
  const clusters = taskDiff?.clusters;
  if (!Array.isArray(clusters)) {
    return taskDiff;
  }
  return {
    ...taskDiff,
    clusters: clusters.filter((c) => !(isDeltaNode(c) && c.$deltaType === 'DELETE'))
  };
};
