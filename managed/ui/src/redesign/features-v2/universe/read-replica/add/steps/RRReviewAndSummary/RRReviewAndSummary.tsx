import { forwardRef, useCallback, useContext, useImperativeHandle, useMemo } from 'react';
import { useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { ArchitectureType } from '@app/components/configRedesign/providerRedesign/constants';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType, InstanceType, Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import type { UniverseResourceDetails } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getUniverseResources, useAddCluster, useEditUniverse } from '@app/v2/api/universe/universe';
import { getReadOnlyCluster } from '@app/redesign/utils/universeUtils';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import {
  StepsRef,
  AddRRContext,
  AddRRContextMethods,
  AddReadReplicaSteps
} from '../../AddReadReplicaContext';
import { getClusterByType } from '../../../../edit-universe/EditUniverseUtils';
import {
  mapAddReadReplicaClusterPayload,
  mapEditReadReplicaClusterSpec,
  sumReadReplicaNodeCounts
} from '../../addReadReplicaClusterPayload';
import {
  buildUniverseSpecCurrentStatePricing,
  buildUniverseSpecForReadReplicaPricing,
  getReadReplicaPricingFingerprint
} from '../../buildUniverseSpecForReadReplicaPricing';
import {
  ReviewAndSummaryComponent,
  ReviewItem
} from '../../../../create-universe/steps/review-summary/ReviewAndSummaryComponent';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { useGetZones } from '@app/redesign/features-v2/universe/create-universe/fields/instance-type/InstanceTypeFieldHelper';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';

import ReplicaIcon from '@app/redesign/assets/copy.svg';
import ClusterIcon from '@app/redesign/assets/clusters.svg';

import { createErrorMessage } from '@app/redesign/features/universe/universe-form/utils/helpers';
import { getReadReplicaExitRoute } from '../../../readReplicaUtils';

const { Box } = mui;

const MONTHLY_COST_MULTIPLIER = 30;

/** API may omit fields or use numeric strings; avoid hiding the whole row on strict typeof checks. */
function finiteMetric(v: unknown): number | undefined {
  if (v === null || v === undefined) return undefined;
  const n = typeof v === 'number' ? v : Number(v);
  return Number.isFinite(n) ? n : undefined;
}

type ResourceMetricKey = 'num_nodes' | 'num_cores' | 'mem_size_gb' | 'volume_size_gb' | 'price_per_hour';

type RrHardwareMetricKey = 'num_cores' | 'mem_size_gb' | 'volume_size_gb';

type RrSpecHardwareTotals = { cores?: number; memGb?: number; storageGb?: number };

const RESOURCE_METRIC_CAMEL: Record<ResourceMetricKey, string> = {
  num_nodes: 'numNodes',
  num_cores: 'numCores',
  mem_size_gb: 'memSizeGb',
  volume_size_gb: 'volumeSizeGb',
  price_per_hour: 'pricePerHour'
};

/** Read metrics from v2 resource payload (snake_case or camelCase). */
function readResourceMetric(
  d: UniverseResourceDetails | null | undefined,
  key: ResourceMetricKey
): number | undefined {
  if (!d) return undefined;
  const rec = d as unknown as Record<string, unknown>;
  const raw = rec[key] ?? rec[RESOURCE_METRIC_CAMEL[key]];
  // YBA may JSON-serialize numeric fields as null; treat as 0 for inventory (not price).
  if (raw === null && key !== 'price_per_hour') {
    return 0;
  }
  return finiteMetric(raw);
}

function finiteHourlyPrice(d: UniverseResourceDetails | undefined): number | undefined {
  return readResourceMetric(d, 'price_per_hour');
}

