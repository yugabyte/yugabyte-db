import { getGFlagDiffs } from './DiffUtils';
import { getTaskDiffValue, withDefaultClusterType } from './taskDiffUtils';

const flag = (o: unknown, n: unknown, t: string) => ({
  $deltaType: t,
  $oldValue: o,
  $newValue: n
});

describe('getGFlagDiffs', () => {
  const taskDiff = withDefaultClusterType({
    clusters: [
      {
        userIntent: {
          specificGFlags: {
            perProcessFlags: {
              value: {
                MASTER: { max_log_size: flag('1', '2', 'REPLACE') },
                TSERVER: { new_flag: flag(null, 'x', 'ADD'), old_flag: flag('y', null, 'DELETE') }
              }
            }
          }
        }
      }
    ]
  });
  const diffs = getGFlagDiffs(getTaskDiffValue(taskDiff, 'old'), getTaskDiffValue(taskDiff, 'new'));

  it('lists changed, added and removed flags per process', () => {
    expect(diffs.master).toEqual([{ name: 'max_log_size', old: '1', new: '2', default: '' }]);
    expect(diffs.tserver).toHaveLength(2);
    expect(diffs.tserver).toEqual(
      expect.arrayContaining([
        { name: 'new_flag', old: null, new: 'x', default: '' },
        { name: 'old_flag', old: 'y', new: null, default: '' }
      ])
    );
  });
});

describe('getGFlagDiffs with legacy flags', () => {
  it('does not list unchanged legacy flags as added', () => {
    const cluster = (userIntent: unknown) => ({
      clusters: [{ clusterType: 'PRIMARY', userIntent }]
    });
    const after = cluster({
      specificGFlags: { perProcessFlags: { value: { MASTER: { a: '1', b: '3' } } } }
    });
    const fullBefore = cluster({ masterGFlags: { a: '1', b: '2' } });
    const diffs = getGFlagDiffs(cluster({}), after, fullBefore);
    expect(diffs.master).toEqual([{ name: 'b', old: '2', new: '3', default: '' }]);
  });
});
