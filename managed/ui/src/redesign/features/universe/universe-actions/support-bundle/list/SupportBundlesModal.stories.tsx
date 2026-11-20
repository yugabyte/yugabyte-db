import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  createSupportBundleListMock,
  createSupportBundleMock,
  supportBundleListMswHandlers
} from '@app/mocks/mock-data/supportBundleMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import { SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { SupportBundlesModal } from './SupportBundlesModal';

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

const HOUR_IN_MS = 60 * 60 * 1000;
const manyBundles = Array.from({ length: 60 }, (_, index) =>
  createSupportBundleMock(`sb-${index}`, SupportBundleStatus.Success, {
    createdAt: new Date(Date.now() - index * 3 * HOUR_IN_MS)
  })
);

const meta = {
  title: 'Universe/Support Bundle/SupportBundlesModal',
  component: SupportBundlesModal,
  // Dates are formatted through the redux store's user timezone.
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: {
    universeUuid: 'universe-uuid'
  },
  argTypes: {
    onClose: { action: 'onClose' },
    onCreateBundle: { action: 'onCreateBundle' },
    onShareBundle: { action: 'onShareBundle' }
  }
} satisfies Meta<typeof SupportBundlesModal>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Empty: Story = {
  parameters: {
    msw: { handlers: supportBundleListMswHandlers() }
  }
};

/** One bundle per status. The running bundle shows the "tasks keep running" note. */
export const WithBundles: Story = {
  parameters: {
    msw: { handlers: supportBundleListMswHandlers({ bundles: createSupportBundleListMock() }) }
  }
};

export const Paged: Story = {
  parameters: {
    msw: { handlers: supportBundleListMswHandlers({ bundles: manyBundles }) }
  }
};

export const Loading: Story = {
  parameters: {
    msw: { handlers: supportBundleListMswHandlers({ listState: 'loading' }) }
  }
};

export const LoadFailed: Story = {
  parameters: {
    msw: { handlers: supportBundleListMswHandlers({ listState: 'error' }) }
  }
};
