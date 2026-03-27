import type { Meta, StoryObj } from '@storybook/react-vite';
import { http, HttpResponse } from 'msw';

import {
  createDbUpgradeTaskMock,
  defaultDbUpgradeCanaryProgress
} from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import {
  AZUpgradeStatus,
  DbUpgradePrecheckStatus,
  type Task
} from '@app/redesign/features/tasks/dtos';
import type { GetPagedCustomerTaskResponse } from '@app/redesign/helpers/api';
import type { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbUpgradeManagementSidePanel } from './DbUpgradeManagementSidePanel';

const mockUniverse = generateUniverseMockResponse();

const UNIVERSE_UUID = mockUniverse.info?.universe_uuid ?? 'mock-universe-uuid';

const defaultCanary = defaultDbUpgradeCanaryProgress();

/**
 * Returns a list of AZUpgradeStates with the second last AZ in progress and the last AZ not started.
 * This is used to test the complete range of upgrade AZ stages.
 */
const tserverAzUpgradeStatesListWithSecondLastInProgress = <T extends { status: AZUpgradeStatus }>(
  tserverAZUpgradeStatesList: T[]
): T[] => {
  const tserverCount = tserverAZUpgradeStatesList.length;
  return tserverAZUpgradeStatesList.map((az, index) => {
    if (tserverCount === 1) {
      return { ...az, status: AZUpgradeStatus.IN_PROGRESS };
    }
    if (index < tserverCount - 2) {
      return { ...az, status: AZUpgradeStatus.COMPLETED };
    }
    if (index === tserverCount - 2) {
      return { ...az, status: AZUpgradeStatus.IN_PROGRESS };
    }
    return { ...az, status: AZUpgradeStatus.NOT_STARTED };
  });
};

const toPagedSoftwareUpgradeTasksResponse = (task: Task): GetPagedCustomerTaskResponse => ({
  entities: [task],
  hasNext: false,
  hasPrev: false,
  totalCount: 1
});

type PrecheckApiBody = {
  ysql_major_version_upgrade: boolean;
  finalize_required: boolean;
};

const defaultPrecheckBody: PrecheckApiBody = {
  ysql_major_version_upgrade: false,
  finalize_required: false
};

const dbUpgradeManagementSidePanelHandlers = (
  task: Task,
  universe: Universe,
  precheckBody: PrecheckApiBody = defaultPrecheckBody
) => [
  http.post('http://localhost:9000/api/v1/customers/customer-uuid/tasks_list/page', () =>
    HttpResponse.json(toPagedSoftwareUpgradeTasksResponse(task))
  ),
  http.get(`http://localhost:9000/api/v2/customers/customer-uuid/universes/${UNIVERSE_UUID}`, () =>
    HttpResponse.json(universe)
  ),
  http.post(
    `http://localhost:9000/api/v2/customers/customer-uuid/universes/${UNIVERSE_UUID}/upgrade/software/precheck`,
    () => HttpResponse.json(precheckBody)
  )
];

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

const storyWithTask = (task: Task, precheckBody?: PrecheckApiBody): Story => ({
  parameters: {
    msw: {
      handlers: {
        dbUpgradeManagementSidePanel: dbUpgradeManagementSidePanelHandlers(
          task,
          mockUniverse,
          precheckBody
        )
      }
    }
  }
});

export const PrecheckRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    canaryUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.RUNNING,
      masterAZUpgradeStatesList: defaultCanary.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const PrecheckFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    canaryUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.FAILED,
      masterAZUpgradeStatesList: defaultCanary.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.NOT_STARTED
      }))
    }
  })
);

export const MasterUpgradeRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    canaryUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultCanary.masterAZUpgradeStatesList.map((az, index) => ({
        ...az,
        status: index === 0 ? AZUpgradeStatus.IN_PROGRESS : AZUpgradeStatus.COMPLETED
      }))
    }
  })
);

export const MasterUpgradeFailed: Story = storyWithTask(
  createDbUpgradeTaskMock({
    canaryUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultCanary.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.FAILED
      }))
    }
  })
);

export const UpgradeAzTserversRunning: Story = storyWithTask(
  createDbUpgradeTaskMock({
    canaryUpgradeProgress: {
      precheckStatus: DbUpgradePrecheckStatus.SUCCESS,
      masterAZUpgradeStatesList: defaultCanary.masterAZUpgradeStatesList.map((az) => ({
        ...az,
        status: AZUpgradeStatus.COMPLETED
      })),
      tserverAZUpgradeStatesList: tserverAzUpgradeStatesListWithSecondLastInProgress(
        defaultCanary.tserverAZUpgradeStatesList
      )
    }
  })
);

export const WithYsqlMajorUpgrade: Story = storyWithTask(createDbUpgradeTaskMock(), {
  ysql_major_version_upgrade: true,
  finalize_required: true
});
