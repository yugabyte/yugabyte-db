import { fireEvent, render, screen } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';

import {
  createSupportBundleMock,
  createSupportBundleUniverseMock
} from '@app/mocks/mock-data/supportBundleMocks';
import { SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import en from '@app/translations/en.json';

import { SupportBundleReadyWatcher } from './ready/SupportBundleReadyWatcher';
import { SupportBundleV2 } from './SupportBundleV2';

void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  interpolation: { escapeValue: false }
});

const api = vi.hoisted(() => ({
  pageListSupportBundles: vi.fn(),
  estimateSupportBundleSize: vi.fn(),
  createSupportBundle: vi.fn(),
  deleteSupportBundle: vi.fn(),
  getSupportBundle: vi.fn()
}));
vi.mock('@app/v2/api/support-bundle/support-bundle', () => api);
const paApi = vi.hoisted(() => ({ fetchUniverseRegistrationDetails: vi.fn() }));
vi.mock('@app/redesign/features/PerfAdvisor/api', async (importOriginal) => ({
  ...(await importOriginal<object>()),
  PerfAdvisorAPI: paApi
}));
vi.mock('@app/redesign/helpers/DateUtils', () => ({
  YBTimeFormats: { YB_LONG_DATETIME: 'long' },
  ybFormatDate: (date: string) => String(date)
}));

const renderFlow = () => {
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <YBThemeProvider theme={yba.ybaTheme}>
      <QueryClientProvider client={queryClient}>
        <SupportBundleV2 universe={createSupportBundleUniverseMock()} onClose={vi.fn()} />
        <SupportBundleReadyWatcher universeUuid="universe-uuid" universeNodeCount={9} />
      </QueryClientProvider>
    </YBThemeProvider>
  );
};

describe('SupportBundleV2', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    Element.prototype.scrollIntoView = vi.fn();
    api.pageListSupportBundles.mockResolvedValue({ entities: [], total_count: 0 });
    api.estimateSupportBundleSize.mockResolvedValue({ data: {} });
    paApi.fetchUniverseRegistrationDetails.mockRejectedValue({ response: { status: 404 } });
  });

  it('opens the create page from the list and returns to it on close', async () => {
    renderFlow();

    fireEvent.click(await screen.findByTestId('SupportBundlesEmptyState-Create'));
    expect(await screen.findByTestId('CreateSupportBundlePage')).toBeInTheDocument();

    fireEvent.click(screen.getByTestId('CreateSupportBundlePage-Close'));
    expect(await screen.findByText('No support bundles yet')).toBeInTheDocument();
  });

  it('shows the new bundle in the list once it is submitted', async () => {
    api.createSupportBundle.mockResolvedValue({ task_uuid: 'task-1', resource_uuid: 'sb-new' });
    renderFlow();
    fireEvent.click(await screen.findByTestId('SupportBundlesEmptyState-Create'));
    api.pageListSupportBundles.mockResolvedValue({
      entities: [createSupportBundleMock('sb-new', SupportBundleStatus.Running)],
      total_count: 1
    });

    fireEvent.click(await screen.findByTestId('SupportBundleSummaryPanel-Create'));

    expect(await screen.findByTestId('SupportBundleRow-sb-new')).toBeInTheDocument();
    expect(screen.queryByTestId('CreateSupportBundlePage')).toBeNull();
  });

  it('shares a ready bundle from the list', async () => {
    api.pageListSupportBundles.mockResolvedValue({
      entities: [createSupportBundleMock('sb-ready', SupportBundleStatus.Success)],
      total_count: 1
    });
    renderFlow();

    fireEvent.click(await screen.findByTestId('SupportBundleRow-Share-sb-ready'));

    expect(await screen.findByText('Share Bundle')).toBeInTheDocument();
    expect(screen.getByTestId('ShareBundleModal-Submit')).toBeDisabled();
  });

  it('announces the bundle it created once the bundle is ready', async () => {
    api.createSupportBundle.mockResolvedValue({ task_uuid: 'task-1', resource_uuid: 'sb-new' });
    api.getSupportBundle.mockResolvedValue(
      createSupportBundleMock('sb-new', SupportBundleStatus.Success)
    );
    renderFlow();

    fireEvent.click(await screen.findByTestId('SupportBundlesEmptyState-Create'));
    fireEvent.click(await screen.findByTestId('SupportBundleSummaryPanel-Create'));

    expect(await screen.findByTestId('SupportBundleReadyBanner')).toBeInTheDocument();
  });
});
