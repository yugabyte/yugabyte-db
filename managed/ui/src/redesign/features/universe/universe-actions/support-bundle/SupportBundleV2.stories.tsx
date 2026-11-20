import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  createSupportBundleListMock,
  createSupportBundleMock,
  createSupportBundleUniverseMock,
  supportBundleCreateMswHandlers,
  supportBundleGetMswHandlers,
  supportBundleListMswHandlers
} from '@app/mocks/mock-data/supportBundleMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import { SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { SupportBundleReadyWatcher } from './ready/SupportBundleReadyWatcher';
import { SupportBundleV2 } from './SupportBundleV2';

import 'react-toastify/dist/ReactToastify.css';

const withCustomerId = (Story: ComponentType) => {
  window.localStorage.setItem('customerId', 'customer-uuid');
  return <Story />;
};

const withToastContainer = (Story: ComponentType) => (
  <>
    <Story />
    <ToastContainer hideProgressBar position="top-center" />
  </>
);

const universe = createSupportBundleUniverseMock();
// A bundle created in the story is reported ready on the watcher's first check.
const readyBundleHandlers = supportBundleGetMswHandlers(
  createSupportBundleMock('sb-new', SupportBundleStatus.Success)
);

/**
 * The whole flow as the universe's Support Bundles action opens it, with the ready banner that
 * the universe page mounts beside it.
 */
const meta = {
  title: 'Universe/Support Bundle/SupportBundleV2',
  component: SupportBundleV2,
  // Dates are formatted through the redux store's user timezone.
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: { universe },
  argTypes: {
    universe: { control: false },
    onClose: { action: 'onClose' }
  },
  render: (args) => (
    <>
      <SupportBundleV2 {...args} />
      <SupportBundleReadyWatcher
        universeUuid={args.universe.universeUUID}
        universeNodeCount={args.universe.universeDetails.nodeDetailsSet.length}
      />
    </>
  )
} satisfies Meta<typeof SupportBundleV2>;

export default meta;
type Story = StoryObj<typeof meta>;

export const FirstBundle: Story = {
  parameters: {
    msw: {
      handlers: [
        ...supportBundleListMswHandlers(),
        ...supportBundleCreateMswHandlers({ universe }),
        ...readyBundleHandlers
      ]
    }
  }
};

export const WithBundles: Story = {
  parameters: {
    msw: {
      handlers: [
        ...supportBundleListMswHandlers({ bundles: createSupportBundleListMock() }),
        ...supportBundleCreateMswHandlers({ universe, isPerfAdvisorRegistered: true }),
        ...readyBundleHandlers
      ]
    }
  }
};
