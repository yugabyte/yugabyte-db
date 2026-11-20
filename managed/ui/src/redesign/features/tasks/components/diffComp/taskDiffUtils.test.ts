import { mapAuditLogToTaskDiffApiResp } from '../../TaskUtils';
import {
  getTaskDiffValue,
  withDefaultClusterType,
  withIdentityFromPayload,
  withUnchangedIntentFromPayload,
  withoutDeletedPrimary,
  isDeltaNode
} from './taskDiffUtils';

const taskDiff = {
  clusters: [
    {
      clusterType: 'PRIMARY',
      userIntent: {
        instanceType: { $deltaType: 'REPLACE', $oldValue: 'c5.large', $newValue: 'c5.xlarge' }
      },
      placementInfo: {
        cloudList: [
          {
            code: 'aws',
            regionList: [
              {
                code: 'us-west-2',
                azList: [{ $deltaType: 'ADD', $newValue: { name: 'az3', numNodesInAZ: 1 } }]
              }
            ]
          }
        ]
      }
    }
  ]
};

describe('getTaskDiffValue', () => {
  it('rebuilds old and new values and keeps identity fields', () => {
    const before = getTaskDiffValue(taskDiff, 'old');
    const after = getTaskDiffValue(taskDiff, 'new');
    expect(before.clusters[0].userIntent.instanceType).toBe('c5.large');
    expect(after.clusters[0].userIntent.instanceType).toBe('c5.xlarge');
    expect(after.clusters[0].clusterType).toBe('PRIMARY');
    // an added AZ has no old value
    expect(before.clusters[0].placementInfo.cloudList[0].regionList[0].azList).toEqual([]);
    expect(after.clusters[0].placementInfo.cloudList[0].regionList[0].azList[0].name).toBe('az3');
  });

  it('detects delta nodes', () => {
    expect(isDeltaNode({ $deltaType: 'ADD' })).toBe(true);
    expect(isDeltaNode({ a: 1 })).toBe(false);
    expect(isDeltaNode(null)).toBe(false);
  });
});

describe('withIdentityFromPayload', () => {
  const lp = (o: number, n: number) => ({
    leaderPreference: { $deltaType: 'REPLACE', $oldValue: o, $newValue: n }
  });
  const diff = {
    clusters: [
      { placementInfo: { cloudList: [{ regionList: [{ azList: [lp(0, 1), lp(0, 2)] }] }] } }
    ]
  };
  const payload = {
    clusters: [
      {
        clusterType: 'PRIMARY',
        placementInfo: {
          cloudList: [
            {
              code: 'aws',
              regionList: [
                {
                  code: 'us-west-2',
                  azList: [
                    { name: 'az-a', leaderPreference: 1 },
                    { name: 'az-b', leaderPreference: 0 },
                    { name: 'az-c', leaderPreference: 2 }
                  ]
                }
              ]
            }
          ]
        }
      }
    ]
  };

  it('adds the identity fields of the matching payload elements, in order', () => {
    const result = withIdentityFromPayload(diff, payload);
    expect(result.clusters[0].clusterType).toBe('PRIMARY');
    expect(result.clusters[0].placementInfo.cloudList[0].code).toBe('aws');
    const azs = result.clusters[0].placementInfo.cloudList[0].regionList[0].azList;
    expect(azs.map((az: any) => az.name)).toEqual(['az-a', 'az-c']);
  });

  it('leaves the taskDiff as is when the payload has nothing matching', () => {
    expect(withIdentityFromPayload(diff, {})).toEqual(diff);
  });
});

