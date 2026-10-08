import { describe, it, expect } from 'vitest';

import { getLatestSoftwareUpgradeLockingTaskForUniverse } from './TaskUtils';
import { Task, TaskState, TaskType, TargetType } from './dtos';

const UNIVERSE_UUID = '11111111-1111-1111-1111-111111111111';

const makeUpgradeTask = (overrides: Partial<Task> & { id: string; createTime: string }): Task =>
  ({
    completionTime: '',
    title: 'Upgrade',
    percentComplete: 50,
    target: TargetType.UNIVERSE,
    targetUUID: UNIVERSE_UUID,
    type: TaskType.SOFTWARE_UPGRADE,
    typeName: 'Upgrade Software',
    status: TaskState.PAUSED,
    details: { taskDetails: [] },
    abortable: false,
    retryable: false,
    canRollback: false,
    correlationId: '',
    userEmail: 'user@example.com',
    subtaskInfos: [],
    taskInfo: { taskParams: {} },
    ...overrides
  }) as Task;

const makeSupportBundleTask = (id: string, createTime: string): Task =>
  ({
    id,
    createTime,
    completionTime: '',
    title: 'Support bundle',
    percentComplete: 10,
    target: TargetType.UNIVERSE,
    targetUUID: UNIVERSE_UUID,
    type: 'CreateSupportBundle',
    typeName: 'Create Support Bundle',
    status: TaskState.RUNNING,
    details: { taskDetails: [] },
    abortable: true,
    retryable: false,
    canRollback: false,
    correlationId: '',
    userEmail: 'user@example.com',
    subtaskInfos: [],
    taskInfo: { taskParams: {} }
  }) as unknown as Task;

describe('getLatestSoftwareUpgradeLockingTaskForUniverse', () => {
  it('returns a paused upgrade when a newer support bundle task exists', () => {
    const upgrade = makeUpgradeTask({
      id: 'upgrade-1',
      createTime: '2025-01-01T10:00:00Z',
      status: TaskState.PAUSED
    });
    const supportBundle = makeSupportBundleTask('bundle-1', '2025-01-01T11:00:00Z');

    expect(
      getLatestSoftwareUpgradeLockingTaskForUniverse([upgrade, supportBundle], UNIVERSE_UUID)
    ).toBe(upgrade);
  });

  it('returns a failed upgrade', () => {
    const upgrade = makeUpgradeTask({
      id: 'upgrade-1',
      createTime: '2025-01-01T10:00:00Z',
      status: TaskState.FAILURE
    });

    expect(getLatestSoftwareUpgradeLockingTaskForUniverse([upgrade], UNIVERSE_UUID)).toBe(upgrade);
  });

  it('returns an aborted upgrade when a newer support bundle exists', () => {
    const upgrade = makeUpgradeTask({
      id: 'upgrade-1',
      createTime: '2025-01-01T10:00:00Z',
      status: TaskState.ABORTED
    });
    const supportBundle = makeSupportBundleTask('bundle-1', '2025-01-01T11:00:00Z');

    expect(
      getLatestSoftwareUpgradeLockingTaskForUniverse([upgrade, supportBundle], UNIVERSE_UUID)
    ).toBe(upgrade);
  });

  it('returns a successful upgrade', () => {
    const upgrade = makeUpgradeTask({
      id: 'upgrade-1',
      createTime: '2025-01-01T10:00:00Z',
      status: TaskState.SUCCESS
    });

    expect(getLatestSoftwareUpgradeLockingTaskForUniverse([upgrade], UNIVERSE_UUID)).toBe(upgrade);
  });

  it('ignores a precheck task', () => {
    const precheck = makeUpgradeTask({
      id: 'precheck-1',
      createTime: '2025-01-01T10:00:00Z',
      status: TaskState.RUNNING,
      typeName: 'Validation Upgrade Software'
    });

    expect(getLatestSoftwareUpgradeLockingTaskForUniverse([precheck], UNIVERSE_UUID)).toBeUndefined();
  });
});
