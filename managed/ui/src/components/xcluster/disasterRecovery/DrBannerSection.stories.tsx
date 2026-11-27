import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { useQuery } from 'react-query';

import {
  DR_MOCK_CONFIG_UUID,
  XCLUSTER_BANNER_SCENARIOS,
  XClusterBannerMockScenario,
  xClusterBannerMswHandlers
} from '@app/mocks/mock-data/xClusterBannerMswHandlers';
import { api, drConfigQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import { XClusterConfigAction } from '../constants';
import { DrConfigAction } from './constants';
import { DrBannerSection } from './DrBannerSection';
import { DrConfigState } from './dtos';

interface DrBannerSectionStoryArgs {
  enabledDrConfigActions: DrConfigAction[];
  enabledXClusterConfigActions: XClusterConfigAction[];
  openRepairConfigModal: () => void;
  openRestartConfigModal: () => void;
  onViewTables: () => void;
}

/**
 * Loads the DR config and source universe the same way `DrPanel` does, so the banners are driven
 * entirely by the mocked API responses.
 */
const DrBannerSectionFromApi = (props: DrBannerSectionStoryArgs) => {
  const drConfigQuery = useQuery(drConfigQueryKey.detail(DR_MOCK_CONFIG_UUID), () =>
    api.fetchDrConfig(DR_MOCK_CONFIG_UUID)
  );
  const sourceUniverseUuid = drConfigQuery.data?.primaryUniverseUuid;
  const sourceUniverseQuery = useQuery(
    universeQueryKey.detail(sourceUniverseUuid),
    () => api.fetchUniverse(sourceUniverseUuid),
    { enabled: sourceUniverseUuid !== undefined }
  );

  if (!drConfigQuery.data) {
    return <span>Loading DR config...</span>;
  }
  return (
    <DrBannerSection
      {...props}
      drConfig={drConfigQuery.data}
      sourceUniverse={sourceUniverseQuery.data}
    />
  );
};

const withCustomerId = (Story: ComponentType) => {
  window.localStorage.setItem('customerId', 'customer-uuid');
  return <Story />;
};

const meta = {
  title: 'xCluster/DrBannerSection',
  component: DrBannerSectionFromApi,
  tags: ['autodocs'],
  decorators: [withCustomerId],
  args: {
    enabledDrConfigActions: [DrConfigAction.EDIT_TARGET],
    enabledXClusterConfigActions: [XClusterConfigAction.RESTART]
  },
  argTypes: {
    openRepairConfigModal: { action: 'openRepairConfigModal' },
    openRestartConfigModal: { action: 'openRestartConfigModal' },
    onViewTables: { action: 'onViewTables' }
  }
} satisfies Meta<typeof DrBannerSectionFromApi>;

export default meta;

type Story = StoryObj<typeof meta>;

const withScenario = (scenario: XClusterBannerMockScenario) => ({
  msw: { handlers: xClusterBannerMswHandlers(scenario) }
});

/** All tables running and lag below the alert threshold: no banner. */
export const Healthy: Story = { parameters: withScenario(XCLUSTER_BANNER_SCENARIOS.healthy) };

/** Lag above the alert threshold: warning banner with DR wording. */
export const LagAboveThreshold: Story = {
  parameters: withScenario(XCLUSTER_BANNER_SCENARIOS.lagAboveThreshold)
};

/** A table is missing an op: error banner with the restart step. */
export const MissingOp: Story = { parameters: withScenario(XCLUSTER_BANNER_SCENARIOS.missingOp) };

/** Halted DR with a missing op: only the halted banner shows; Repair DR is the fix. */
export const HaltedWithMissingOp: Story = {
  parameters: withScenario({
    ...XCLUSTER_BANNER_SCENARIOS.missingOp,
    drState: DrConfigState.HALTED
  })
};

/** Switchover running with a missing op and high lag: only the in-progress banner shows. */
export const SwitchoverInProgressWithIssues: Story = {
  parameters: withScenario({
    ...XCLUSTER_BANNER_SCENARIOS.missingOpWithOtherIssuesAndLag,
    drState: DrConfigState.SWITCHOVER_IN_PROGRESS
  })
};
