import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';

import { createSupportBundleMock } from '@app/mocks/mock-data/supportBundleMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import {
  SupportBundleComponentType,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { ShareBundleModal } from './ShareBundleModal';

const withCustomerId = (Story: ComponentType) => {
  window.localStorage.setItem('customerId', 'customer-uuid');
  return <Story />;
};

const meta = {
  title: 'Universe/Support Bundle/ShareBundleModal',
  component: ShareBundleModal,
  // Dates are formatted through the redux store's user timezone.
  decorators: [withCustomerId, withStorybookTasksReduxProvider],
  args: {
    universeUuid: 'universe-uuid',
    bundle: createSupportBundleMock('sb-ready', SupportBundleStatus.Success)
  },
  argTypes: {
    bundle: { control: false },
    onClose: { action: 'onClose' }
  }
} satisfies Meta<typeof ShareBundleModal>;

export default meta;
type Story = StoryObj<typeof meta>;

/** Core dumps can't be redacted, so sharing them needs an acknowledgement. */
export const WithCoreDumps: Story = {};

export const WithoutSensitiveData: Story = {
  args: {
    bundle: createSupportBundleMock('sb-ready', SupportBundleStatus.Success, {
      components: [
        SupportBundleComponentType.ApplicationLogs,
        SupportBundleComponentType.UniverseLogs,
        SupportBundleComponentType.GFlags
      ]
    })
  }
};
