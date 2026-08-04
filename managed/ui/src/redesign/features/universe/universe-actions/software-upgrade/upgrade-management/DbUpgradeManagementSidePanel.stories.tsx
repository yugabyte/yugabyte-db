import type { Meta, StoryObj } from '@storybook/react-vite';
import { http, HttpResponse } from 'msw';

import { generateDbUpgradeTaskMockResponse } from '@app/mocks/mock-data/taskMocks';
import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import type { GetPagedCustomerTaskResponse } from '@app/redesign/helpers/api';
import { DbUpgradeManagementSidePanel } from './DbUpgradeManagementSidePanel';

const mockDbUpgradeTask = generateDbUpgradeTaskMockResponse();

const mockUniverse = generateUniverseMockResponse();

const mockPagedSoftwareUpgradeTasksResponse: GetPagedCustomerTaskResponse = {
  entities: [mockDbUpgradeTask],
  hasNext: false,
  hasPrev: false,
  totalCount: 1
};

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
    universeUuid: mockUniverse.info?.universe_uuid ?? 'mock-universe-uuid',
    modalProps: {
      open: true,
      onClose: () => {}
    }
  }
} satisfies Meta<typeof DbUpgradeManagementSidePanel>;

export default meta;
type Story = StoryObj<typeof meta>;

export const WithSoftwareUpgradeTask: Story = {
  parameters: {
    msw: {
      handlers: {
        dbUpgradeManagementSidePanel: [
          http.post('http://localhost:9000/api/v1/customers/customer-uuid/tasks_list/page', () =>
            HttpResponse.json(mockPagedSoftwareUpgradeTasksResponse)
          ),
          http.get(
            'http://localhost:9000/api/v2/customers/customer-uuid/universes/mock-universe-uuid',
            () => HttpResponse.json(mockUniverse)
          ),
          http.post(
            'http://localhost:9000/api/v2/customers/customer-uuid/universes/mock-universe-uuid/upgrade/software/precheck',
            () =>
              HttpResponse.json({
                ysql_major_version_upgrade: false,
                finalize_required: false
              })
          )
        ]
      }
    }
  }
};

export const WithYsqlMajorUpgrade: Story = {
  parameters: {
    msw: {
      handlers: {
        dbUpgradeManagementSidePanel: [
          http.post('http://localhost:9000/api/v1/customers/customer-uuid/tasks_list/page', () =>
            HttpResponse.json(mockPagedSoftwareUpgradeTasksResponse)
          ),
          http.get(
            'http://localhost:9000/api/v2/customers/customer-uuid/universes/mock-universe-uuid',
            () => HttpResponse.json(mockUniverse)
          ),
          http.post(
            'http://localhost:9000/api/v2/customers/customer-uuid/universes/mock-universe-uuid/upgrade/software/precheck',
            () =>
              HttpResponse.json({
                ysql_major_version_upgrade: true,
                finalize_required: true
              })
          )
        ]
      }
    }
  }
};
