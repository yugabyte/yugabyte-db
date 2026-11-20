import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  createSupportBundleMock,
  supportBundleGetMswHandlers
} from '@app/mocks/mock-data/supportBundleMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';
import { SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { addPendingSupportBundle } from './pendingSupportBundles';
import { SupportBundleReadyWatcher } from './SupportBundleReadyWatcher';

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

/** Stands in for the create page registering the bundle it just submitted. */
const withPendingBundle = (Story: ComponentType) => {
  addPendingSupportBundle('universe-uuid', 'sb-new');
  return <Story />;
};

const meta = {
  title: 'Universe/Support Bundle/SupportBundleReadyWatcher',
  component: SupportBundleReadyWatcher,
  parameters: { layout: 'fullscreen' },
  decorators: [
    withCustomerId,
    withToastContainer,
    withStorybookTasksReduxProvider,
    withPendingBundle
  ],
  args: {
    universeUuid: 'universe-uuid',
    universeNodeCount: 9
  }
} satisfies Meta<typeof SupportBundleReadyWatcher>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Ready: Story = {
  parameters: {
    msw: {
      handlers: supportBundleGetMswHandlers(
        createSupportBundleMock('sb-new', SupportBundleStatus.Success, {
          sizeInBytes: 6_300_000_000
        })
      )
    }
  }
};

/** A bundle from a subset of nodes reports that subset. */
export const ReadyFromSomeNodes: Story = {
  parameters: {
    msw: {
      handlers: supportBundleGetMswHandlers(
        createSupportBundleMock('sb-new', SupportBundleStatus.Success, {
          nodeNames: ['yb-dev-puppy-food-n1', 'yb-dev-puppy-food-n2']
        })
      )
    }
  }
};

export const Failed: Story = {
  parameters: {
    msw: {
      handlers: supportBundleGetMswHandlers(
        createSupportBundleMock('sb-new', SupportBundleStatus.Failed)
      )
    }
  }
};