describe('withDefaultClusterType', () => {
  const resize = {
    clusters: [
      {
        userIntent: {
          deviceInfo: { volumeSize: { $deltaType: 'REPLACE', $oldValue: 250, $newValue: 300 } }
        }
      }
    ]
  };

  it('treats a single typeless cluster as primary', () => {
    const after = getTaskDiffValue(withDefaultClusterType(resize), 'new');
    expect(after.clusters[0].clusterType).toBe('PRIMARY');
    expect(after.clusters[0].userIntent.deviceInfo.volumeSize).toBe(300);
  });

  it('only types the first cluster, and leaves typed clusters alone', () => {
    expect(withDefaultClusterType(taskDiff)).toEqual(taskDiff);
    const two = withDefaultClusterType({ clusters: [{ a: 1 }, { b: 2 }] });
    expect(two.clusters).toEqual([{ clusterType: 'PRIMARY', a: 1 }, { b: 2 }]);
  });

  it('gives the one untyped cluster the payload cluster type no other cluster has', () => {
    const payload = { clusters: [{ clusterType: 'PRIMARY' }, { clusterType: 'ASYNC' }] };
    const rr = withDefaultClusterType(
      { clusters: [{ clusterType: 'PRIMARY' }, { b: 2 }] },
      payload
    );
    expect(rr.clusters[1].clusterType).toBe('ASYNC');
  });

  it('types several untyped clusters in target order, primary first', () => {
    const payload = { clusters: [{ clusterType: 'ASYNC' }, { clusterType: 'PRIMARY' }] };
    const out = withDefaultClusterType({ clusters: [{ a: 1 }, { b: 2 }] }, payload);
    expect(out.clusters.map((c: any) => c.clusterType)).toEqual(['PRIMARY', 'ASYNC']);
  });

  it('counts the type of an added cluster as taken', () => {
    const payload = { clusters: [{ clusterType: 'PRIMARY' }, { clusterType: 'ASYNC' }] };
    const out = withDefaultClusterType(
      { clusters: [{ a: 1 }, { $deltaType: 'ADD', $newValue: { clusterType: 'ASYNC' } }] },
      payload
    );
    expect(out.clusters[0].clusterType).toBe('PRIMARY');
  });
});

describe('mapAuditLogToTaskDiffApiResp', () => {
  const base: any = { taskUUID: 't', auditID: 1, payload: { p: 1 }, additionalDetails: { a: 1 } };

  it('uses taskDiff when present', () => {
    const resp = mapAuditLogToTaskDiffApiResp({ ...base, taskDiff });
    expect((resp?.beforeData as any).clusters[0].userIntent.instanceType).toBe('c5.large');
  });

  it('treats an empty taskDiff as absent', () => {
    expect(mapAuditLogToTaskDiffApiResp({ ...base, taskDiff: {} })).toBeUndefined();
  });

  it('keeps the gflags audit entries of a row without a taskDiff', () => {
    const gflags = { master: [{ name: 'a', old: '1', new: '2', default: '' }], tserver: [] };
    const resp = mapAuditLogToTaskDiffApiResp({ ...base, additionalDetails: { gflags } });
    expect(resp?.gflagsAudit).toEqual(gflags);
  });

  it('returns nothing without a taskDiff', () => {
    expect(mapAuditLogToTaskDiffApiResp(base)).toBeUndefined();
  });
});

describe('identity matching with several regions', () => {
  it('does not give a region the identity of the first payload region', () => {
    const diff = {
      clusters: [
        {
          placementInfo: {
            cloudList: [
              {
                regionList: [
                  {
                    azList: [
                      { numNodesInAZ: { $deltaType: 'REPLACE', $oldValue: 1, $newValue: 2 } }
                    ]
                  }
                ]
              }
            ]
          }
        }
      ]
    };
    const az = (name: string, n: number) => ({ name, numNodesInAZ: n });
    const payload = {
      clusters: [
        {
          clusterType: 'PRIMARY',
          placementInfo: {
            cloudList: [
              {
                regionList: [
                  { code: 'us-west-2', azList: [az('w-a', 1)] },
                  { code: 'us-east-1', azList: [az('e-a', 2)] }
                ]
              }
            ]
          }
        }
      ]
    };
    const out = withIdentityFromPayload(diff, payload);
    const region = out.clusters[0].placementInfo.cloudList[0].regionList[0];
    expect(region.code).toBe('us-east-1');
    expect(region.azList[0].name).toBe('e-a');
  });
});

describe('withUnchangedIntentFromPayload', () => {
  it('restores the unchanged dedicatedNodes and providerType on the modified userIntent', () => {
    const out = withUnchangedIntentFromPayload(
      {
        clusters: [
          {
            clusterType: 'PRIMARY',
            userIntent: {
              masterInstanceType: { $deltaType: 'REPLACE', $oldValue: 'a', $newValue: 'b' }
            }
          }
        ]
      },
      {
        clusters: [
          {
            clusterType: 'PRIMARY',
            userIntent: { dedicatedNodes: true, providerType: 'kubernetes' }
          }
        ]
      }
    );
    expect(getTaskDiffValue(out, 'old').clusters[0].userIntent.dedicatedNodes).toBe(true);
    expect(getTaskDiffValue(out, 'new').clusters[0].userIntent.dedicatedNodes).toBe(true);
    expect(getTaskDiffValue(out, 'new').clusters[0].userIntent.providerType).toBe('kubernetes');
  });
});

