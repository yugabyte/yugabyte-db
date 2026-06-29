import * as Yup from 'yup';
import { OnPremNodeState } from '../../../../../../helpers/dtos';
import { ProviderNode } from '../../../../../../utils/dtos';
import {
  computeFreeNodesByAzUuid,
  findFirstOnPremNodeViolation,
  validateOnPremFreeNodesPerAz
} from './onPrem';

const createError = (opts: { path: string; message?: string }) =>
  new Yup.ValidationError(opts.message ?? '', undefined, opts.path);

const makeNode = (
  zoneUuid: string,
  state: OnPremNodeState = OnPremNodeState.FREE
): ProviderNode => ({
  details: {
    instanceName: 'n0',
    instanceType: 'c5.large',
    ip: '10.0.0.1',
    nodeName: '',
    region: 'us-west-2',
    sshUser: 'ec2-user',
    zone: 'west2-1'
  },
  detailsJson: '{}',
  inUse: false,
  instanceName: 'n0',
  instanceTypeCode: 'c5.large',
  nodeName: '',
  nodeUuid: `node-${zoneUuid}-${state}`,
  state,
  zoneUuid
});

describe('computeFreeNodesByAzUuid', () => {
  it('counts FREE nodes per zoneUuid', () => {
    const counts = computeFreeNodesByAzUuid([
      makeNode('az-1', OnPremNodeState.FREE),
      makeNode('az-1', OnPremNodeState.FREE),
      makeNode('az-2', OnPremNodeState.FREE),
      makeNode('az-1', OnPremNodeState.USED),
      makeNode('az-2', OnPremNodeState.DECOMMISSIONED)
    ]);

    expect(counts).toEqual({ 'az-1': 2, 'az-2': 1 });
  });

  it('ignores nodes without FREE state', () => {
    expect(
      computeFreeNodesByAzUuid([
        makeNode('az-1', OnPremNodeState.USED),
        { ...makeNode('az-1'), state: undefined }
      ])
    ).toEqual({});
  });
});

describe('findFirstOnPremNodeViolation', () => {
  it('returns null when nodeCount is within free capacity', () => {
    expect(
      findFirstOnPremNodeViolation(
        {
          r0: [{ name: 'Z0', uuid: 'az-1', nodeCount: 2, preffered: 1 }]
        },
        { 'az-1': 2 }
      )
    ).toBeNull();
  });

  it('returns violation when nodeCount exceeds free capacity', () => {
    expect(
      findFirstOnPremNodeViolation(
        {
          r0: [{ name: 'Z0', uuid: 'az-1', nodeCount: 3, preffered: 1 }]
        },
        { 'az-1': 2 }
      )
    ).toEqual({
      az_name: 'Z0',
      requested_count: 3,
      free_count: 2,
      region_code: 'r0',
      zone_index: 0
    });
  });

  it('skips zones without uuid or with zero nodeCount', () => {
    expect(
      findFirstOnPremNodeViolation(
        {
          r0: [
            { name: '', uuid: '', nodeCount: 5, preffered: 0 },
            { name: 'Z1', uuid: 'az-1', nodeCount: 0, preffered: 0 }
          ]
        },
        { 'az-1': 0 }
      )
    ).toBeNull();
  });
});

describe('validateOnPremFreeNodesPerAz', () => {
  const availabilityZones = {
    r0: [{ name: 'Z0', uuid: 'az-1', nodeCount: 3, preffered: 1 }]
  };

  it('skips validation when not on-prem', () => {
    expect(
      validateOnPremFreeNodesPerAz({
        availabilityZones,
        path: 'lesserNodes',
        createError,
        onPremContext: {
          isOnPrem: false,
          freeNodesByAzUuid: {},
          isOnPremNodesLoaded: true
        }
      })
    ).toEqual([]);
  });

  it('returns loading error when on-prem nodes are not loaded', () => {
    const errors = validateOnPremFreeNodesPerAz({
      availabilityZones,
      path: 'lesserNodes',
      createError,
      onPremContext: {
        isOnPrem: true,
        freeNodesByAzUuid: {},
        isOnPremNodesLoaded: false
      }
    });

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toBe('errMsg.onPremNodesLoading');
  });

  it('returns free-node exceeded error when requested count is too high', () => {
    const errors = validateOnPremFreeNodesPerAz({
      availabilityZones,
      path: 'lesserNodes',
      createError,
      onPremContext: {
        isOnPrem: true,
        freeNodesByAzUuid: { 'az-1': 1 },
        isOnPremNodesLoaded: true
      }
    });

    expect(errors).toHaveLength(2);
    expect(errors[0].message).toBe('errMsg.onPremFreeNodesExceeded');
    expect(errors[1].path).toBe('availabilityZones.r0.0.nodeCount');
  });

  it('passes when requested count fits free capacity', () => {
    expect(
      validateOnPremFreeNodesPerAz({
        availabilityZones: {
          r0: [{ name: 'Z0', uuid: 'az-1', nodeCount: 2, preffered: 1 }]
        },
        path: 'lesserNodes',
        createError,
        onPremContext: {
          isOnPrem: true,
          freeNodesByAzUuid: { 'az-1': 2 },
          isOnPremNodesLoaded: true
        }
      })
    ).toEqual([]);
  });
});
