import { act, render, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from 'react-query';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import {
  SupportBundleComponentType,
  SupportBundleCreateSpec,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { useSupportBundleEstimate, useSupportBundleList } from './useSupportBundles';

const api = vi.hoisted(() => ({
  pageListSupportBundles: vi.fn(),
  estimateSupportBundleSize: vi.fn()
}));
vi.mock('@app/v2/api/support-bundle/support-bundle', () => api);

const page = (status: SupportBundleStatus) => ({
  entities: [{ info: { uuid: 'sb-1', status }, spec: { components: [] } }],
  total_count: 1
});

const spec = (components: SupportBundleComponentType[]): SupportBundleCreateSpec => ({
  components,
  start_date: '2026-07-25T00:00:00Z',
  end_date: '2026-07-26T00:00:00Z'
});

/** Renders a hook and returns a ref to its latest result (RTL 12 has no renderHook). */
const renderHook = <P extends object, R>(hook: (props: P) => R, initialProps: P) => {
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  const result: { current?: R } = {};
  const Harness = (props: P) => {
    result.current = hook(props);
    return null;
  };
  const view = render(
    <QueryClientProvider client={queryClient}>
      <Harness {...initialProps} />
    </QueryClientProvider>
  );
  const rerender = (props: P) =>
    view.rerender(
      <QueryClientProvider client={queryClient}>
        <Harness {...props} />
      </QueryClientProvider>
    );
  return { result, rerender };
};

describe('useSupportBundleList', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers({ shouldAdvanceTime: true });
  });
  afterEach(() => vi.useRealTimers());

  it('polls while a bundle is running and stops once none are', async () => {
    api.pageListSupportBundles
      .mockResolvedValueOnce(page(SupportBundleStatus.Running))
      .mockResolvedValue(page(SupportBundleStatus.Success));
    const { result } = renderHook(() => useSupportBundleList('universe-1', 0), {});

    await waitFor(() => expect(result.current?.data).toBeDefined());
    expect(api.pageListSupportBundles).toHaveBeenCalledTimes(1);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(10_000);
    });
    await waitFor(() =>
      expect(result.current?.data?.entities?.[0].info?.status).toBe(SupportBundleStatus.Success)
    );
    expect(api.pageListSupportBundles).toHaveBeenCalledTimes(2);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(30_000);
    });
    expect(api.pageListSupportBundles).toHaveBeenCalledTimes(2);
  });
});

describe('useSupportBundleEstimate', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers({ shouldAdvanceTime: true });
    api.estimateSupportBundleSize.mockResolvedValue({ data: {} });
  });
  afterEach(() => vi.useRealTimers());

  it('estimates the initial spec, then only the edit the user settles on', async () => {
    const { result, rerender } = renderHook(
      ({ estimateSpec }: { estimateSpec: SupportBundleCreateSpec }) =>
        useSupportBundleEstimate('universe-1', estimateSpec),
      { estimateSpec: spec([SupportBundleComponentType.GFlags]) }
    );
    await waitFor(() => expect(result.current?.isEstimating).toBe(false));
    expect(api.estimateSupportBundleSize).toHaveBeenCalledTimes(1);

    rerender({ estimateSpec: spec([SupportBundleComponentType.ConsensusMeta]) });
    rerender({ estimateSpec: spec([SupportBundleComponentType.TabletMeta]) });
    expect(result.current?.isEstimating).toBe(true);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(500);
    });

    await waitFor(() => expect(result.current?.isEstimating).toBe(false));
    expect(api.estimateSupportBundleSize).toHaveBeenCalledTimes(2);
    expect(api.estimateSupportBundleSize).toHaveBeenLastCalledWith(
      'universe-1',
      spec([SupportBundleComponentType.TabletMeta])
    );
    expect(result.current?.asOf).toBeInstanceOf(Date);
  });
});
