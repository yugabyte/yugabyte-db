import { forwardRef, useContext, useImperativeHandle, useMemo } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { ArchitectureType } from '@app/components/configRedesign/providerRedesign/constants';
import { ClusterSpecClusterType, NodeDetailsDedicatedTo } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType, InstanceType, Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import type { UniverseResourceDetails, Universe, ClusterSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getUniverseResources } from '@app/v2/api/universe/universe';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import {
  StepsRef,
  AddRRContext,
  AddRRContextMethods,
  AddReadReplicaSteps
} from '../../AddReadReplicaContext';
import {
  countMasterAndTServerNodes,
  getClusterByType
} from '../../../../edit-universe/EditUniverseUtils';
import { sumReadReplicaNodeCounts } from '../../addReadReplicaClusterPayload';
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

import { useSubmitReadReplica } from '../../useSubmitReadReplica';

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

/**
 * When dedicated masters are enabled, API/spec `num_nodes` is T-Server count only.
 * Match create-universe review: display total = tservers + masters (masters ≈ RF).
 */
function getDedicatedPrimaryNodeTotal(
  universeData: Universe | undefined,
  primary: ClusterSpec | undefined,
  apiNumNodes: number | undefined
): number | undefined {
  if (!primary?.node_spec?.dedicated_nodes) {
    return undefined;
  }

  const fromDetails = universeData
    ? countMasterAndTServerNodes(universeData, primary)
    : undefined;
  const tFromDetails = fromDetails?.[NodeDetailsDedicatedTo.TSERVER] ?? 0;
  const mFromDetails = fromDetails?.[NodeDetailsDedicatedTo.MASTER] ?? 0;

  const tserver =
    tFromDetails > 0
      ? tFromDetails
      : apiNumNodes ?? finiteMetric(primary.num_nodes);
  const master =
    mFromDetails > 0 ? mFromDetails : finiteMetric(primary.replication_factor);

  if (tserver === undefined || master === undefined) {
    return undefined;
  }
  return tserver + master;
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
  const isK8s =
    primaryCluster?.placement_spec?.cloud_list?.[0]?.code === CloudType.kubernetes;
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

  const proposedRrPricingUuid = useMemo(() => {
    if (typeof crypto !== 'undefined' && 'randomUUID' in crypto) {
      return crypto.randomUUID();
    }
    return `00000000-0000-4000-8000-${Date.now().toString(16).padStart(12, '0').slice(-12)}`;
  }, [rrPricingFingerprint]);

  const pricingSpec = useMemo(
    () =>
      buildUniverseSpecForReadReplicaPricing(
        pricingContext,
        regionsList as Region[],
        proposedRrPricingUuid
      ),
    [pricingContext, regionsList, proposedRrPricingUuid]
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

  /** RR-only fetch-universe-resources (primary omitted from clusters). */
  const { data: rrPricingData, isLoading: isRrPricingLoading } = useQuery(
    ['readReplicaPricing', universeUuid, pricingSpec],
    () => getUniverseResources(pricingSpec!),
    {
      enabled: fullQueryEnabled,
      refetchOnWindowFocus: false,
      retry: false,
      keepPreviousData: true
    }
  );

  const { submit } = useSubmitReadReplica();

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

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () =>
        submit(
          {
            universeUuid,
            universeData,
            regionsAndAZ,
            instanceSettings,
            databaseSettings,
            activeStep
          },
          regionsList as Region[]
        ),
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    [
      submit,
      universeUuid,
      universeData,
      regionsAndAZ,
      instanceSettings,
      databaseSettings,
      activeStep,
      regionsList,
      moveToPreviousPage
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
      isRrPricingLoading);

  const primaryHourly = finiteHourlyPrice(primaryPricingData);
  const rrHourly = finiteHourlyPrice(rrPricingData);
  const hasPrimaryCost = primaryHourly !== undefined;
  const hasRrCost = rrHourly !== undefined;
  const hasTotalCost = hasPrimaryCost && hasRrCost;
  const totalHourly =
    hasTotalCost ? (primaryHourly as number) + (rrHourly as number) : undefined;

  const dash = '-';

  const rrMetrics = useMemo(() => {
    if (!rrPricingData) {
      return { hourly: 0, nodes: 0, cores: 0, memGb: 0, storageGb: 0 };
    }
    return {
      hourly: readResourceMetric(rrPricingData, 'price_per_hour') ?? 0,
      nodes: readResourceMetric(rrPricingData, 'num_nodes') ?? 0,
      cores: readResourceMetric(rrPricingData, 'num_cores') ?? 0,
      memGb: readResourceMetric(rrPricingData, 'mem_size_gb') ?? 0,
      storageGb: readResourceMetric(rrPricingData, 'volume_size_gb') ?? 0
    };
  }, [rrPricingData]);

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
    const nodesLabel = t(isK8s ? 'reviewSummary.totalPods' : 'reviewSummary.totalNodes');
    const coresLabel = t('reviewSummary.totalCores');
    const memLabel = t('reviewSummary.totalMemory');
    const storageLabel = t('reviewSummary.totalStorage');

    const p = primaryPricingData;
    const rr = rrPricingData;
    const pNodes = readResourceMetric(p, 'num_nodes');
    const pCores = readResourceMetric(p, 'num_cores');
    const pMem = readResourceMetric(p, 'mem_size_gb');
    const pVol = readResourceMetric(p, 'volume_size_gb');
    const primarySpecCluster = universeData
      ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
      : undefined;
    const primaryNodesFromSpec = finiteMetric(primarySpecCluster?.num_nodes);
    const primaryNodesForScale = pNodes ?? primaryNodesFromSpec;
    const dedicatedPrimaryNodes = getDedicatedPrimaryNodeTotal(
      universeData,
      primarySpecCluster as ClusterSpec | undefined,
      pNodes
    );
    const primaryNodesDisplay =
      dedicatedPrimaryNodes !== undefined
        ? String(dedicatedPrimaryNodes)
        : pNodes !== undefined
          ? String(pNodes)
          : primaryNodesFromSpec !== undefined
            ? String(primaryNodesFromSpec)
            : dash;

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
     * Prefer RR-only fetch-universe metrics. When "same as primary" is on and the API
     * omits cores/mem/disk, scale from primary universe totals. When custom RR hardware
     * is selected, fall back to instance settings + instance-types API (or K8 resources).
     */
    const rrHardwareCell = (
      metricKey: RrHardwareMetricKey,
      rrMetricVal: number,
      primaryMetric: number | undefined
    ): string => {
      const fromPrimary = scaleMetricFromPrimaryNodes(primaryMetric);
      const hasRrMetric = readResourceMetric(rr, metricKey) !== undefined;

      if (inheritPrimaryHardware) {
        if (hasRrMetric && rrMetricVal !== 0) {
          return String(rrMetricVal);
        }
        if (fromPrimary !== undefined) {
          return fromPrimary;
        }
        if (hasRrMetric) {
          return String(rrMetricVal);
        }
        return dash;
      }

      const specVal =
        metricKey === 'num_cores'
          ? rrSpecHardwareTotals?.cores
          : metricKey === 'mem_size_gb'
            ? rrSpecHardwareTotals?.memGb
            : rrSpecHardwareTotals?.storageGb;

      if (hasRrMetric && rrMetricVal !== 0) {
        return String(rrMetricVal);
      }
      if (specVal !== undefined && Number.isFinite(specVal)) {
        return String(Math.round(specVal * 10) / 10);
      }
      if (hasRrMetric) {
        return String(rrMetricVal);
      }
      return dash;
    };

    items.push({
      name: t('reviewCostSummary.primaryCluster'),
      icon: <ClusterIcon />,
      attributes: [
        {
          name: nodesLabel,
          value: primaryNodesDisplay
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

    const rrCoresDisplay = rrHardwareCell('num_cores', rrMetrics.cores, pCores);
    const rrMemDisplay = rrHardwareCell('mem_size_gb', rrMetrics.memGb, pMem);
    const rrStorageDisplay = rrHardwareCell(
      'volume_size_gb',
      rrMetrics.storageGb,
      pVol
    );

    items.push({
      name: t('reviewCostSummary.readReplicaTitle'),
      icon: <ReplicaIcon />,
      attributes: [
        {
          name: nodesLabel,
          value:
            readResourceMetric(rr, 'num_nodes') !== undefined
              ? String(rrMetrics.nodes)
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
      dailyCost: hasRrCost ? (rrMetrics.hourly * 24).toFixed(2) : dash,
      monthlyCost: hasRrCost
        ? (rrMetrics.hourly * 24 * MONTHLY_COST_MULTIPLIER).toFixed(2)
        : dash
    });

    return items;
  }, [
    t,
    isK8s,
    universeData,
    primaryPricingData,
    rrPricingData,
    totalRRNodes,
    rrMetrics,
    dash,
    hasPrimaryCost,
    hasRrCost,
    inheritPrimaryHardware,
    primaryHourly,
    rrSpecHardwareTotals
  ]);

  const totalDailyCost = hasTotalCost ? (totalHourly! * 24).toFixed(2) : dash;
  const totalMonthlyCost = hasTotalCost
    ? (totalHourly! * 24 * MONTHLY_COST_MULTIPLIER).toFixed(2)
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
