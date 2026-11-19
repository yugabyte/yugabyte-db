import { mapAuditLogToTaskDiffApiResp } from '../../TaskUtils';
import {
  getTaskDiffValue,
  withDefaultClusterType,
  withIdentityFromPayload,
  withDedicatedNodesFromPayload,
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
    expect(withDefaultClusterType(taskDiff)).toBe(taskDiff);
    const two = withDefaultClusterType({ clusters: [{ a: 1 }, { b: 2 }] });
    expect(two.clusters).toEqual([{ clusterType: 'PRIMARY', a: 1 }, { b: 2 }]);
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

describe('withDedicatedNodesFromPayload', () => {
  it('restores the unchanged dedicatedNodes on the modified userIntent', () => {
    const out = withDedicatedNodesFromPayload(
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
      { clusters: [{ clusterType: 'PRIMARY', userIntent: { dedicatedNodes: true } }] }
    );
    expect(getTaskDiffValue(out, 'old').clusters[0].userIntent.dedicatedNodes).toBe(true);
    expect(getTaskDiffValue(out, 'new').clusters[0].userIntent.dedicatedNodes).toBe(true);
  });
});