describe('ambiguous identity', () => {
  it('gives no identity when several payload elements match', () => {
    const lp = { leaderPreference: { $deltaType: 'REPLACE', $oldValue: 0, $newValue: 1 } };
    const out = withIdentityFromPayload(
      [lp],
      [
        { name: 'az-a', leaderPreference: 1 },
        { name: 'az-b', leaderPreference: 1 }
      ]
    );
    expect(out[0].name).toBeUndefined();
  });
});

describe('identity with task-internal fields', () => {
  it('ignores a changed value the payload lacks, e.g. masterMoveStage', () => {
    const diff = {
      clusters: [
        {
          masterMoveStage: { $deltaType: 'ADD', $newValue: 'STARTED' },
          userIntent: { numNodes: { $deltaType: 'REPLACE', $oldValue: 3, $newValue: 4 } }
        }
      ]
    };
    const out = withIdentityFromPayload(diff, {
      clusters: [{ clusterType: 'PRIMARY', uuid: 'u1', userIntent: { numNodes: 4 } }]
    });
    expect(out.clusters[0].uuid).toBe('u1');
  });

  it('breaks a cluster tie by the values the element changed from', () => {
    const diff = {
      clusters: [{ userIntent: { volume: { $deltaType: 'REPLACE', $oldValue: 1, $newValue: 5 } } }]
    };
    const payload = {
      clusters: [
        { clusterType: 'PRIMARY', uuid: 'p', userIntent: { volume: 5 } },
        { clusterType: 'ASYNC', uuid: 'r', userIntent: { volume: 5 } }
      ]
    };
    const before = {
      clusters: [
        { uuid: 'p', userIntent: { volume: 5 } },
        { uuid: 'r', userIntent: { volume: 1 } }
      ]
    };
    expect(withIdentityFromPayload(diff, payload, before).clusters[0].clusterType).toBe('ASYNC');
    // nothing settles the tie without the state before the task
    expect(withIdentityFromPayload(diff, payload).clusters[0].clusterType).toBeUndefined();
  });

  it('leaves a single untyped cluster untyped when the payload has more cluster types', () => {
    const payload = { clusters: [{ clusterType: 'PRIMARY' }, { clusterType: 'ASYNC' }] };
    const out = withDefaultClusterType({ clusters: [{ a: 1 }] }, payload);
    expect(out.clusters[0].clusterType).toBeUndefined();
  });
});

describe('withoutDeletedPrimary', () => {
  const deleted = { $deltaType: 'DELETE', $oldValue: { clusterType: 'PRIMARY', uuid: 'p' } };
  const rr = { userIntent: { x: 1 } };
  it('drops the deleted primary when the payload has no primary', () => {
    const out = withoutDeletedPrimary(
      { clusters: [deleted, rr] },
      { clusters: [{ clusterType: 'ASYNC' }] }
    );
    expect(out.clusters).toEqual([rr]);
  });
  it('keeps it when the payload has a primary', () => {
    const diff = { clusters: [deleted] };
    expect(
      withoutDeletedPrimary(diff, {
        clusters: [{ clusterType: 'PRIMARY' }, { clusterType: 'ASYNC' }]
      })
    ).toBe(diff);
  });
});

describe('cluster identity when the payload lists the read replica first', () => {
  it('matches each cluster on its own', () => {
    const d = (o: number, n: number) => ({
      userIntent: { volume: { $deltaType: 'REPLACE', $oldValue: o, $newValue: n } }
    });
    const out = withIdentityFromPayload(
      { clusters: [d(1, 2), d(3, 4)] },
      {
        clusters: [
          { clusterType: 'ASYNC', uuid: 'r', userIntent: { volume: 4 } },
          { clusterType: 'PRIMARY', uuid: 'p', userIntent: { volume: 2 } }
        ]
      }
    );
    expect(out.clusters.map((c: any) => c.clusterType)).toEqual(['PRIMARY', 'ASYNC']);
  });

  it('ignores gflag fields the task rewrites', () => {
    const diff = {
      clusters: [
        {
          userIntent: {
            masterGFlags: { $deltaType: 'REPLACE', $oldValue: { a: '1' }, $newValue: { a: '2' } },
            numNodes: { $deltaType: 'REPLACE', $oldValue: 3, $newValue: 4 }
          }
        }
      ]
    };
    const out = withIdentityFromPayload(diff, {
      clusters: [{ clusterType: 'PRIMARY', userIntent: { masterGFlags: { a: '1' }, numNodes: 4 } }]
    });
    expect(out.clusters[0].clusterType).toBe('PRIMARY');
  });
});
