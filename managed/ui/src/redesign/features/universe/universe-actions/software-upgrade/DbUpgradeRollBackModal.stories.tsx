import type { Meta, StoryObj } from '@storybook/react-vite';
import { http, HttpResponse } from 'msw';

import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import type { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbUpgradeRollBackModal } from './DbUpgradeRollBackModal';

const mockUniverse: Universe = generateUniverseMockResponse();

function withCustomerId(Story: React.ComponentType) {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
}

const meta = {
  title: 'Universe/DB Upgrade/DbUpgradeRollBackModal',
  component: DbUpgradeRollBackModal,
  parameters: {
    layout: 'centered'
  },
  decorators: [withCustomerId, withStorybookTasksReduxProvider],
  args: {
    modalProps: {
      open: true,
      onClose: () => {},
      onSubmit: () => {}
    }
  }
} satisfies Meta<typeof DbUpgradeRollBackModal>;

export default meta;
type Story = StoryObj<typeof meta>;

export const MultiRegionUniverse: Story = {
  parameters: {
    msw: {
      handlers: {
        dbUpgradeRollBackModal: [
          http.get(
            'http://localhost:9000/api/v2/customers/customer-uuid/universes/mock-universe-uuid',
            () => HttpResponse.json(mockUniverse)
          )
        ]
      }
    }
  },
  args: {
    universeUuid: mockUniverse.info?.universe_uuid ?? 'mock-universe-uuid'
  }
};
