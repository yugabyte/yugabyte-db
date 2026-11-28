import type { ComponentType } from 'react';
import type { Meta, StoryObj } from '@storybook/react-vite';
import { useQuery } from 'react-query';

import { fetchXClusterConfig } from '@app/actions/xClusterReplication';
import {
  XCLUSTER_BANNER_SCENARIOS,
  XCLUSTER_MOCK_CONFIG_UUID,
  xClusterBannerMswHandlers
} from '@app/mocks/mock-data/xClusterBannerMswHandlers';
import { api, universeQueryKey, xClusterQueryKey } from '@app/redesign/helpers/api';
import { XClusterConfigAction } from '../constants';
import { XClusterBannerSection } from './XClusterBannerSection';

interface XClusterBannerSectionStoryArgs {
  enabledConfigActions: XClusterConfigAction[];
  openRestartConfigModal: () => void;
  onViewTables: () => void;
}

/**
 * Loads the xCluster config and source universe the same way `ReplicationDetails` does, so the
 * banner is driven entirely by the mocked API responses.
 */
const XClusterBannerSectionFromApi = (props: XClusterBannerSectionStoryArgs) => {
  const xClusterConfigQuery = useQuery(xClusterQueryKey.detail(XCLUSTER_MOCK_CONFIG_UUID), () =>
    fetchXClusterConfig(XCLUSTER_MOCK_CONFIG_UUID)
  );
  const sourceUniverseUuid = xClusterConfigQuery.data?.sourceUniverseUUID;
  const sourceUniverseQuery = useQuery(
    universeQueryKey.detail(sourceUniverseUuid),
    () => api.fetchUniverse(sourceUniverseUuid),
    { enabled: sourceUniverseUuid !== undefined }
  );

  if (!xClusterConfigQuery.data) {
    return <span>Loading xCluster config...</span>;
  }
  return (
    <XClusterBannerSection
      {...props}
      xClusterConfig={xClusterConfigQuery.data}
      sourceUniverse={sourceUniverseQuery.data}
    />
  );
};

const withCustomerId = (Story: ComponentType) => {
  window.localStorage.setItem('customerId', 'customer-uuid');
  return <Story />;
};

const meta = {
  title: 'xCluster/XClusterBannerSection',
  component: XClusterBannerSectionFromApi,
  tags: ['autodocs'],
  decorators: [withCustomerId],
  args: {
    enabledConfigActions: [XClusterConfigAction.RESTART]
  },
  argTypes: {
    openRestartConfigModal: { action: 'openRestartConfigModal' },
    onViewTables: { action: 'onViewTables' }
  }
} satisfies Meta<typeof XClusterBannerSectionFromApi>;

export default meta;

type Story = StoryObj<typeof meta>;

const withScenario = (scenario: keyof typeof XCLUSTER_BANNER_SCENARIOS) => ({
  msw: { handlers: xClusterBannerMswHandlers(XCLUSTER_BANNER_SCENARIOS[scenario]) }
});

/** All tables running and lag below the alert threshold: no banner. */
export const Healthy: Story = { parameters: withScenario('healthy') };

/** Bootstrapping, updating and warning tables are not problems: no banner. */
export const NonProblemStatuses: Story = { parameters: withScenario('nonProblemStatuses') };

/** One table's latest lag is above the alert threshold: warning banner. */
export const LagAboveThreshold: Story = { parameters: withScenario('lagAboveThreshold') };

/** High lag but no replication lag alert configured: no banner. */
export const LagWithoutAlertConfig: Story = { parameters: withScenario('lagWithoutAlertConfig') };

/** Alert config and metrics requests fail: treated as no lag, so no banner. */
export const LagRequestsFail: Story = { parameters: withScenario('lagRequestsFail') };

/** A new table that is still uninitialized is expected in automatic DDL mode: no banner. */
export const UninitializedTableOnly: Story = { parameters: withScenario('uninitializedTableOnly') };

/** System error on the DDL queue, with a new table stuck uninitialized behind it: error banner. */
export const SystemErrorWithUninitializedTable: Story = {
  parameters: withScenario('systemErrorWithUninitializedTable')
};

/** Every problem group except Missing Op ID: error banner without numbered steps. */
export const ProblemTablesWithoutMissingOp: Story = {
  parameters: withScenario('problemTablesWithoutMissingOp')
};

/** A table is missing an op: error banner with the restart step. */
export const MissingOp: Story = { parameters: withScenario('missingOp') };

/** Missing op plus other errors and lag above threshold: one error banner with the restart step. */
export const MissingOpWithOtherIssuesAndLag: Story = {
  parameters: withScenario('missingOpWithOtherIssuesAndLag')
};

/** Restart is not currently an enabled config action: the restart button is disabled. */
export const RestartNotAllowed: Story = {
  args: { enabledConfigActions: [] },
  parameters: withScenario('missingOp')
};
