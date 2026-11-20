import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';

import {
  createSupportBundleListMock,
  createSupportBundleUniverseMock,
  supportBundleCreateMswHandlers,
  supportBundleListMswHandlers
} from '@app/mocks/mock-data/supportBundleMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';

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

/** The whole flow as the universe's Support Bundles action opens it. */
const meta = {
  title: 'Universe/Support Bundle/SupportBundleV2',
  component: SupportBundleV2,
  // Dates are formatted through the redux store's user timezone.
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: { universe },
  argTypes: {
    universe: { control: false },
    onClose: { action: 'onClose' }
  }
} satisfies Meta<typeof SupportBundleV2>;

export default meta;
type Story = StoryObj<typeof meta>;

export const FirstBundle: Story = {
  parameters: {
    msw: {
      handlers: [...supportBundleListMswHandlers(), ...supportBundleCreateMswHandlers({ universe })]
    }
  }
};

export const WithBundles: Story = {
  parameters: {
    msw: {
      handlers: [
        ...supportBundleListMswHandlers({ bundles: createSupportBundleListMock() }),
        ...supportBundleCreateMswHandlers({ universe, isPerfAdvisorRegistered: true })
      ]
    }
  }
};
