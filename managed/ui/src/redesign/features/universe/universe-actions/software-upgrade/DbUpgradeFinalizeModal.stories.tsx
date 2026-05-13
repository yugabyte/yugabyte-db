import type { Meta, StoryObj } from '@storybook/react-vite';

import { generateUniverseMockResponse } from '@app/mocks/mock-data/universeMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import type { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { DbUpgradeFinalizeModal } from './DbUpgradeFinalizeModal';

const mockUniverse: Universe = generateUniverseMockResponse();

function withCustomerId(Story: React.ComponentType) {
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('customerId', 'customer-uuid');
  }
  return <Story />;
}

const meta = {
  title: 'Universe/DB Upgrade/DbUpgradeFinalizeModal',
  component: DbUpgradeFinalizeModal,
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
} satisfies Meta<typeof DbUpgradeFinalizeModal>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {
  args: {
    universeUuid: mockUniverse.info?.universe_uuid ?? 'mock-universe-uuid'
  }
};
