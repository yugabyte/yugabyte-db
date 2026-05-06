import type { Meta, StoryObj } from '@storybook/react-vite';

import { dbUpgradeManagementPanelMswHandlers } from '@app/mocks/mock-data/dbUpgradeManagementPanelMswHandlers';
import {
  createDbUpgradePrecheckMetadataMock,
  createDbUpgradeTaskMock,
  defaultDbUpgradeSoftwareUpgradeProgress,
  tserverAzUpgradeStatesListWithSecondLastInProgress
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import {
  AZUpgradeStatus,
  CanaryPauseState,
  DbUpgradePrecheckStatus,
  TaskState,
  type Task
} from '@app/redesign/features/tasks/dtos';
import type { UniverseSoftwareUpgradePrecheckResp } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

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
  decorators: [withCustomerId],
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

const storyWithTask = (
  task: Task,
  precheckOverrides?: Partial<UniverseSoftwareUpgradePrecheckResp>
): Story => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeManagementSidePanel: dbUpgradeManagementPanelMswHandlers(
          UNIVERSE_UUID,
          task,
          mockUniverse,
          createDbUpgradePrecheckMetadataMock(precheckOverrides ?? {})
        )
      }
    }
  }
});

export const PrecheckRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.RUNNING,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const PrecheckFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.FAILED,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const MasterUpgradeRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az, index) => ({
        ...az,
        status: index === 0 ? AZUpgradeStatus.IN_PROGRESS : AZUpgradeStatus.COMPLETED
      }))
    }
  })
);

export const MasterUpgradeFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.FAILED
      }))
    }
  })
);

export const MasterUpgradeCompletedAndUpgradePaused: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      canaryPauseState: CanaryPauseState.PAUSED_AFTER_MASTERS,
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const UpgradeAzTserversRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
        defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList
      )
    }
  })
);

export const UpgradeAzTserversFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az, index) => ({
        ...az,
        status: index === 0 ? AZUpgradeStatus.FAILED : AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const UpgradeAzTserverCompletedAndUpgradePaused: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      canaryPauseState: CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ,
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az, index) => ({
        ...az,
        status: index === 0 ? AZUpgradeStatus.COMPLETED : AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const UpgradePendingFinalize: Story = storyWithTask(
  createDbUpgradeTaskMock({
    status: TaskState.SUCCESS,
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      }))
    }
  })
);

export const WithYsqlMajorUpgrade: Story = storyWithTask(
  createDbUpgradeTaskMock({
    softwareUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultSoftwareUpgradeProgress.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
        defaultSoftwareUpgradeProgress.tserverAZUpgradeStatesList
      )
    }
  }),
  {
    ysql_major_version_upgrade: true,
    finalize_required: true
  }
);
