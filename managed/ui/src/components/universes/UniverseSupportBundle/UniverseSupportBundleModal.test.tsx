import { render, screen, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import { mainTheme } from '@app/redesign/theme/mainTheme';

import { UniverseSupportBundleModal } from './UniverseSupportBundleModal';

const adminApi = vi.hoisted(() => ({ fetchGlobalRunTimeConfigs: vi.fn() }));
// The legacy wizard lists bundles as it mounts; leaving it pending keeps it on its loader.
const listSupportBundles = vi.hoisted(() => vi.fn(() => new Promise(() => {})));
vi.mock('../../../api/admin', () => adminApi);

vi.mock(
  '../../../redesign/features/universe/universe-actions/support-bundle',
  async (importOriginal) => ({
    ...(await importOriginal<object>()),
    SupportBundleV2: () => <div data-testid="SupportBundleV2" />,
    listSupportBundles
  })
);
vi.mock('@app/redesign/features/PerfAdvisor/api', async (importOriginal) => {
  const original = await importOriginal<typeof import('@app/redesign/features/PerfAdvisor/api')>();
  return {
    ...original,
    PerfAdvisorAPI: { fetchUniverseRegistrationDetails: () => new Promise(() => {}) }
  };
});

const universe = {
  universeUUID: 'universe-1',
  name: 'puppy-food',
  universeDetails: { universeUUID: 'universe-1', nodeDetailsSet: [], clusters: [] }
};

const renderModal = ({ v2Api, redesign }: { v2Api: boolean; redesign: boolean }) => {
  adminApi.fetchGlobalRunTimeConfigs.mockResolvedValue({
    data: {
      configEntries: [
        { key: 'yb.support_bundle.ui_v2_enabled', value: String(v2Api) },
        { key: 'yb.ui.feature_flags.support_bundle_redesign', value: String(redesign) }
      ]
    }
  });
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <ThemeProvider theme={mainTheme}>
      <QueryClientProvider client={queryClient}>
        <UniverseSupportBundleModal currentUniverse={universe} closeModal={vi.fn()} />
      </QueryClientProvider>
    </ThemeProvider>
  );
};

describe('UniverseSupportBundleModal', () => {
  beforeEach(() => vi.clearAllMocks());

  it('opens the redesigned UI only with both the v2 API key and the redesign flag', async () => {
    renderModal({ v2Api: true, redesign: true });

    expect(await screen.findByTestId('SupportBundleV2')).toBeInTheDocument();
    expect(listSupportBundles).not.toHaveBeenCalled();
  });

  it('keeps the legacy wizard on the v2 API while the redesign flag is off', async () => {
    renderModal({ v2Api: true, redesign: false });

    await waitFor(() =>
      expect(listSupportBundles).toHaveBeenCalledWith('universe-1', true, expect.any(Number))
    );
    expect(screen.queryByTestId('SupportBundleV2')).toBeNull();
  });

  it('keeps the legacy wizard on v1 when the v2 API key is off, whatever the redesign flag', async () => {
    renderModal({ v2Api: false, redesign: true });

    await waitFor(() =>
      expect(listSupportBundles).toHaveBeenCalledWith('universe-1', false, expect.any(Number))
    );
    expect(screen.queryByTestId('SupportBundleV2')).toBeNull();
  });
});
