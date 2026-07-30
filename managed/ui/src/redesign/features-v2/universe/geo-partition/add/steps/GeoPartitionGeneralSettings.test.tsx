import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import React, { useMemo, useState } from 'react';
import { describe, expect, it, vi } from 'vitest';
import { QueryClient, QueryClientProvider } from 'react-query';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextProps,
  AddGeoPartitionSteps,
  GeoPartition,
  addGeoPartitionFormMethods,
  initialAddGeoPartitionFormState
} from '../AddGeoPartitionContext';
import { ClusterSpecClusterType, UniverseRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  FaultToleranceType,
  ResilienceFormMode,
  ResilienceType
} from '../../../create-universe/steps/resilence-regions/dtos';

const mockMoveToNextPage = vi.fn();
const mockMoveToPreviousPage = vi.fn();

vi.mock('react-i18next', () => ({
  useTranslation: (_ns: string, opts?: { keyPrefix?: string }) => ({
    t: (key: string, extra?: { keyPrefix?: string }) => {
      if (extra?.keyPrefix === 'common') {
        return key;
      }
      return opts?.keyPrefix ? `${opts.keyPrefix}.${key}` : key;
    },
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({
    children,
    i18nKey
  }: {
    children?: React.ReactNode;
    i18nKey?: string;
  }) => (
    <span data-testid={`trans-${i18nKey ?? 'unknown'}`}>{children ?? i18nKey}</span>
  )
}));

vi.mock('../AddGeoPartitionUtils', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../AddGeoPartitionUtils')>();
  return {
    ...actual,
    useGeoPartitionNavigation: () => ({
      moveToNextPage: (ctx: AddGeoPartitionContextProps) => mockMoveToNextPage(ctx),
      moveToPreviousPage: () => mockMoveToPreviousPage()
    }),
    navigateToUniverseSettingsFromWizard: vi.fn()
  };
});

vi.mock('@yugabyte-ui-library/core', () => {
  const Box = ({ children, ...rest }: React.ComponentProps<'div'>) => (
    <div {...rest}>{children}</div>
  );
  const Typography = ({ children }: { children?: React.ReactNode }) => <span>{children}</span>;
  const styled = () => () => {
    const Cmp = ({ children, ...props }: React.ComponentProps<'div'>) => (
      <div {...props}>{children}</div>
    );
    return Cmp;
  };
  return {
    mui: {
      Box,
      Typography,
      styled,
      typographyClasses: { root: 'typography-root' }
    },
    yba: {},
    YBInputField: ({
      name,
      label,
      value,
      onChange,
      control: _c,
      ...rest
    }: {
      name: string;
      label?: string;
      value?: string;
      onChange?: (e: React.ChangeEvent<HTMLInputElement>) => void;
      control?: unknown;
      dataTestId?: string;
    }) => (
      <label>
        {label}
        <input
          data-testid={rest.dataTestId ?? name}
          name={name}
          value={value ?? ''}
          onChange={onChange}
        />
      </label>
    ),
    YBInput: () => null,
    YBTag: ({ children }: { children?: React.ReactNode }) => (
      <span data-testid="yb-tag">{children}</span>
    )
  };
});

vi.mock('@app/redesign/assets/book_open_blue.svg', () => ({ default: () => null }));

vi.mock('../../../create-universe/components/UniverseActionButtons', () => ({
  UniverseActionButtons: ({
    nextButton,
    prevButton,
    cancelButton
  }: {
    nextButton?: { text?: string; onClick: () => void; disabled?: boolean };
    prevButton?: { text?: string; onClick: () => void; disabled?: boolean };
    cancelButton?: { text?: string; onClick: () => void };
  }) => (
    <div>
      {cancelButton && (
        <button type="button" data-testid="create-universe-cancel-button" onClick={cancelButton.onClick}>
          {cancelButton.text}
        </button>
      )}
      {prevButton && (
        <button
          type="button"
          data-testid="create-universe-back-button"
          onClick={prevButton.onClick}
          disabled={prevButton.disabled}
        >
          {prevButton.text}
        </button>
      )}
      {nextButton && (
        <button type="button" data-testid="create-universe-next-button" onClick={nextButton.onClick}>
          {nextButton.text}
        </button>
      )}
    </div>
  )
}));

import { GeoPartitionGeneralSettings } from './GeoPartitionGeneralSettings';

function universeWithPartitions(partitionsSpec: unknown[] | undefined): UniverseRespResponse {
  return {
    info: { universe_uuid: 'u-1', arch: 'x86_64' } as any,
    spec: {
      clusters: [
        {
          cluster_type: ClusterSpecClusterType.PRIMARY,
          replication_factor: 3,
          provider_spec: { provider: 'p1' },
          placement_spec: { cloud_list: [{ code: 'aws', uuid: 'c1', region_list: [] }] },
          partitions_spec: partitionsSpec
        }
      ]
    } as any
  } as UniverseRespResponse;
}

function rowWithRegions(name: string): GeoPartition {
  const base = initialAddGeoPartitionFormState.geoPartitions[0];
  return {
    ...base,
    name,
    tablespaceName: 'ts_main',
    resilience: {
      ...base.resilience!,
      resilienceType: ResilienceType.REGULAR,
      resilienceFormMode: ResilienceFormMode.GUIDED,
      faultToleranceType: FaultToleranceType.AZ_LEVEL,
      resilienceFactor: 3,
      nodeCount: 1,
      regions: [
        {
          code: 'us-west-2',
          name: 'US West',
          uuid: 'reg-1',
          latitude: 0,
          longitude: 0,
          zones: []
        } as any
      ]
    }
  };
}

function Provider({
  ctx,
  children
}: {
  ctx: AddGeoPartitionContextProps;
  children: React.ReactNode;
}) {
  const methods = useMemo(() => addGeoPartitionFormMethods(ctx), [ctx]);
  const value = [ctx, methods] as any;
  return (
    <AddGeoPartitionContext.Provider value={value}>{children}</AddGeoPartitionContext.Provider>
  );
}

function StatefulProvider({ initial }: { initial: AddGeoPartitionContextProps }) {
  const [ctx, setCtx] = useState(initial);
  const methods = useMemo(() => {
    const base = addGeoPartitionFormMethods(ctx);
    return {
      ...base,
      addGeoPartition: (gp: GeoPartition) => {
        const next = base.addGeoPartition(gp);
        setCtx(next);
        return next;
      }
    };
  }, [ctx]);

  const value = [ctx, methods] as any;
  return (
    <AddGeoPartitionContext.Provider value={value}>
      <GeoPartitionGeneralSettings />
    </AddGeoPartitionContext.Provider>
  );
}

const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });

