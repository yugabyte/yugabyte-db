import type { Meta, StoryObj } from '@storybook/react-vite';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradeMockAzUpgradeState,
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradeTaskMock,
  defaultDbUpgradeSoftwareUpgradeProgress,
  tserverAzUpgradeStatesListWithSecondLastInProgress
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import {
  AZUpgradeStatus,
  CanaryPauseState,
  ServerType,
  TaskState,
  type AZUpgradeState,
  type Task
} from '@app/redesign/features/tasks/dtos';
import {
  UniverseInfoSoftwareUpgradeState,
  type UniverseInfo,
  type UniverseSoftwareUpgradePrecheckResp
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbUpgradeManagementSidePanel } from './DbUpgradeManagementSidePanel';

const mockUniverse = generateUniverseMockResponse();

const UNIVERSE_UUID = mockUniverse.info?.universe_uuid ?? 'mock-universe-uuid';

const defaultSoftwareUpgradeProgress = defaultDbUpgradeSoftwareUpgradeProgress();

const withCustomerId = (Story: React.ComponentType) => {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
};

const meta = {
  title: 'Universe/DB Upgrade/DbUpgradeManagementSidePanel',
  component: DbUpgradeManagementSidePanel,
  parameters: {
    layout: 'centered'
  },
  decorators: [withCustomerId, withStorybookTasksReduxProvider],
  args: {
    universeUuid: UNIVERSE_UUID,
    modalProps: {
      open: true,
      onClose: () => {}
    }
  }
} satisfies Meta<typeof DbUpgradeManagementSidePanel>;

export default meta;
type Story = StoryObj<typeof meta>;

type DbUpgradeManagementSidePanelStoryOptions = {
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>;
  universeInfoOverrides?: Partial<UniverseInfo>;
};

const storyWithTask = (task: Task, options?: DbUpgradeManagementSidePanelStoryOptions): Story => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeManagementSidePanel: dbUpgradeManagementPanelMswHandlers(
          UNIVERSE_UUID,
          task,
          mockUniverse,
          createDbUpgradePrecheckMetadataMock(options?.precheckOverrides ?? {}),
          options?.universeInfoOverrides
            ? { universeInfoOverrides: options.universeInfoOverrides }
            : undefined
        )
      }
    }
  }
});

export const PrecheckRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    status: TaskState.RUNNING,
    omitSoftwareUpgradeProgress: true
  }),
  { universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Ready } }
);

export const PrecheckFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    status: TaskState.FAILURE,
    omitSoftwareUpgradeProgress: true
  }),
  { universeInfoOverrides: { software_upgrade_state: UniverseInfoSoftwareUpgradeState.Ready } }
);

export const MasterUpgradeRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az, index) => ({
          ...az,
          status: index === 0 ? AZUpgradeStatus.IN_PROGRESS : AZUpgradeStatus.COMPLETED
        })
      )
    }
  })
);

export const MasterUpgradeFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.FAILED
        })
      )
    }
  })
);

export const MasterUpgradeCompletedAndUpgradePaused: Story = storyWithTask(
  createDbUpgradeTaskMock({
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
  })
);

export const UpgradeAzTserversRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.COMPLETED
        })
      ),
      tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
        defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList
      )
    }
  })
);

export const UpgradeAzTserversFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
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
  })
);

export const UpgradeAzTserverCompletedAndUpgradePaused: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      canaryPauseState: CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.COMPLETED
        })
      ),
      tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map(
        (az, index) => ({
          ...az,
          status: index === 0 ? AZUpgradeStatus.COMPLETED : AZUpgradeStatus.NOT_STARTED
        })
      )
    }
  })
);

export const UpgradePendingFinalize: Story = storyWithTask(
  createDbUpgradeTaskMock({
    status: TaskState.SUCCESS,
    softwareUpgradeProgress: {
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.COMPLETED
        })
      ),
      tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.COMPLETED
        })
      )
    }
  })
);

const MANY_TSERVER_AZ_COUNT = 16;
const MANY_TSERVER_AZ_ACTIVE_INDEX = 15;
const MANY_TSERVER_AZ_CLUSTER_UUID = 'story-many-az-cluster-uuid';

/**
 * Builds {@link MANY_TSERVER_AZ_COUNT} t-server AZ rows where the AZ at
 * {@link MANY_TSERVER_AZ_ACTIVE_INDEX} is IN_PROGRESS, earlier AZs are COMPLETED,
 * and later AZs are NOT_STARTED. Used to exercise the panel's auto-scroll behavior
 * when the active stage sits below the initial scroll position.
 */
const createManyTserverAzUpgradeStates = (): AZUpgradeState[] =>
  Array.from({ length: MANY_TSERVER_AZ_COUNT }, (_, index) => {
    const status =
      index < MANY_TSERVER_AZ_ACTIVE_INDEX
        ? AZUpgradeStatus.COMPLETED
        : index === MANY_TSERVER_AZ_ACTIVE_INDEX
          ? AZUpgradeStatus.IN_PROGRESS
          : AZUpgradeStatus.NOT_STARTED;
    const azLabel = String(index + 1).padStart(2, '0');
    return createDbUpgradeMockAzUpgradeState(
      `story-many-az-${azLabel}-uuid`,
      status,
      ServerType.TSERVER,
      MANY_TSERVER_AZ_CLUSTER_UUID,
      `story-many-az-${azLabel}`
    );
  });

export const UpgradeAzTserversRunningManyAzs: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.COMPLETED
        })
      ),
      tserverAZUpgradeStatesList: createManyTserverAzUpgradeStates()
    }
  })
);

export const WithYsqlMajorUpgrade: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map(
        (az) => ({
          ...az,
          status: AZUpgradeStatus.COMPLETED
        })
      ),
      tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
        defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList
      )
    }
  }),
  {
    precheckOverrides: {
      ysql_major_version_upgrade: true,
      finalize_required: true
    }
  }
);
