import { ReactNode } from 'react';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';

import { SupportBundle, SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import en from '@app/translations/en.json';

import { SupportBundlesModal } from './SupportBundlesModal';

void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  interpolation: { escapeValue: false }
});

const api = vi.hoisted(() => ({
  pageListSupportBundles: vi.fn(),
  deleteSupportBundle: vi.fn()
}));
vi.mock('@app/v2/api/support-bundle/support-bundle', () => api);
vi.mock('@app/redesign/features/rbac/common/RbacApiPermValidator', () => ({
  RbacValidator: ({ children }: { children: ReactNode }) => <>{children}</>
}));
// The real formatter reads the user's timezone from the redux store.
vi.mock('@app/redesign/helpers/DateUtils', () => ({
  YBTimeFormats: { YB_LONG_DATETIME: 'long' },
  ybFormatDate: (date: string) => date
}));

const bundle = (uuid: string, status: SupportBundleStatus): SupportBundle => ({
  info: {
    uuid,
    status,
    creation_date: status === SupportBundleStatus.Success ? '2026-07-26T14:32:00Z' : undefined,
    expiration_date: status === SupportBundleStatus.Success ? '2099-01-01T00:00:00Z' : undefined,
    size_in_bytes: status === SupportBundleStatus.Success ? 4_100_000_000 : 0
  },
  spec: { components: [], end_date: '2026-07-26T14:32:00Z' }
});

const renderModal = (props: Partial<Parameters<typeof SupportBundlesModal>[0]> = {}) => {
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  const onShareBundle = vi.fn();
  render(
    <YBThemeProvider theme={yba.ybaTheme}>
      <QueryClientProvider client={queryClient}>
        <SupportBundlesModal
          universeUuid="universe-1"
          onClose={vi.fn()}
          onShareBundle={onShareBundle}
          {...props}
        />
      </QueryClientProvider>
    </YBThemeProvider>
  );
  return { onShareBundle };
};

describe('SupportBundlesModal', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('shows the empty state when the universe has no bundles', async () => {
    api.pageListSupportBundles.mockResolvedValue({ entities: [], total_count: 0 });
    const onCreateBundle = vi.fn();
    renderModal({ onCreateBundle });

    expect(await screen.findByText('No support bundles yet')).toBeInTheDocument();
    fireEvent.click(screen.getByTestId('SupportBundlesEmptyState-Create'));
    expect(onCreateBundle).toHaveBeenCalled();
  });

  it('lists bundles with their status, size and share action', async () => {
    const ready = bundle('ready-1', SupportBundleStatus.Success);
    api.pageListSupportBundles.mockResolvedValue({
      entities: [ready, bundle('running-1', SupportBundleStatus.Running)],
      total_count: 2
    });
    const { onShareBundle } = renderModal();

    expect(await screen.findByText('2 bundles')).toBeInTheDocument();
    expect(screen.getByText('Ready')).toBeInTheDocument();
    expect(screen.getByText('Creating…')).toBeInTheDocument();
    expect(screen.getByText('4.1 GB')).toBeInTheDocument();

    expect(screen.getByTestId('SupportBundleRow-Share-running-1')).toBeDisabled();
    fireEvent.click(screen.getByTestId('SupportBundleRow-Share-ready-1'));
    expect(onShareBundle).toHaveBeenCalledWith(ready);
  });

  it('blocks deleting a running bundle', async () => {
    api.pageListSupportBundles.mockResolvedValue({
      entities: [bundle('running-1', SupportBundleStatus.Running)],
      total_count: 1
    });
    renderModal();

    expect(await screen.findByTestId('SupportBundleRow-Delete-running-1')).toBeDisabled();
  });

  it('deletes a bundle after confirmation', async () => {
    api.pageListSupportBundles.mockResolvedValue({
      entities: [bundle('ready-1', SupportBundleStatus.Success)],
      total_count: 1
    });
    api.deleteSupportBundle.mockResolvedValue(undefined);
    renderModal();

    fireEvent.click(await screen.findByTestId('SupportBundleRow-Delete-ready-1'));
    fireEvent.click(await screen.findByTestId('DeleteSupportBundleModal-Submit'));

    await waitFor(() =>
      expect(api.deleteSupportBundle).toHaveBeenCalledWith('universe-1', 'ready-1')
    );
  });
  it('lists the oldest bundles first when sorted by name', async () => {
    api.pageListSupportBundles.mockImplementation((_universeUuid, { direction }) =>
      Promise.resolve({
        entities: [bundle(direction === 'ASC' ? 'oldest' : 'newest', SupportBundleStatus.Success)],
        total_count: 1
      })
    );
    renderModal();
    await screen.findByTestId('SupportBundleRow-newest');

    fireEvent.click(screen.getByRole('button', { name: /Sorted by Name/ }));

    expect(await screen.findByTestId('SupportBundleRow-oldest')).toBeInTheDocument();
  });

  it('shows the next page of bundles', async () => {
    api.pageListSupportBundles.mockImplementation((_universeUuid, { offset }) =>
      Promise.resolve({
        entities: [
          bundle(offset === 0 ? 'first-page' : 'second-page', SupportBundleStatus.Success)
        ],
        total_count: 30
      })
    );
    renderModal();
    await screen.findByTestId('SupportBundleRow-first-page');

    fireEvent.click(screen.getByRole('button', { name: 'Go to page 2' }));

    expect(await screen.findByTestId('SupportBundleRow-second-page')).toBeInTheDocument();
  });

  it('says so when the bundles fail to load', async () => {
    api.pageListSupportBundles.mockRejectedValue(new Error('boom'));
    renderModal();

    expect(await screen.findByText('Failed to load support bundles.')).toBeInTheDocument();
    expect(screen.queryByTestId('SupportBundlesModal-Create')).toBeNull();
  });
});