function renderGeneral(ctx: AddGeoPartitionContextProps) {
  return render(
    <QueryClientProvider client={queryClient}>
      <Provider ctx={ctx}>
        <GeoPartitionGeneralSettings />
      </Provider>
    </QueryClientProvider>
  );
}

describe('GeoPartitionGeneralSettings', () => {
  it('shows Primary tag, help banner, and read-only region tags for default primary row', () => {
    const ctx: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: true,
      activeGeoPartitionIndex: 0,
      activeStep: AddGeoPartitionSteps.GENERAL_SETTINGS,
      universeData: universeWithPartitions([]),
      geoPartitions: [rowWithRegions('Primary partition')]
    };
    renderGeneral(ctx);
    expect(screen.getByText('Primary')).toBeInTheDocument();
    expect(screen.getByTestId('trans-helpText')).toBeInTheDocument();
    expect(
      screen.getByText(/geoPartition\.geoPartitionGeneralSettings\.existingRegions/)
    ).toBeInTheDocument();
    expect(screen.getByText(/US West/)).toBeInTheDocument();
    expect(screen.getByText(/us-west-2/)).toBeInTheDocument();
  });

  it('shows addNewGeoPartition as next label when only the default primary row exists', () => {
    const ctx: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: true,
      activeGeoPartitionIndex: 0,
      activeStep: AddGeoPartitionSteps.GENERAL_SETTINGS,
      universeData: universeWithPartitions([]),
      geoPartitions: [rowWithRegions('Primary partition')]
    };
    renderGeneral(ctx);
    expect(screen.getByTestId('create-universe-next-button')).toHaveTextContent(
      'geoPartition.geoPartitionGeneralSettings.addNewGeoPartition'
    );
  });

  it('shows common next label when more than one wizard partition exists', () => {
    const ctx: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: true,
      activeGeoPartitionIndex: 0,
      activeStep: AddGeoPartitionSteps.GENERAL_SETTINGS,
      universeData: universeWithPartitions([]),
      geoPartitions: [
        rowWithRegions('Primary partition'),
        {
          ...initialAddGeoPartitionFormState.geoPartitions[0],
          name: 'Geo Partition 2',
          tablespaceName: 'Tablespace_2'
        }
      ]
    };
    renderGeneral(ctx);
    expect(screen.getByTestId('create-universe-next-button')).toHaveTextContent('next');
  });

  it('clicking add partition switches next button to common next label', async () => {
    const initial: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: true,
      activeGeoPartitionIndex: 0,
      activeStep: AddGeoPartitionSteps.GENERAL_SETTINGS,
      universeData: universeWithPartitions([]),
      geoPartitions: [rowWithRegions('Primary partition')]
    };
    render(
      <QueryClientProvider client={queryClient}>
        <StatefulProvider initial={initial} />
      </QueryClientProvider>
    );
    fireEvent.click(screen.getByTestId('create-universe-next-button'));
    await waitFor(() => {
      expect(screen.getByTestId('create-universe-next-button')).toHaveTextContent('next');
    });
  });
});