export const RRReviewAndSummary = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    {
      universeUuid,
      universeData,
      regionsAndAZ,
      instanceSettings,
      databaseSettings,
      activeStep
    },
    { moveToPreviousPage }
  ] = (useContext(AddRRContext) as unknown) as AddRRContextMethods;

  const inheritPrimaryHardware = Boolean(instanceSettings?.inheritPrimaryInstance);

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });

  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const providerUUID = primaryCluster?.provider_spec?.provider ?? '';

  const { data: regionsList = [], isLoading: isRegionsLoading } = useQuery(
    [QUERY_KEY.getRegionsList, providerUUID],
    () => api.getRegionsList(providerUUID),
    // Keep the same cache as the Regions step so submit/pricing always resolve region metadata,
    // even if the user reaches Review without revisiting placement.
    { enabled: Boolean(providerUUID) }
  );

  const pricingContext = useMemo(
    () => ({
      universeUuid,
      universeData,
      regionsAndAZ,
      instanceSettings,
      databaseSettings,
      activeStep
    }),
    [universeUuid, universeData, regionsAndAZ, instanceSettings, databaseSettings, activeStep]
  );

  const primarySpec = useMemo(
    () => buildUniverseSpecCurrentStatePricing(universeData),
    [universeData]
  );

  const rrPricingFingerprint = useMemo(
    () => getReadReplicaPricingFingerprint(pricingContext, regionsList as Region[]),
    [pricingContext, regionsList]
  );

  const proposedAsyncClusterUuid = useMemo(
    () => crypto.randomUUID(),
    [rrPricingFingerprint]
  );

  const pricingSpec = useMemo(
    () =>
      buildUniverseSpecForReadReplicaPricing(
        pricingContext,
        regionsList as Region[],
        proposedAsyncClusterUuid
      ),
    [pricingContext, regionsList, proposedAsyncClusterUuid]
  );

  const { data: primaryPricingData, isLoading: isPrimaryPricingLoading } = useQuery(
    ['readReplicaPricingPrimary', universeUuid, primarySpec],
    () => getUniverseResources(primarySpec!),
    {
      enabled: Boolean(primarySpec) && activeStep === AddReadReplicaSteps.REVIEW,
      refetchOnWindowFocus: false,
      retry: false,
      keepPreviousData: true
    }
  );

  const fullQueryEnabled =
    Boolean(pricingSpec) &&
    activeStep === AddReadReplicaSteps.REVIEW &&
    !isRegionsLoading &&
    Boolean(regionsList.length);

  const { data: fullPricingData, isLoading: isFullPricingLoading } = useQuery(
    ['readReplicaPricing', universeUuid, pricingSpec],
    () => getUniverseResources(pricingSpec!),
    {
      enabled: fullQueryEnabled,
      refetchOnWindowFocus: false,
      retry: false,
      keepPreviousData: true
    }
  );

  const addCluster = useAddCluster();
  const editUniverse = useEditUniverse();

  const existingReadReplicaCluster = useMemo(
    () => getReadOnlyCluster(universeData?.spec?.clusters ?? []),
    [universeData?.spec?.clusters]
  );

  const mapPins = useMemo(() => {
    const list = regionsList as Region[];
    if (!regionsAndAZ?.regions?.length || !list.length) return [];
    const byUuid = new Map(list.map((r) => [r.uuid, r]));
    return regionsAndAZ.regions
      .map((r) => (r.regionUuid ? byUuid.get(r.regionUuid) : undefined))
      .filter((r): r is Region => Boolean(r));
  }, [regionsAndAZ, regionsList]);

  const providerForInstanceTypes = useMemo<Partial<ProviderType>>(
    () => ({
      uuid: providerUUID,
      code: (primaryCluster?.placement_spec?.cloud_list?.[0]?.code ?? '') as CloudType
    }),
    [providerUUID, primaryCluster?.placement_spec?.cloud_list]
  );

  const { zones, isLoadingZones } = useGetZones(providerForInstanceTypes, mapPins);
  const zoneNames = useMemo(() => zones.map((z) => z.name), [zones]);

  const { osPatchingEnabled } = useRuntimeConfigValues(providerUUID);
  const cpuArch = instanceSettings?.arch ?? null;

  const instanceTypesQueryEnabled =
    activeStep === AddReadReplicaSteps.REVIEW &&
    !inheritPrimaryHardware &&
    Boolean(providerUUID) &&
    zoneNames.length > 0 &&
    !isLoadingZones;

  const { data: instanceTypesData } = useQuery(
    [
      QUERY_KEY.getInstanceTypes,
      providerUUID,
      JSON.stringify(zoneNames),
      osPatchingEnabled ? cpuArch : null,
      'addRRReview'
    ],
    () =>
      api.getInstanceTypes(
        providerUUID,
        zoneNames,
        osPatchingEnabled ? ((cpuArch as ArchitectureType | null) ?? null) : null
      ),
    { enabled: instanceTypesQueryEnabled, refetchOnWindowFocus: false }
  );

  const instanceTypes: InstanceType[] = instanceTypesData ?? [];

  const buildAddPayload = useCallback(() => {
    return mapAddReadReplicaClusterPayload(
      {
        universeUuid,
        universeData,
        regionsAndAZ,
        instanceSettings,
        databaseSettings,
        activeStep
      },
      regionsList as Region[]
    );
  }, [
    universeUuid,
    universeData,
    regionsAndAZ,
    instanceSettings,
    databaseSettings,
    activeStep,
    regionsList
  ]);

  const buildEditPayload = useCallback(() => {
    if (!existingReadReplicaCluster?.uuid) {
      throw new Error('READ_REPLICA_CLUSTER_MISSING');
    }
    return {
      expected_universe_version: -1,
      clusters: [
        mapEditReadReplicaClusterSpec(
          existingReadReplicaCluster.uuid,
          {
            universeUuid,
            universeData,
            regionsAndAZ,
            instanceSettings,
            databaseSettings,
            activeStep
          },
          regionsList as Region[],
          { enforceNumNodesFloor: true }
        )
      ]
    };
  }, [
    existingReadReplicaCluster?.uuid,
    universeUuid,
    universeData,
    regionsAndAZ,
    instanceSettings,
    databaseSettings,
    activeStep,
    regionsList
  ]);

  const redirectToUniverse = useCallback(() => {
    window.location.href = getReadReplicaExitRoute(universeUuid);
  }, [universeUuid]);

  const runSubmitMutation = useCallback(
    (mutationPromise: Promise<unknown>, errorPrefix: string) =>
      mutationPromise.then(redirectToUniverse).catch((error) => {
        console.error(errorPrefix, error);
      }),
    [redirectToUniverse]
  );

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        if (!universeUuid) {
          toast.error(t('validation.universeUuidMissing'));
          return Promise.resolve();
        }
        let addPayload;
        let editPayload;
        try {
          if (existingReadReplicaCluster?.uuid) {
            editPayload = buildEditPayload();
          } else {
            addPayload = buildAddPayload();
          }
        } catch (e) {
          toast.error(createErrorMessage(e));
          return Promise.resolve();
        }

        if (existingReadReplicaCluster?.uuid && editPayload) {
          return runSubmitMutation(
            editUniverse.mutateAsync(
              { uniUUID: universeUuid, data: editPayload },
              {
                onError(error: any) {
                  toast.error(error?.response?.data?.error ?? t('toast.updateRRFailed'));
                }
              }
            ),
            'Update read replica failed:'
          );
        }

        return runSubmitMutation(
          addCluster.mutateAsync(
            { uniUUID: universeUuid, data: addPayload! },
            {
              onError(error: any) {
                toast.error(error?.response?.data?.error ?? t('toast.addRRFailed'));
              }
            }
          ),
          'Add read replica failed:'
        );
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    [
      addCluster,
      editUniverse,
      existingReadReplicaCluster?.uuid,
      buildAddPayload,
      buildEditPayload,
      moveToPreviousPage,
      runSubmitMutation,
      t,
      universeUuid
    ]
  );

  const isPageLoading =
    isRegionsLoading ||
    (Boolean(primarySpec) &&
      activeStep === AddReadReplicaSteps.REVIEW &&
      isPrimaryPricingLoading) ||
    (Boolean(pricingSpec) &&
      activeStep === AddReadReplicaSteps.REVIEW &&
      !isRegionsLoading &&
      Boolean(regionsList.length) &&
      isFullPricingLoading);

  const primaryHourly = finiteHourlyPrice(primaryPricingData);
  const fullHourly = finiteHourlyPrice(fullPricingData);
  const hasPrimaryCost = primaryHourly !== undefined;
  const hasFullCost = fullHourly !== undefined;

  const dash = '-';

  const incremental = useMemo(() => {
    if (!fullPricingData || !primaryPricingData) {
      return { hourly: 0, nodes: 0, cores: 0, memGb: 0, storageGb: 0 };
    }
    const ph = readResourceMetric(primaryPricingData, 'price_per_hour');
    const fh = readResourceMetric(fullPricingData, 'price_per_hour');
    const hourly =
      ph !== undefined && fh !== undefined ? Math.max(0, fh - ph) : 0;
    const pn = readResourceMetric(primaryPricingData, 'num_nodes');
    const fn = readResourceMetric(fullPricingData, 'num_nodes');
    const pc = readResourceMetric(primaryPricingData, 'num_cores');
    const fc = readResourceMetric(fullPricingData, 'num_cores');
    const pm = readResourceMetric(primaryPricingData, 'mem_size_gb');
    const fm = readResourceMetric(fullPricingData, 'mem_size_gb');
    const ps = readResourceMetric(primaryPricingData, 'volume_size_gb');
    const fs = readResourceMetric(fullPricingData, 'volume_size_gb');
    return {
      hourly,
      nodes: fn !== undefined && pn !== undefined ? Math.max(0, fn - pn) : 0,
      cores: fc !== undefined && pc !== undefined ? Math.max(0, fc - pc) : 0,
      memGb: fm !== undefined && pm !== undefined ? Math.max(0, fm - pm) : 0,
      storageGb: fs !== undefined && ps !== undefined ? Math.max(0, fs - ps) : 0
    };
  }, [fullPricingData, primaryPricingData]);

  const totalRRNodes = regionsAndAZ ? sumReadReplicaNodeCounts(regionsAndAZ) : 0;

  const rrSpecHardwareTotals = useMemo((): RrSpecHardwareTotals | null => {
    if (inheritPrimaryHardware || totalRRNodes <= 0 || !instanceSettings) {
      return null;
    }
    const di = instanceSettings.deviceInfo;
    const volSize = finiteMetric(di?.volumeSize);
    const numVol = finiteMetric(di?.numVolumes);
    const storageGb =
      volSize !== undefined && numVol !== undefined ? volSize * numVol * totalRRNodes : undefined;

    const k8 = instanceSettings.tserverK8SNodeResourceSpec;
    const cpuK = finiteMetric(k8?.cpuCoreCount);
    const memK = finiteMetric(k8?.memoryGib);
    if (cpuK !== undefined && memK !== undefined) {
      return {
        cores: cpuK * totalRRNodes,
        memGb: memK * totalRRNodes,
        storageGb
      };
    }

    const inst = instanceSettings.instanceType;
    const match = inst
      ? instanceTypes.find((i) => i.instanceTypeCode === inst)
      : undefined;
    const perCores = match ? finiteMetric(match.numCores) : undefined;
    const perMem = match ? finiteMetric(match.memSizeGB) : undefined;
    if (perCores !== undefined && perMem !== undefined) {
      return {
        cores: perCores * totalRRNodes,
        memGb: perMem * totalRRNodes,
        storageGb
      };
    }
    if (storageGb !== undefined) {
      return { storageGb };
    }
    return null;
  }, [inheritPrimaryHardware, totalRRNodes, instanceSettings, instanceTypes]);

  const reviewItems: ReviewItem[] = useMemo(() => {
    const items: ReviewItem[] = [];
    const nodesLabel = t('reviewSummary.totalNodes');
    const coresLabel = t('reviewSummary.totalCores');
    const memLabel = t('reviewSummary.totalMemory');
    const storageLabel = t('reviewSummary.totalStorage');

    const p = primaryPricingData;
    const f = fullPricingData;
    const pNodes = readResourceMetric(p, 'num_nodes');
    const pCores = readResourceMetric(p, 'num_cores');
    const pMem = readResourceMetric(p, 'mem_size_gb');
    const pVol = readResourceMetric(p, 'volume_size_gb');
    const primarySpecCluster = universeData
      ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
      : undefined;
    const primaryNodesFromSpec = finiteMetric(primarySpecCluster?.num_nodes);
    const primaryNodesForScale = pNodes ?? primaryNodesFromSpec;

    /** Linear estimate: primary-universe metric × (RR nodes / primary nodes). */
    const scaleMetricFromPrimaryNodes = (primaryMetric: number | undefined): string | undefined => {
      if (
        primaryMetric === undefined ||
        primaryNodesForScale === undefined ||
        primaryNodesForScale <= 0 ||
        totalRRNodes <= 0
      ) {
        return undefined;
      }
      const v = (primaryMetric * totalRRNodes) / primaryNodesForScale;
      return String(Math.round(v * 10) / 10);
    };

    /**
     * YBA often returns identical cores/mem/disk on primary-only vs full+RR pricing calls, so
     * incremental delta is 0. When "same as primary" is on, scale from primary universe totals.
     * When custom RR hardware is selected, derive totals from instance settings + instance-types
     * API (or K8 custom resources); do not scale primary metrics or the RR row mirrors primary.
     */
    const rrHardwareCell = (
      metricKey: RrHardwareMetricKey,
      incrementalVal: number,
      primaryMetric: number | undefined
    ): string => {
      const fromPrimary = scaleMetricFromPrimaryNodes(primaryMetric);
      const hasPair =
        readResourceMetric(f, metricKey) !== undefined &&
        readResourceMetric(p, metricKey) !== undefined;

      if (inheritPrimaryHardware) {
        if (fromPrimary !== undefined) {
          return fromPrimary;
        }
        if (hasPair) {
          return String(incrementalVal);
        }
        return dash;
      }

      const specVal =
        metricKey === 'num_cores'
          ? rrSpecHardwareTotals?.cores
          : metricKey === 'mem_size_gb'
            ? rrSpecHardwareTotals?.memGb
            : rrSpecHardwareTotals?.storageGb;

      if (hasPair && incrementalVal !== 0) {
        return String(incrementalVal);
      }
      if (specVal !== undefined && Number.isFinite(specVal)) {
        return String(Math.round(specVal * 10) / 10);
      }
      if (hasPair) {
        return String(incrementalVal);
      }
      return dash;
    };

    items.push({
      name: t('reviewCostSummary.primaryCluster'),
      icon: <ClusterIcon />,
      attributes: [
        {
          name: nodesLabel,
          value:
            pNodes !== undefined
              ? String(pNodes)
              : primaryNodesFromSpec !== undefined
                ? String(primaryNodesFromSpec)
                : dash
        },
        {
          name: coresLabel,
          value: pCores !== undefined ? String(pCores) : dash
        },
        {
          name: memLabel,
          value: pMem !== undefined ? String(pMem) : dash
        },
        {
          name: storageLabel,
          value: pVol !== undefined ? String(pVol) : dash
        }
      ],
      dailyCost: hasPrimaryCost ? (primaryHourly! * 24).toFixed(2) : dash,
      monthlyCost: hasPrimaryCost
        ? (primaryHourly! * 24 * MONTHLY_COST_MULTIPLIER).toFixed(2)
        : dash
    });

    const rrCoresDisplay = rrHardwareCell('num_cores', incremental.cores, pCores);
    const rrMemDisplay = rrHardwareCell('mem_size_gb', incremental.memGb, pMem);
    const rrStorageDisplay = rrHardwareCell(
      'volume_size_gb',
      incremental.storageGb,
      pVol
    );

    items.push({
      name: t('reviewCostSummary.readReplicaTitle'),
      icon: <ReplicaIcon />,
      attributes: [
        {
          name: nodesLabel,
          value:
            readResourceMetric(f, 'num_nodes') !== undefined &&
            readResourceMetric(p, 'num_nodes') !== undefined
              ? String(incremental.nodes)
              : totalRRNodes > 0
                ? String(totalRRNodes)
                : dash
        },
        {
          name: coresLabel,
          value: rrCoresDisplay
        },
        {
          name: memLabel,
          value: rrMemDisplay
        },
        {
          name: storageLabel,
          value: rrStorageDisplay
        }
      ],
      dailyCost:
        readResourceMetric(f, 'price_per_hour') !== undefined &&
        readResourceMetric(p, 'price_per_hour') !== undefined
          ? (incremental.hourly * 24).toFixed(2)
          : dash,
      monthlyCost:
        readResourceMetric(f, 'price_per_hour') !== undefined &&
        readResourceMetric(p, 'price_per_hour') !== undefined
          ? (incremental.hourly * 24 * MONTHLY_COST_MULTIPLIER).toFixed(2)
          : dash
    });

    return items;
  }, [
    t,
    universeData,
    primaryPricingData,
    fullPricingData,
    totalRRNodes,
    incremental,
    dash,
    hasPrimaryCost,
    hasFullCost,
    inheritPrimaryHardware,
    primaryHourly,
    fullHourly,
    rrSpecHardwareTotals
  ]);

  const totalDailyCost = hasFullCost ? (fullHourly! * 24).toFixed(2) : dash;
  const totalMonthlyCost = hasFullCost
    ? (fullHourly! * 24 * MONTHLY_COST_MULTIPLIER).toFixed(2)
    : dash;

  if (isPageLoading) {
    return <YBLoadingCircleIcon />;
  }

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
      <div style={{ padding: '24px 24px 0 0' }}>
        <RRBreadCrumbs groupTitle={t('review')} subTitle={t('summaryAndCost')} />
      </div>
      <ReviewAndSummaryComponent
        regions={mapPins}
        reviewItems={reviewItems}
        totalDailyCost={totalDailyCost}
        totalMonthlyCost={totalMonthlyCost}
        summaryTranslationKeyPrefix="readReplica.addRR.reviewSummary"
        mapLegendLabel={t('legendSecondary')}
        mapsDataTestId="yb-maps-rr-review-summary"
      />
    </Box>
  );
});

RRReviewAndSummary.displayName = 'RRReviewAndSummary';
