import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';

import {
  SupportBundle,
  SupportBundleComponentType,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import en from '@app/translations/en.json';

import { addPendingSupportBundle } from '../ready/pendingSupportBundles';
import { SupportBundleReadyWatcher } from '../ready/SupportBundleReadyWatcher';
import { ShareBundleModal } from './ShareBundleModal';

void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  interpolation: { escapeValue: false }
});

const api = vi.hoisted(() => ({ getSupportBundle: vi.fn() }));
const toast = vi.hoisted(() => ({ error: vi.fn(), success: vi.fn() }));
vi.mock('react-toastify', () => ({ toast }));
vi.mock('@app/v2/api/support-bundle/support-bundle', () => api);
const download = vi.hoisted(() => ({ downloadSupportBundle: vi.fn() }));
vi.mock('../supportBundleApi', () => download);
vi.mock('@app/redesign/helpers/DateUtils', () => ({
  YBTimeFormats: { YB_LONG_DATETIME: 'long' },
  ybFormatDate: (date: string) => date
}));

const bundle = (
  components: SupportBundleComponentType[],
  status: SupportBundleStatus = SupportBundleStatus.Success
): SupportBundle => ({
  info: { uuid: 'sb-1', status, size_in_bytes: 63_000_000_000 },
  spec: {
    components,
    start_date: '2026-09-16T20:01:00Z',
    end_date: '2026-09-17T20:01:00Z'
  }
});

const wrap = (children: JSX.Element) => {
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <YBThemeProvider theme={yba.ybaTheme}>
      <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
    </YBThemeProvider>
  );
};

describe('ShareBundleModal', () => {
  beforeEach(() => vi.clearAllMocks());

  it('downloads a bundle without sensitive components straight away', () => {
    const onClose = vi.fn();
    wrap(
      <ShareBundleModal
        universeUuid="universe-1"
        bundle={bundle([SupportBundleComponentType.GFlags])}
        onClose={onClose}
      />
    );

    expect(screen.queryByTestId('ShareBundleModal-SensitiveWarning')).toBeNull();
    fireEvent.click(screen.getByTestId('ShareBundleModal-Submit'));

    expect(download.downloadSupportBundle).toHaveBeenCalledWith('universe-1', 'sb-1', true);
    expect(onClose).toHaveBeenCalled();
  });

  it('requires acknowledgement before sharing core dumps', () => {
    wrap(
      <ShareBundleModal
        universeUuid="universe-1"
        bundle={bundle([SupportBundleComponentType.CoreFiles])}
        onClose={vi.fn()}
      />
    );

    expect(screen.getByText('Unredacted components included')).toBeInTheDocument();
    expect(screen.getByTestId('ShareBundleModal-Submit')).toBeDisabled();
    fireEvent.click(screen.getByRole('checkbox'));
    expect(screen.getByTestId('ShareBundleModal-Submit')).toBeEnabled();
  });

  it('asks for the acknowledgement again after the modal is cancelled', () => {
    wrap(
      <ShareBundleModal
        universeUuid="universe-1"
        bundle={bundle([SupportBundleComponentType.CoreFiles])}
        onClose={vi.fn()}
      />
    );

    fireEvent.click(screen.getByRole('checkbox'));
    fireEvent.click(screen.getByTestId('ShareBundleModal-Cancel'));

    expect(screen.getByRole('checkbox')).not.toBeChecked();
    expect(screen.getByTestId('ShareBundleModal-Submit')).toBeDisabled();
    expect(download.downloadSupportBundle).not.toHaveBeenCalled();
  });
});

describe('SupportBundleReadyWatcher', () => {
  beforeEach(() => vi.clearAllMocks());

  it('announces a bundle created in this session once it is ready', async () => {
    api.getSupportBundle.mockResolvedValue(bundle([SupportBundleComponentType.GFlags]));
    wrap(<SupportBundleReadyWatcher universeUuid="universe-2" universeNodeCount={30} />);

    expect(screen.queryByTestId('SupportBundleReadyBanner')).toBeNull();
    act(() => addPendingSupportBundle('universe-2', 'sb-1'));

    expect(await screen.findByText('Support bundle ready')).toBeInTheDocument();
    expect(screen.getByText(/63 GB · collected from 30 nodes/)).toBeInTheDocument();

    fireEvent.click(screen.getByTestId('SupportBundleReadyBanner-Share'));
    expect(await screen.findByText('Share Bundle')).toBeInTheDocument();
    await waitFor(() => expect(screen.queryByTestId('SupportBundleReadyBanner')).toBeNull());
  });

  it('reports a bundle that failed instead of announcing it', async () => {
    api.getSupportBundle.mockResolvedValue(
      bundle([SupportBundleComponentType.GFlags], SupportBundleStatus.Failed)
    );
    wrap(<SupportBundleReadyWatcher universeUuid="universe-3" universeNodeCount={30} />);

    act(() => addPendingSupportBundle('universe-3', 'sb-1'));

    await waitFor(() => expect(toast.error).toHaveBeenCalled());
    expect(toast.error.mock.calls[0][0]).toMatch(/could not be created/);
    expect(screen.queryByTestId('SupportBundleReadyBanner')).toBeNull();
  });

  it('waits while the bundle is still being collected', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    try {
      api.getSupportBundle
        .mockResolvedValueOnce(
          bundle([SupportBundleComponentType.GFlags], SupportBundleStatus.Running)
        )
        .mockResolvedValue(bundle([SupportBundleComponentType.GFlags]));
      wrap(<SupportBundleReadyWatcher universeUuid="universe-6" universeNodeCount={30} />);

      act(() => addPendingSupportBundle('universe-6', 'sb-1'));
      await waitFor(() => expect(api.getSupportBundle).toHaveBeenCalledTimes(1));
      expect(screen.queryByTestId('SupportBundleReadyBanner')).toBeNull();

      await act(async () => {
        await vi.advanceTimersByTimeAsync(10_000);
      });

      expect(await screen.findByTestId('SupportBundleReadyBanner')).toBeInTheDocument();
    } finally {
      vi.useRealTimers();
    }
  });
});
