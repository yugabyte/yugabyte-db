import { describe, expect, it } from 'vitest';
import {
  NodeDetailsDedicatedTo,
  Universe
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  hasDedicatedNodes,
  placementSpecToAvailabilityZones
} from './EditUniverseUtils';
import {
  FIXTURE_AZ_1_UUID,
  FIXTURE_AZ_2_UUID,
  FIXTURE_REGION_CODE,
  makePrimaryPlacementSpec
} from './__fixtures__/editUniverseFixtures';

describe('hasDedicatedNodes', () => {
  it('returns false when node_details_set is empty', () => {
    const u = {
      info: { node_details_set: [] }
    } as Universe;
    expect(hasDedicatedNodes(u)).toBe(false);
  });

  it('returns false when node_details_set is undefined', () => {
    const u = { info: {} } as Universe;
    expect(hasDedicatedNodes(u)).toBe(false);
  });

  it('returns false when no node has dedicated_to', () => {
    const u = {
      info: {
        node_details_set: [{ node_uuid: 'a', az_uuid: FIXTURE_AZ_1_UUID }]
      }
    } as Universe;
    expect(hasDedicatedNodes(u)).toBe(false);
  });

  it('returns true when any node has dedicated_to', () => {
    const u = {
      info: {
        node_details_set: [
          { node_uuid: 'a', az_uuid: FIXTURE_AZ_1_UUID, dedicated_to: NodeDetailsDedicatedTo.TSERVER }
        ]
      }
    } as Universe;
    expect(hasDedicatedNodes(u)).toBe(true);
  });
});

describe('placementSpecToAvailabilityZones', () => {
  it('maps region code to AZ rows with node counts and leader preference offset', () => {
    const spec = makePrimaryPlacementSpec();
    const firstRegion = spec.cloud_list[0].region_list![0];
    firstRegion.az_list![0].leader_preference = 2;

    const zones = placementSpecToAvailabilityZones(spec);
    expect(Object.keys(zones)).toEqual([FIXTURE_REGION_CODE]);
    expect(zones[FIXTURE_REGION_CODE]).toHaveLength(2);
    expect(zones[FIXTURE_REGION_CODE][0]).toMatchObject({
      uuid: FIXTURE_AZ_1_UUID,
      nodeCount: 1,
      preffered: 1
    });
    expect(zones[FIXTURE_REGION_CODE][1]).toMatchObject({
      uuid: FIXTURE_AZ_2_UUID,
      nodeCount: 1,
      preffered: -1
    });
  });
});
