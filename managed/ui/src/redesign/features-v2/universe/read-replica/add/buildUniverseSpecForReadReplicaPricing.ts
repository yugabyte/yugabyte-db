import _ from 'lodash';
import {
  ClusterAddReqBody,
  ClusterSpec,
  ClusterSpecClusterType,
  Universe,
  UniverseCreateReqBody,
  UniverseCreateSpecArch,
  UniverseSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { AddRRContextProps } from './AddReadReplicaContext';
import { getClusterByType } from '../../edit-universe/EditUniverseUtils';
import {
  mapAddReadReplicaClusterPayload,
  mapEditReadReplicaClusterSpec
} from './addReadReplicaClusterPayload';
import { getReadOnlyCluster } from '@app/redesign/utils/universeUtils';

type JsonRecord = Record<string, unknown>;

function isRecord(value: unknown): value is JsonRecord {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function sanitizeStringMap(value: unknown): Record<string, string> | undefined {
  if (!isRecord(value)) {
    return undefined;
  }
  const entries = Object.entries(value).filter(([, v]) => v !== null && v !== undefined);
  return Object.fromEntries(entries.map(([k, v]) => [k, String(v)]));
}

function sanitizeAzGFlags(value: unknown): JsonRecord | undefined {
  if (!isRecord(value)) {
    return undefined;
  }
  const out: JsonRecord = {};
  Object.entries(value).forEach(([azUuid, entry]) => {
    if (!isRecord(entry)) return;
    const master = sanitizeStringMap(entry.master);
    const tserver = sanitizeStringMap(entry.tserver);
    const sanitizedEntry: JsonRecord = {};
    if (master !== undefined) {
      sanitizedEntry.master = master;
    }
    if (tserver !== undefined) {
      sanitizedEntry.tserver = tserver;
    }
    if (Object.keys(sanitizedEntry).length) {
      out[azUuid] = sanitizedEntry;
    }
  });
  return Object.keys(out).length ? out : undefined;
}

function sanitizeClusterGFlags(value: unknown): JsonRecord | undefined {
  if (!isRecord(value)) {
    return undefined;
  }
  const out: JsonRecord = {};
  const master = sanitizeStringMap(value.master);
  const tserver = sanitizeStringMap(value.tserver);
  const azGflags = sanitizeAzGFlags(value.az_gflags);
  if (master !== undefined) {
    out.master = master;
  }
  if (tserver !== undefined) {
    out.tserver = tserver;
  }
  if (azGflags !== undefined) {
    out.az_gflags = azGflags;
  }
  if (Array.isArray(value.gflag_groups) && value.gflag_groups.length) {
    out.gflag_groups = value.gflag_groups;
  }
  return Object.keys(out).length ? out : undefined;
}

function sanitizeClusterForPricing(cluster: ClusterSpec): ClusterSpec {
  const sanitized = _.cloneDeep(cluster) as ClusterSpec & { gflags?: unknown };
  const cleanedGFlags = sanitizeClusterGFlags(sanitized.gflags);
  if (cleanedGFlags) {
    sanitized.gflags = cleanedGFlags as ClusterSpec['gflags'];
  } else {
    delete sanitized.gflags;
  }
  return sanitized;
}

export function sanitizeClusters(clusters: ClusterSpec[] | undefined): ClusterSpec[] {
  return (clusters ?? []).map((cluster) => sanitizeClusterForPricing(cluster));
}

function replaceClusterByUuid(
  clusters: ClusterSpec[],
  clusterUuid: string,
  updater: (cluster: ClusterSpec) => ClusterSpec
): ClusterSpec[] {
  return clusters.map((cluster) => (cluster.uuid === clusterUuid ? updater(cluster) : cluster));
}

function buildPricingSpecFromClusters(
  universeData: Universe,
  clusters: ClusterSpec[]
): UniverseCreateReqBody {
  return {
    spec: {
      ..._.cloneDeep(universeData!.spec),
      clusters
    } as UniverseSpec,
    arch: universeData!.info!.arch as UniverseCreateSpecArch
  };
}

/**
 * Stable key for read-replica pricing: when this changes, generate a new proposed
 * async cluster UUID so fetch-universe-resources is refetched without unstable query keys.
 */
export function getReadReplicaPricingFingerprint(
  context: AddRRContextProps,
  providerRegions: Region[]
): string {
  const clusterUuids = (context.universeData?.spec?.clusters ?? [])
    .map((c) => c.uuid)
    .filter((id): id is string => Boolean(id))
    .sort()
    .join(',');
  try {
    const existingReadReplica = getReadOnlyCluster(context.universeData?.spec?.clusters ?? []);
    if (existingReadReplica?.uuid) {
      const edit = mapEditReadReplicaClusterSpec(existingReadReplica.uuid, context, providerRegions);
      return `${context.universeUuid ?? ''}\u0001${clusterUuids}\u0001edit\u0001${JSON.stringify(edit)}`;
    }
    const add = mapAddReadReplicaClusterPayload(context, providerRegions);
    return `${context.universeUuid ?? ''}\u0001${clusterUuids}\u0001add\u0001${JSON.stringify(add)}`;
  } catch {
    return `${context.universeUuid ?? ''}\u0001${clusterUuids}\u0001pending`;
  }
}

/**
 * Builds a create-time universe spec for POST /fetch-universe-resources so pricing
 * reflects the current universe plus the read-replica cluster being added.
 *
 */
function clusterSpecFromAddPayload(
  add: ClusterAddReqBody,
  primary: ClusterSpec | undefined,
  proposedAsyncClusterUuid: string,
  primaryProviderSpec: NonNullable<ClusterSpec['provider_spec']>
): ClusterSpec {
  const provider_spec = {
    ..._.cloneDeep(primaryProviderSpec),
    region_list: add.provider_spec.region_list
  };

  const placementFromPartitions =
    add.partitions_spec?.find((p) => p.default_partition) ?? add.partitions_spec?.[0];
  const placement_spec = placementFromPartitions?.placement ?? add.placement_spec;

  return sanitizeClusterForPricing({
    uuid: proposedAsyncClusterUuid,
    cluster_type: ClusterSpecClusterType.ASYNC,
    num_nodes: add.num_nodes,
    replication_factor: add.num_nodes,
    node_spec: add.node_spec,
    provider_spec,
    ...(placement_spec ? { placement_spec } : {}),
    ...(add.partitions_spec ? { partitions_spec: add.partitions_spec } : {}),
    gflags: add.gflags,
    ...(add.instance_tags ? { instance_tags: add.instance_tags } : {})
  });
}

/** Current universe spec only (no proposed read replica) — for primary-row pricing. */
export function buildUniverseSpecCurrentStatePricing(
  universeData: Universe | undefined
): UniverseCreateReqBody | undefined {
  if (!universeData?.spec || !universeData.info?.arch) {
    return undefined;
  }
  return buildPricingSpecFromClusters(universeData, sanitizeClusters(universeData.spec.clusters));
}

export function buildUniverseSpecForReadReplicaPricing(
  context: AddRRContextProps,
  providerRegions: Region[],
  proposedAsyncClusterUuid: string
): UniverseCreateReqBody | undefined {
  const { universeData } = context;
  if (!universeData?.spec || !universeData.info?.arch) {
    return undefined;
  }

  const existingReadReplica = getReadOnlyCluster(universeData.spec?.clusters ?? []);

  const primary = getClusterByType(universeData as Universe, ClusterSpecClusterType.PRIMARY);
  const primaryProviderSpec = primary?.provider_spec;
  if (!primaryProviderSpec?.provider) {
    return undefined;
  }

  if (existingReadReplica?.uuid) {
    let editSpec: ReturnType<typeof mapEditReadReplicaClusterSpec>;
    try {
      editSpec = mapEditReadReplicaClusterSpec(existingReadReplica.uuid, context, providerRegions);
    } catch {
      return undefined;
    }
    const existingClusters = sanitizeClusters(universeData.spec.clusters);
    const updatedClusters = replaceClusterByUuid(
      existingClusters,
      existingReadReplica.uuid,
      (cluster) => {
      const mergedProviderSpec = {
        ...(cluster.provider_spec ?? {}),
        ...(editSpec.provider_spec ?? {})
      };
      const placementFromEditPartitions =
        editSpec.partitions_spec?.find((p) => p.default_partition) ??
        editSpec.partitions_spec?.[0];
      const mirroredPlacement =
        placementFromEditPartitions?.placement ?? editSpec.placement_spec;

      return sanitizeClusterForPricing({
        ...cluster,
        ...(editSpec.num_nodes !== undefined ? { num_nodes: editSpec.num_nodes } : {}),
        ...(editSpec.node_spec ? { node_spec: editSpec.node_spec } : {}),
        ...(editSpec.partitions_spec ? { partitions_spec: editSpec.partitions_spec } : {}),
        ...(mirroredPlacement ? { placement_spec: mirroredPlacement } : {}),
        provider_spec: mergedProviderSpec
      });
      }
    );

    return buildPricingSpecFromClusters(universeData, updatedClusters);
  }

  let add: ClusterAddReqBody;
  try {
    add = mapAddReadReplicaClusterPayload(context, providerRegions);
  } catch {
    return undefined;
  }

  const newAsyncCluster = clusterSpecFromAddPayload(
    add,
    primary,
    proposedAsyncClusterUuid,
    primaryProviderSpec
  );
  const existingClusters = sanitizeClusters(universeData.spec.clusters);

  return buildPricingSpecFromClusters(universeData, [...existingClusters, newAsyncCluster]);
}
