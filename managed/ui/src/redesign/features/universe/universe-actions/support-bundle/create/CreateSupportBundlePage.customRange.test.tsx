import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';

import { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import en from '@app/translations/en.json';

import { TimeRangePreset } from '../model/formState';
import { CreateSupportBundlePage } from './CreateSupportBundlePage';

void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  interpolation: { escapeValue: false }
});

// The date pickers are hard to drive in jsdom; this stand-in sets a complete custom range.
vi.mock('../components/TimeRangeField', () => ({
  TimeRangeField: ({
    onChange,
    dataTestId
  }: {
    onChange: (value: unknown) => void;
    dataTestId: string;
  }) => (
    <button
      data-testid={dataTestId}
      onClick={() =>
        onChange({
          preset: TimeRangePreset.CUSTOM,
          customStart: new Date('2026-07-25T00:00:00Z'),
          customEnd: new Date('2026-07-26T00:00:00Z')
        })
      }
    />
  )
}));

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

const lastEstimateSpec = () => api.estimateSupportBundleSize.mock.calls.at(-1)?.[1];

describe('CreateSupportBundlePage with a custom time range', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    Element.prototype.scrollIntoView = vi.fn();
    api.estimateSupportBundleSize.mockResolvedValue({
      data: { YBA: { ApplicationLogs: 1_300_000_000 }, n1: { UniverseLogs: 5_800_000_000 } }
    });
  });

  it('re-estimates on Refresh estimate even though the range does not move', async () => {
    renderPage();
    fireEvent.click(screen.getByTestId('CreateSupportBundlePage-TimeRange'));
    await waitFor(() => expect(lastEstimateSpec().start_date).toBe('2026-07-25T00:00:00.000Z'), {
      timeout: 2000
    });
    await screen.findByText('\u2248 7.1 GB');
    const estimateCount = api.estimateSupportBundleSize.mock.calls.length;

    fireEvent.click(screen.getByTestId('SupportBundleSummaryPanel-Refresh'));

    await waitFor(
      () => expect(api.estimateSupportBundleSize).toHaveBeenCalledTimes(estimateCount + 1),
      { timeout: 2000 }
    );
  });
});
