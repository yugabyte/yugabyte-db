import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { ToastContainer } from 'react-toastify';
import { within } from '@testing-library/dom';
import userEvent from '@testing-library/user-event';

import {
  createSupportBundleUniverseMock,
  supportBundleCreateMswHandlers
} from '@app/mocks/mock-data/supportBundleMocks';
import { withStorybookTasksReduxProvider } from '@app/mocks/storybook/storybookTasksRedux';

import { CreateSupportBundlePage } from './CreateSupportBundlePage';

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
const k8sUniverse = createSupportBundleUniverseMock({ isKubernetes: true });

const meta = {
  title: 'Universe/Support Bundle/CreateSupportBundlePage',
  component: CreateSupportBundlePage,
  parameters: { layout: 'fullscreen' },
  // Dates are formatted through the redux store's user timezone.
  decorators: [withCustomerId, withToastContainer, withStorybookTasksReduxProvider],
  args: {
    universe,
    context: { isK8sUniverse: false, isPerfAdvisorRegistered: false }
  },
  argTypes: {
    universe: { control: false },
    onClose: { action: 'onClose' },
    onCreated: { action: 'onCreated' }
  }
} satisfies Meta<typeof CreateSupportBundlePage>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Recommended: Story = {
  parameters: {
    msw: { handlers: supportBundleCreateMswHandlers({ universe }) }
  }
};

/** Over 20 GB the summary warns and offers to scope the bundle to fewer nodes. */
export const LargeBundle: Story = {
  parameters: {
    msw: { handlers: supportBundleCreateMswHandlers({ universe, estimateScale: 12 }) }
  }
};

export const Estimating: Story = {
  parameters: {
    msw: { handlers: supportBundleCreateMswHandlers({ universe, estimateState: 'loading' }) }
  }
};

/** Kubernetes info and Perf Advisor metrics are offered only where they apply. */
export const KubernetesWithPerfAdvisor: Story = {
  args: {
    universe: k8sUniverse,
    context: { isK8sUniverse: true, isPerfAdvisorRegistered: true }
  },
  parameters: {
    msw: { handlers: supportBundleCreateMswHandlers({ universe: k8sUniverse }) }
  }
};

// The page is a full-screen dialog, so it renders outside the story's canvas.
const selectCustomize = async (canvasElement: HTMLElement) => {
  const page = within(canvasElement.ownerDocument.body);
  await userEvent.click(await page.findByTestId('CreateSupportBundlePage-Customize-Select'));
  return page;
};

export const Customize: Story = {
  parameters: {
    msw: { handlers: supportBundleCreateMswHandlers({ universe }) }
  },
  play: async ({ canvasElement }: { canvasElement: HTMLElement }) => {
    await selectCustomize(canvasElement);
  }
};

/** Selecting Performance Metrics opens its Prometheus and Perf Advisor settings. */
export const CustomizeMetrics: Story = {
  args: {
    context: { isK8sUniverse: false, isPerfAdvisorRegistered: true }
  },
  parameters: {
    msw: {
      handlers: supportBundleCreateMswHandlers({ universe, isPerfAdvisorRegistered: true })
    }
  },
  play: async ({ canvasElement }: { canvasElement: HTMLElement }) => {
    const page = await selectCustomize(canvasElement);
    const metrics = within(await page.findByTestId('CustomizeCategory-performanceMetrics'));
    await userEvent.click(metrics.getAllByRole('checkbox')[0]);
  }
};
