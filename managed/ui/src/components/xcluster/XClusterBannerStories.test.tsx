import type { ComponentType } from 'react';
import { render, screen, waitFor, within } from '@testing-library/react';
import { ThemeProvider } from '@material-ui/core';
import { composeStory } from '@storybook/react-vite';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';
import i18n from 'i18next';
import type { RequestHandler } from 'msw';
import { initReactI18next } from 'react-i18next';
import { QueryClient, QueryClientProvider } from 'react-query';
import { afterEach, beforeAll, describe, expect, it } from 'vitest';

import { server } from '@app/mocks/server';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import en from '@app/translations/en.json';
import * as XClusterBannerSectionStories from './configDetails/XClusterBannerSection.stories';
import * as DrBannerSectionStories from './disasterRecovery/DrBannerSection.stories';
import { XCLUSTER_TABLE_WARNING_BANNER_TEST_ID } from './sharedComponents/XClusterTableWarningBanner';

/**
 * Renders each banner story against its own MSW handlers and checks the banner the API responses
 * produce, so the stories double as regression tests for the API-to-banner logic.
 */

type ExpectedBanner =
  | { kind: 'none' }
  | { kind: 'warning'; text: string }
  | { kind: 'error'; restartButton: 'none' | 'enabled' | 'disabled' };

// `composeStories` drops every story name from its return type in this Storybook version, so
// compose each story individually.
type XClusterStoryName = Exclude<keyof typeof XClusterBannerSectionStories, 'default'>;
type DrStoryName = Exclude<keyof typeof DrBannerSectionStories, 'default'>;
const xClusterStory = (name: XClusterStoryName) =>
  composeStory(XClusterBannerSectionStories[name], XClusterBannerSectionStories.default);
const drStory = (name: DrStoryName) =>
  composeStory(DrBannerSectionStories[name], DrBannerSectionStories.default);

const LAG_ENDPOINTS = ['/alert_configurations/list', '/metrics'];

const renderStory = async (Story: ComponentType & { parameters: Record<string, any> }) => {
  let lagRequestsHandled = 0;
  const onResponse = ({ request }: { request: Request }) => {
    if (LAG_ENDPOINTS.some((endpoint) => new URL(request.url).pathname.endsWith(endpoint))) {
      lagRequestsHandled += 1;
    }
  };
  server.events.on('response:mocked', onResponse);
  server.use(...(Story.parameters.msw.handlers as RequestHandler[]));

  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={queryClient}>
      <YBThemeProvider theme={yba.ybaTheme}>
        <ThemeProvider theme={mainTheme}>
          <Story />
        </ThemeProvider>
      </YBThemeProvider>
    </QueryClientProvider>
  );

  try {
    // The lag queries only start once the config and source universe have loaded, so wait for
    // both lag responses and for every query to settle before checking the result.
    await waitFor(() => {
      expect(lagRequestsHandled).toBe(LAG_ENDPOINTS.length);
      expect(queryClient.isFetching()).toBe(0);
    });
  } finally {
    server.events.removeListener('response:mocked', onResponse);
  }
};

const expectBanner = (expected: ExpectedBanner) => {
  const banner = screen.queryByTestId(XCLUSTER_TABLE_WARNING_BANNER_TEST_ID);
  if (expected.kind === 'none') {
    expect(banner).not.toBeInTheDocument();
    return;
  }
  expect(banner).toBeInTheDocument();
  expect(banner).toHaveAttribute('aria-label', `banner ${expected.kind}`);
  if (expected.kind === 'warning') {
    expect(banner).toHaveTextContent(expected.text);
    return;
  }

  const restartButton = within(banner as HTMLElement).queryByRole('button', {
    name: 'Restart Replication'
  });
  if (expected.restartButton === 'none') {
    expect(restartButton).not.toBeInTheDocument();
  } else {
    expect(restartButton).toBeInTheDocument();
    if (expected.restartButton === 'enabled') {
      expect(restartButton).toBeEnabled();
    } else {
      expect(restartButton).toBeDisabled();
    }
  }
};

describe('xCluster banner stories', () => {
  beforeAll(async () => {
    await i18n.use(initReactI18next).init({
      resources: { en },
      lng: 'en',
      interpolation: { escapeValue: false }
    });
  });

  afterEach(() => {
    window.localStorage.removeItem('customerId');
  });

  const xClusterLagText = 'xCluster replication lag is above the alert threshold.';
  it.each<[XClusterStoryName, ExpectedBanner]>([
    ['Healthy', { kind: 'none' }],
    ['NonProblemStatuses', { kind: 'none' }],
    ['LagAboveThreshold', { kind: 'warning', text: xClusterLagText }],
    ['LagWithoutAlertConfig', { kind: 'none' }],
    ['LagRequestsFail', { kind: 'none' }],
    ['UninitializedTableOnly', { kind: 'none' }],
    ['SystemErrorWithUninitializedTable', { kind: 'error', restartButton: 'none' }],
    ['ProblemTablesWithoutMissingOp', { kind: 'error', restartButton: 'none' }],
    ['MissingOp', { kind: 'error', restartButton: 'enabled' }],
    ['MissingOpWithOtherIssuesAndLag', { kind: 'error', restartButton: 'enabled' }],
    ['RestartNotAllowed', { kind: 'error', restartButton: 'disabled' }]
  ])('XClusterBannerSection %s', async (storyName, expected) => {
    await renderStory(xClusterStory(storyName));
    expectBanner(expected);
  });

  it.each<[DrStoryName, ExpectedBanner]>([
    ['Healthy', { kind: 'none' }],
    [
      'LagAboveThreshold',
      { kind: 'warning', text: 'DR replication lag is above the alert threshold.' }
    ],
    ['MissingOp', { kind: 'error', restartButton: 'enabled' }],
    ['HaltedWithMissingOp', { kind: 'none' }],
    ['SwitchoverInProgressWithIssues', { kind: 'none' }]
  ])('DrBannerSection %s', async (storyName, expected) => {
    await renderStory(drStory(storyName));
    expectBanner(expected);
  });

  it('DrBannerSection HaltedWithMissingOp shows the halted banner instead', async () => {
    await renderStory(drStory('HaltedWithMissingOp'));
    expect(screen.getByRole('button', { name: 'Repair DR' })).toBeInTheDocument();
  });

  it('DrBannerSection SwitchoverInProgressWithIssues shows the in-progress banner instead', async () => {
    await renderStory(drStory('SwitchoverInProgressWithIssues'));
    expect(screen.getByText('Switchover in progress...')).toBeInTheDocument();
  });
});
