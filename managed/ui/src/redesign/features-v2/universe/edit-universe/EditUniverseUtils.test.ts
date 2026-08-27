import { describe, expect, it } from 'vitest';
import {
  ClusterSpecClusterType,
  NodeDetailsDedicatedTo,
  Universe
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  hasDedicatedNodesForCluster,
  placementSpecToAvailabilityZones
} from './EditUniverseUtils';
import {
  FIXTURE_AZ_1_UUID,
  FIXTURE_AZ_2_UUID,
  FIXTURE_ASYNC_CLUSTER_UUID,
  FIXTURE_PRIMARY_CLUSTER_UUID,
  FIXTURE_REGION_CODE,
  makeNonGeoUniverse,
  makeNonGeoUniverseWithReadReplicaPlacementSpec,
  makePrimaryPlacementSpec
} from './__fixtures__/editUniverseFixtures';

describe('hasDedicatedNodesForCluster', () => {
  it('returns false when cluster is undefined', () => {
    const u = {
      info: { node_details_set: [] }
    } as Universe;
    expect(hasDedicatedNodesForCluster(u)).toBe(false);
  });

  it('returns false when node_details_set is empty', () => {
    const u = makeNonGeoUniverse();
    expect(hasDedicatedNodesForCluster(u, u.spec!.clusters[0])).toBe(false);
  });

  it('returns false when no node has dedicated_to', () => {
    const u = makeNonGeoUniverse();
    u.info = {
      ...u.info,
      node_details_set: [{ node_uuid: 'a', az_uuid: FIXTURE_AZ_1_UUID }]
    };
    expect(hasDedicatedNodesForCluster(u, u.spec!.clusters[0])).toBe(false);
  });

  it('returns true when any node has dedicated_to', () => {
    const u = makeNonGeoUniverse();
    u.info = {
      ...u.info,
      node_details_set: [
        {
          node_uuid: 'a',
          az_uuid: FIXTURE_AZ_1_UUID,
          dedicated_to: NodeDetailsDedicatedTo.TSERVER,
          placement_uuid: FIXTURE_PRIMARY_CLUSTER_UUID
        }
      ]
    };
    expect(hasDedicatedNodesForCluster(u, u.spec!.clusters[0])).toBe(true);
  });

  it('returns true when cluster node_spec has dedicated_nodes', () => {
    const u = makeNonGeoUniverse();
    const primary = u.spec!.clusters[0];
    primary.node_spec = { ...primary.node_spec, dedicated_nodes: true };
    expect(hasDedicatedNodesForCluster(u, primary)).toBe(true);
  });

  it('returns true for primary when node_details lack placement_uuid (legacy)', () => {
    const u = {
      info: {
        node_details_set: [
          {
            node_uuid: 'a',
            az_uuid: FIXTURE_AZ_1_UUID,
            dedicated_to: NodeDetailsDedicatedTo.TSERVER
          }
        ]
      },
      spec: {
        clusters: [
          {
            uuid: FIXTURE_PRIMARY_CLUSTER_UUID,
            cluster_type: ClusterSpecClusterType.PRIMARY
          }
        ]
      }
    } as Universe;
    expect(hasDedicatedNodesForCluster(u, u.spec!.clusters[0])).toBe(true);
  });

  it('returns false for primary when only read replica nodes are dedicated', () => {
    const u = makeNonGeoUniverseWithReadReplicaPlacementSpec();
    const readReplica = u.spec!.clusters[1];
    readReplica.node_spec = { ...readReplica.node_spec, dedicated_nodes: true };
    u.info = {
      ...u.info,
      node_details_set: [
        {
          node_uuid: 'rr1',
          az_uuid: FIXTURE_AZ_1_UUID,
          dedicated_to: NodeDetailsDedicatedTo.TSERVER,
          placement_uuid: FIXTURE_ASYNC_CLUSTER_UUID
        }
      ]
    };
    expect(hasDedicatedNodesForCluster(u, u.spec!.clusters[0])).toBe(false);
    expect(hasDedicatedNodesForCluster(u, readReplica)).toBe(true);
  });
});

describe('placementSpecToAvailabilityZones', () => {
  it('maps region code to AZ rows with node counts and leader preference', () => {
    const spec = makePrimaryPlacementSpec();
    const firstRegion = spec.cloud_list[0].region_list![0];
    firstRegion.az_list![0].leader_preference = 2;

    const zones = placementSpecToAvailabilityZones(spec);
    expect(Object.keys(zones)).toEqual([FIXTURE_REGION_CODE]);
    expect(zones[FIXTURE_REGION_CODE]).toHaveLength(2);
    expect(zones[FIXTURE_REGION_CODE][0]).toMatchObject({
      uuid: FIXTURE_AZ_1_UUID,
      nodeCount: 1,
      preffered: 2
    });
    expect(zones[FIXTURE_REGION_CODE][1]).toMatchObject({
      uuid: FIXTURE_AZ_2_UUID,
      nodeCount: 1,
      preffered: 0
    });
  });
});
