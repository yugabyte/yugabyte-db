import { UniverseDiff } from './UniverseDiff';
import { getTaskDiffValue, withDefaultClusterType } from '../taskDiffUtils';

// A resize `taskDiff`: only the changed fields, so most of the cluster is absent.
const resizeTaskDiff = withDefaultClusterType({
  clusters: [
    {
      userIntent: {
        deviceInfo: { volumeSize: { $deltaType: 'REPLACE', $oldValue: 250, $newValue: 300 } }
      }
    }
  ]
});

describe('UniverseDiff with a partial taskDiff', () => {
  it('renders the changed volume size without throwing', () => {
    const differ = new UniverseDiff({
      beforeData: getTaskDiffValue(resizeTaskDiff, 'old'),
      afterData: getTaskDiffValue(resizeTaskDiff, 'new'),
      task: {} as any
    });
    expect(() => differ.getDiffComponent()).not.toThrow();
    expect(differ.changesCount).toBe(1);
  });

  it('shows a leader preference change and no empty region card', () => {
    const lp = (o: number, n: number) => ({
      leaderPreference: { $deltaType: 'REPLACE', $oldValue: o, $newValue: n }
    });
    const diff = withDefaultClusterType({
      clusters: [
        { placementInfo: { cloudList: [{ regionList: [{ azList: [lp(0, 1), lp(0, 2)] }] }] } }
      ]
    });
    const differ = new UniverseDiff({
      beforeData: getTaskDiffValue(diff, 'old'),
      afterData: getTaskDiffValue(diff, 'new'),
      task: {} as any
    });
    differ.getDiffComponent();
    expect(differ.changesCount).toBe(2);
    // one Leader Preference card only
    expect(differ.cards.PRIMARY).toHaveLength(1);
  });

  it('does not pair an added AZ with a modified one', () => {
    const diff = withDefaultClusterType({
      clusters: [
        {
          placementInfo: {
            cloudList: [
              {
                regionList: [
                  {
                    azList: [
                      { $deltaType: 'ADD', $newValue: { name: 'az3', leaderPreference: 1 } },
                      {
                        name: 'az1',
                        numNodesInAZ: { $deltaType: 'REPLACE', $oldValue: 1, $newValue: 2 }
                      }
                    ]
                  }
                ]
              }
            ]
          }
        }
      ]
    });
    const differ = new UniverseDiff({
      beforeData: getTaskDiffValue(diff, 'old'),
      afterData: getTaskDiffValue(diff, 'new'),
      task: {} as any
    });
    differ.getDiffComponent();
    expect(JSON.stringify(differ.cards.PRIMARY)).not.toContain('Leader Preference');
  });

  it('renders a read replica only change', () => {
    const diff = {
      clusters: [
        {
          clusterType: 'ASYNC',
          userIntent: {
            deviceInfo: { volumeSize: { $deltaType: 'REPLACE', $oldValue: 100, $newValue: 200 } }
          }
        }
      ]
    };
    const differ = new UniverseDiff({
      beforeData: getTaskDiffValue(diff, 'old'),
      afterData: getTaskDiffValue(diff, 'new'),
      task: {} as any
    });
    differ.getDiffComponent();
    expect(differ.changesCount).toBe(1);
    expect(differ.cards.ASYNC).toHaveLength(1);
    expect(differ.cards.PRIMARY).toHaveLength(0);
  });

  it('renders an added read replica', () => {
    const diff = {
      clusters: [
        {
          $deltaType: 'ADD',
          $newValue: {
            clusterType: 'ASYNC',
            userIntent: { instanceType: 'c5.large', deviceInfo: { volumeSize: 100 } }
          }
        }
      ]
    };
    const differ = new UniverseDiff({
      beforeData: getTaskDiffValue(diff, 'old'),
      afterData: getTaskDiffValue(diff, 'new'),
      task: {} as any
    });
    expect(() => differ.getDiffComponent()).not.toThrow();
    expect(differ.changesCount).toBeGreaterThan(0);
    expect(differ.cards.ASYNC.length).toBeGreaterThan(0);
  });
});
