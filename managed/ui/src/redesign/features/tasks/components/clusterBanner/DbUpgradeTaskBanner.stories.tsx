import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradeTaskMock,
  DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
  defaultDbUpgradeSoftwareUpgradeProgress,
  tserverAzUpgradeStatesListWithSecondLastInProgress
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import {
  AZUpgradeStatus,
  CanaryPauseState,
  TaskState,
  TaskType,
  type Task
} from '@app/redesign/features/tasks/dtos';
import {
  UniverseInfoSoftwareUpgradeState,
  type UniverseInfo,
  type UniverseSoftwareUpgradePrecheckResp
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbUpgradeTaskBanner } from './DbUpgradeTaskBanner';

const mockUniverse = generateUniverseMockResponse();

const defaultSoftwareUpgradeProgress = defaultDbUpgradeSoftwareUpgradeProgress();

/** Default docs canvas: paused after masters, matching {@link TaskState.PAUSED} on the base mock. */
const defaultDbUpgradeTask = createDbUpgradeTaskMock({
  softwareUpgradeProgress: {
    canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
    masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
      (az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })
    ),
    tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map(
      (az) => ({
        ...az,
        status: AZUpgradeStatus.NOT_STARTED
      })
    )
  }
});

type DbUpgradeTaskBannerStoryMswOptions = {
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>;
  universeInfoOverrides?: Partial<UniverseInfo>;
};

const storyWithBannerMsw = (task: Task, storyMswOptions?: DbUpgradeTaskBannerStoryMswOptions) => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeTaskBanner: dbUpgradeManagementPanelMswHandlers(
          DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
          task,
          mockUniverse,
          createDbUpgradePrecheckMetadataMock(storyMswOptions?.precheckOverrides ?? {}),
          storyMswOptions?.universeInfoOverrides
            ? { universeInfoOverrides: storyMswOptions.universeInfoOverrides }
            : undefined
        )
      }
    }
  }
});

const withCustomerId = (Story: ComponentType) => {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
};

const meta = {
  title: 'Tasks/DbUpgradeTaskBanner',
  component: DbUpgradeTaskBanner,
  tags: ['autodocs'],
  parameters: storyWithBannerMsw(defaultDbUpgradeTask, {
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Paused }
  }).parameters,
  decorators: [withCustomerId, withStorybookTasksReduxProvider],
  args: {
    universeUuid: DB_UPGRADE_TASK_MOCK_UNIVERSE_UUID,
    task: defaultDbUpgradeTask
  }
} satisfies Meta<typeof DbUpgradeTaskBanner>;

export default meta;

type Story = StoryObj<typeof meta>;

const runningUpgradeSoftwareUpgradeProgress = {
  masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  })),
  tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
    defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList
  )
};

const upgradeRunningTask = createDbUpgradeTaskMock({
  status: TaskState.RUNNING,
  softwareUpgradeProgress: runningUpgradeSoftwareUpgradeProgress
});

export const UpgradeRunning: Story = {
  ...storyWithBannerMsw(upgradeRunningTask, {
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Upgrading }
  }),
  args: {
    task: upgradeRunningTask
  }
};

const upgradeRunningYsqlTask = createDbUpgradeTaskMock({
  status: TaskState.RUNNING,
  softwareUpgradeProgress: runningUpgradeSoftwareUpgradeProgress
});

export const UpgradeRunningYsqlMajorVersion: Story = {
  ...storyWithBannerMsw(upgradeRunningYsqlTask, {
    precheckOverrides: { ysql_major_version_upgrade: true },
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Upgrading }
  }),
  args: {
    task: upgradeRunningYsqlTask
  }
};

const pausedAfterMastersSoftwareUpgradeProgress = {
  canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
  masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  })),
  tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map(
    (az) => ({
      ...az,
      status: AZUpgradeStatus.NOT_STARTED
    })
  )
};

const upgradePausedTask = createDbUpgradeTaskMock({
  status: TaskState.PAUSED,
  softwareUpgradeProgress: pausedAfterMastersSoftwareUpgradeProgress
});

export const UpgradePaused: Story = {
  ...storyWithBannerMsw(upgradePausedTask, {
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Paused }
  }),
  args: {
    task: upgradePausedTask
  }
};

const allAzsCompletedSoftwareUpgradeProgress = {
  masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
    ...az,
    status: AZUpgradeStatus.COMPLETED
  })),
  tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map(
    (az) => ({
      ...az,
      status: AZUpgradeStatus.COMPLETED
    })
  )
};

const upgradeSuccessfulPendingFinalizeTask = createDbUpgradeTaskMock({
  status: TaskState.SUCCESS,
  softwareUpgradeProgress: allAzsCompletedSoftwareUpgradeProgress
});

export const UpgradeSuccessfulPendingFinalize: Story = {
  ...storyWithBannerMsw(upgradeSuccessfulPendingFinalizeTask, {
    precheckOverrides: {
      finalize_required: true,
      ysql_major_version_upgrade: true
    },
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.PreFinalize }
  }),
  args: {
    task: upgradeSuccessfulPendingFinalizeTask
  }
};

const upgradeSuccessfulTask = createDbUpgradeTaskMock({
  status: TaskState.SUCCESS,
  softwareUpgradeProgress: allAzsCompletedSoftwareUpgradeProgress
});

export const UpgradeSuccessful: Story = {
  ...storyWithBannerMsw(upgradeSuccessfulTask, {
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Ready }
  }),
  args: {
    task: upgradeSuccessfulTask
  }
};

const upgradeFailedTask = createDbUpgradeTaskMock({
  status: TaskState.FAILURE,
  softwareUpgradeProgress: {
    masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
      (az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })
    ),
    tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map(
      (az, index) => ({
        ...az,
        status: index === 0 ? AZUpgradeStatus.FAILED : AZUpgradeStatus.NOT_STARTED
      })
    )
  }
});

export const UpgradeFailed: Story = {
  ...storyWithBannerMsw(upgradeFailedTask, {
    universeInfoOverrides: {
      software_upgrade_state: UniverseInfoSoftwareUpgradeState.UpgradeFailed
    }
  }),
  args: {
    task: upgradeFailedTask
  }
};

/** Task failed while universe is Ready (e.g. upgrade aborted) — ALERT banner vs {@link UpgradeFailed} ERROR. */
const upgradeAbortedTask = createDbUpgradeTaskMock({
  status: TaskState.FAILURE,
  omitSoftwareUpgradeProgress: true
});

export const UpgradeFailedDuringPrecheck: Story = {
  ...storyWithBannerMsw(upgradeAbortedTask, {
    universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Ready }
  }),
  args: {
    task: upgradeAbortedTask
  }
};

/** Renders nothing: banner only applies to software upgrade tasks. */
export const NonDbUpgradeTask: Story = {
  args: {
    task: createDbUpgradeTaskMock({ type: TaskType.EDIT as Task['type'] })
  }
};
