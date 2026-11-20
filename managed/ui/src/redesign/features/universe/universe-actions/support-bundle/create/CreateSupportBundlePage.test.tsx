import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';

import { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import en from '@app/translations/en.json';

import { CreateSupportBundlePage } from './CreateSupportBundlePage';

void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  interpolation: { escapeValue: false }
});

const api = vi.hoisted(() => ({
  estimateSupportBundleSize: vi.fn(),
  createSupportBundle: vi.fn(),
  pageListSupportBundles: vi.fn(),
  deleteSupportBundle: vi.fn()
}));
vi.mock('@app/v2/api/support-bundle/support-bundle', () => api);
vi.mock('@app/redesign/helpers/DateUtils', () => ({
  YBTimeFormats: { YB_LONG_DATETIME: 'long' },
  ybFormatDate: () => 'Jul 26, 2026'
}));

const VM_CONTEXT = { isK8sUniverse: false, isPerfAdvisorRegistered: false };

const universe = {
  universeUUID: 'universe-1',
  name: 'puppy-food',
  universeDetails: {
    nodeDetailsSet: [
      { nodeName: 'n1', cloudInfo: { region: 'us-east-1' } },
      { nodeName: 'n2', cloudInfo: { region: 'us-west-1' } }
    ]
  }
} as unknown as Universe;

const renderPage = () => {
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  const onCreated = vi.fn();
  render(
    <YBThemeProvider theme={yba.ybaTheme}>
      <QueryClientProvider client={queryClient}>
        <CreateSupportBundlePage
          universe={universe}
          context={VM_CONTEXT}
          onClose={vi.fn()}
          onCreated={onCreated}
        />
      </QueryClientProvider>
    </YBThemeProvider>
  );
  return { onCreated };
};

const openSelect = (testId: string) =>
  fireEvent.mouseDown(within(screen.getByTestId(testId)).getByRole('combobox'));

const lastEstimateSpec = () => api.estimateSupportBundleSize.mock.calls.at(-1)?.[1];

describe('CreateSupportBundlePage', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    // Not implemented by jsdom.
    Element.prototype.scrollIntoView = vi.fn();
    api.estimateSupportBundleSize.mockResolvedValue({
      data: { YBA: { ApplicationLogs: 1_300_000_000 }, n1: { UniverseLogs: 5_800_000_000 } }
    });
  });

  it('estimates the recommended bundle as soon as the page opens', async () => {
    renderPage();

    expect(await screen.findByText('≈ 7.1 GB')).toBeInTheDocument();
  });

  it('adds metrics and their time range when Performance Metrics is checked', async () => {
    renderPage();
    await screen.findByText('≈ 7.1 GB');

    fireEvent.click(
      within(screen.getByTestId('CreateSupportBundlePage-Category-performanceMetrics')).getByRole(
        'checkbox'
      )
    );

    expect(screen.getByTestId('CreateSupportBundlePage-MetricsTimeRange')).toBeInTheDocument();
    await waitFor(() => expect(lastEstimateSpec().components).toContain('PrometheusMetrics'), {
      timeout: 2000
    });
  });

  it('leaves out a recommended category the user unchecks', async () => {
    renderPage();
    await screen.findByText('≈ 7.1 GB');

    fireEvent.click(
      within(screen.getByTestId('CreateSupportBundlePage-Category-clusterLogs')).getByRole(
        'checkbox'
      )
    );

    await waitFor(() => expect(lastEstimateSpec().components).not.toContain('UniverseLogs'), {
      timeout: 2000
    });
    expect(lastEstimateSpec().components).toContain('ApplicationLogs');
  });

  it('warns about bundles over 20 GB and opens the node picker to narrow them', async () => {
    api.estimateSupportBundleSize.mockResolvedValue({
      data: { n1: { UniverseLogs: 25_000_000_000 } }
    });
    renderPage();

    fireEvent.click(await screen.findByTestId('SupportBundleSummaryPanel-ScopeNodes'));

    expect(await screen.findByRole('listbox')).toBeInTheDocument();
    expect(screen.getByText('us-east-1')).toBeInTheDocument();
  });

  it('sends only the nodes the user keeps', async () => {
    renderPage();
    await screen.findByText('≈ 7.1 GB');

    openSelect('CreateSupportBundlePage-Nodes');
    fireEvent.click(await screen.findByTestId('CreateSupportBundlePage-Nodes-n2'));

    await waitFor(() => expect(lastEstimateSpec().node_names).toEqual(['n1']), { timeout: 2000 });
  });

  it('holds off estimating and creating until a custom range is complete', async () => {
    renderPage();
    await screen.findByText('≈ 7.1 GB');
    const estimateCount = api.estimateSupportBundleSize.mock.calls.length;

    openSelect('CreateSupportBundlePage-TimeRange');
    fireEvent.click(await screen.findByRole('option', { name: 'Custom range' }));

    expect(screen.getByTestId('SupportBundleSummaryPanel-Create')).toBeDisabled();
    await new Promise((resolve) => setTimeout(resolve, 700));
    expect(api.estimateSupportBundleSize).toHaveBeenCalledTimes(estimateCount);
  });

  it('re-estimates on Refresh estimate', async () => {
    renderPage();
    await screen.findByText('≈ 7.1 GB');
    const estimateCount = api.estimateSupportBundleSize.mock.calls.length;

    fireEvent.click(screen.getByTestId('SupportBundleSummaryPanel-Refresh'));

    await waitFor(
      () => expect(api.estimateSupportBundleSize).toHaveBeenCalledTimes(estimateCount + 1),
      { timeout: 2000 }
    );
  });

  it('stays open when the bundle cannot be created', async () => {
    api.createSupportBundle.mockRejectedValue(new Error('Bad request'));
    const { onCreated } = renderPage();
    await screen.findByText('≈ 7.1 GB');

    fireEvent.click(screen.getByTestId('SupportBundleSummaryPanel-Create'));

    await waitFor(() => expect(api.createSupportBundle).toHaveBeenCalled());
    await waitFor(() =>
      expect(screen.getByTestId('SupportBundleSummaryPanel-Create')).toBeEnabled()
    );
    expect(onCreated).not.toHaveBeenCalled();
  });
});
