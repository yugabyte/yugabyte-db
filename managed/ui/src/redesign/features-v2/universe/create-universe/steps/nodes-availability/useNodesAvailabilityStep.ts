import {
  useCallback,
  useContext,
  useEffect,
  useImperativeHandle,
  useMemo,
  useRef,
  useState,
  type ForwardedRef
} from 'react';
import { isEmpty, values } from 'lodash';
import { useMount } from 'react-use';
import type { TFunction } from 'i18next';
import { useTranslation } from 'react-i18next';
import { useForm, type UseFormReturn } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useQuery } from 'react-query';
import { MarkerType, useGetMapIcons } from '@yugabyte-ui-library/core';
import {
  assignRegionsAZNodeByReplicationFactor,
  getExpertAvailabilityZonesOrEmpty,
  getFaultToleranceNeeded,
  getNodeCount,
  inferResilience,
  reduceExpertNodeCountsToAtMostRf
} from '../../CreateUniverseUtils';
import { getGuidedNodesStepReplicationFactor } from '../resilence-regions/GuidedResilienceRequirementSummary';
import { NodesAvailabilitySchema } from './ValidationSchema';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { usePersistStepFormValues } from '../../helpers/persistStepFormValues';
import { NodeAvailabilityProps } from './dtos';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { ResilienceFormMode, type ResilienceAndRegionsProps } from '../resilence-regions/dtos';
import {
  ENABLE_MULTI_PROVIDER_CREATE,
  getPrimaryProvider,
  getProviderConfigurations,
  isMultiProvider
} from '../../helpers/providerConfigurations';
import { withEffectiveRegions } from '../../helpers/placementsByProvider';
import {
  AVAILABILITY_ZONES_BY_PROVIDER,
  REPLICATION_FACTOR,
  RESILIENCE_FACTOR
} from '../../fields/FieldNames';
import {
  availabilityZonesByProviderFromNodes,
  buildNodesByProvider,
  countZonesInByProvider,
  flattenAvailabilityZonesByProvider,
  partitionAvailabilityZonesByProvider,
  singularNodesFromMultiForm,
  unflattenAvailabilityZonesByProvider,
  type AvailabilityZonesByProvider
} from '../../helpers/nodesByProvider';
import { AZ_NOT_PREFERRED, AZ_PREFFERED_HIGHEST_RANK } from '../../helpers/constants';
import { CloudType } from '../../../../../helpers/dtos';
import { NodeAgentAPI, QUERY_KEY as NodeAgentQueryKey } from '../../../../../features/NodeAgent/api';
import {
  computeFreeNodesByAzUuid,
  findFirstOnPremNodeViolation
} from './validation/onPrem';
import { useRuntimeConfigValues } from '../../helpers/utils';

/** Fields that drive guided-mode AZ/node layout from the Regions step. */
function getGuidedPlacementSyncSignature(r: ResilienceAndRegionsProps): string {
  return JSON.stringify({
    rf: r.resilienceFactor,
    ft: r.faultToleranceType,
    rt: r.resilienceType,
    nc: r.nodeCount,
    saz: r.singleAvailabilityZone,
    reg: (r.regions ?? []).map((x) => x.uuid).sort()
  });
}

function regionCodesMatchAvailabilityZones(
  regions: Region[],
  availabilityZones: NodeAvailabilityProps['availabilityZones'] | undefined
): boolean {
  const selectedCodes = new Set(regions.map((r) => r.code));
  const zoneKeys = new Set(Object.keys(availabilityZones ?? {}));
  if (selectedCodes.size !== zoneKeys.size) {
    return false;
  }
  for (const code of zoneKeys) {
    if (!selectedCodes.has(code)) {
      return false;
    }
  }
  return true;
}

function regionsMatchAvailabilityZonesByProvider(
  regions: Region[],
  byProvider: AvailabilityZonesByProvider | null | undefined
): boolean {
  const expected = new Set(
    regions
      .filter((region) => !!region.providerUuid)
      .map((region) => `${region.providerUuid}:${region.code}`)
  );
  const actual = new Set<string>();
  for (const [providerUuid, zonesByRegion] of Object.entries(byProvider ?? {})) {
    for (const regionCode of Object.keys(zonesByRegion ?? {})) {
      actual.add(`${providerUuid}:${regionCode}`);
    }
  }
  if (expected.size !== actual.size) return false;
  for (const key of expected) {
    if (!actual.has(key)) return false;
  }
  return true;
}

/** Initializes or refreshes placement from resilience settings when zones are empty (or cleared). */
export function applyNodesStepPlacementFromResilience(
  methods: UseFormReturn<NodeAvailabilityProps>,
  resilienceAndRegionsSettings: ResilienceAndRegionsProps,
  saveResilienceAndRegionsSettings: (data: ResilienceAndRegionsProps) => void,
  options?: {
    multiPlacement?: boolean;
    providers?: ReturnType<typeof getProviderConfigurations>;
    enablePrimaryRf9?: boolean;
  }
): void {
  const isExpert =
    resilienceAndRegionsSettings.resilienceFormMode === ResilienceFormMode.EXPERT_MODE;
  const multiPlacement = !!options?.multiPlacement;
  const providers = options?.providers ?? [];
  const enablePrimaryRf9 = options?.enablePrimaryRf9 ?? false;
  const regions = resilienceAndRegionsSettings.regions ?? [];

  if (resilienceAndRegionsSettings.resilienceFormMode === ResilienceFormMode.GUIDED) {
    methods.setValue(
      REPLICATION_FACTOR,
      getGuidedNodesStepReplicationFactor(
        resilienceAndRegionsSettings.faultToleranceType,
        resilienceAndRegionsSettings.resilienceFactor
      )
    );
  }

  const matches = multiPlacement
    ? regionsMatchAvailabilityZonesByProvider(
        regions,
        methods.getValues(AVAILABILITY_ZONES_BY_PROVIDER) as AvailabilityZonesByProvider
      )
    : regionCodesMatchAvailabilityZones(regions, methods.getValues('availabilityZones'));

  if (!matches) {
    if (isExpert) {
      const expertPlacement = getExpertAvailabilityZonesOrEmpty(
        resilienceAndRegionsSettings,
        enablePrimaryRf9
      );
      const replicationFactor = expertPlacement.replicationFactor;
      methods.setValue(REPLICATION_FACTOR, replicationFactor);
      if (multiPlacement) {
        // One universe placement across all regions, then partition (same as Guided).
        methods.setValue('availabilityZones', {});
        methods.setValue(
          AVAILABILITY_ZONES_BY_PROVIDER,
          partitionAvailabilityZonesByProvider(
            expertPlacement.availabilityZones,
            regions,
            providers
          ) as any
        );
      } else {
        methods.setValue('availabilityZones', expertPlacement.availabilityZones);
        methods.setValue(AVAILABILITY_ZONES_BY_PROVIDER, undefined as any);
      }
      if (replicationFactor !== resilienceAndRegionsSettings.resilienceFactor) {
        saveResilienceAndRegionsSettings({
          ...resilienceAndRegionsSettings,
          [RESILIENCE_FACTOR]: replicationFactor
        });
      }
    } else {
      // Guided: assign once across the full region set, then partition for multi-provider.
      // Per-provider assign re-applied the full FT budget to each slice (extra AZs/nodes).
      const nextZones = assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);
      if (multiPlacement) {
        methods.setValue('availabilityZones', {});
        methods.setValue(
          AVAILABILITY_ZONES_BY_PROVIDER,
          partitionAvailabilityZonesByProvider(nextZones, regions, providers) as any
        );
      } else {
        methods.setValue('availabilityZones', nextZones);
        methods.setValue(AVAILABILITY_ZONES_BY_PROVIDER, undefined as any);
      }
    }
  } else if (isExpert) {
    const currentRf = methods.getValues(REPLICATION_FACTOR);
    if (currentRf === undefined || currentRf === null) {
      methods.setValue(REPLICATION_FACTOR, resilienceAndRegionsSettings.resilienceFactor ?? 1);
    }
  }
}

const getPreferredRemovalIndex = (zones: NodeAvailabilityProps['availabilityZones'][string]) => {
  if (!zones?.length) return -1;
  let removalIndex = -1;
  let highestPreferredRank = Number.NEGATIVE_INFINITY;
  zones.forEach((zone, index) => {
    const preferredRank =
      typeof zone.preffered === 'number' && zone.preffered > AZ_NOT_PREFERRED
        ? zone.preffered
        : Number.NEGATIVE_INFINITY;
    if (
      preferredRank > highestPreferredRank ||
      (preferredRank === highestPreferredRank && index > removalIndex)
    ) {
      highestPreferredRank = preferredRank;
      removalIndex = index;
    }
  });
  return removalIndex;
};

const normalizePreferredRanks = (zones: NodeAvailabilityProps['availabilityZones'][string]) => {
  const sortedByPreferred = zones
    .map((zone, index) => ({
      index,
      preferred:
        typeof zone.preffered === 'number' && zone.preffered > AZ_NOT_PREFERRED
          ? zone.preffered
          : AZ_NOT_PREFERRED
    }))
    .filter(({ preferred }) => preferred > AZ_NOT_PREFERRED)
    .sort((a, b) => a.preferred - b.preferred || a.index - b.index);
  sortedByPreferred.forEach(({ index }, rank) => {
    zones[index] = { ...zones[index], preffered: rank + AZ_PREFFERED_HIGHEST_RANK };
  });
};

export type UseNodesAvailabilityStepResult = {
  methods: UseFormReturn<NodeAvailabilityProps>;
  regions: Region[];
  icon: ReturnType<typeof useGetMapIcons>;
  showErrorsAfterSubmit: boolean;
  lesserNodesTransValues: {
    faultToleranceNeeded: number;
    required_zones: number;
    max_az: number;
    nodeCount: number;
    availability_zone: number;
    selected_regions: number;
    rf: number;
    az_name?: string;
    requested_count?: number;
    free_count?: number;
  };
  errors: UseFormReturn<NodeAvailabilityProps>['formState']['errors'];
  t: TFunction;
  resilienceAndRegionsSettings?: ResilienceAndRegionsProps;
  inferredResilience: ReturnType<typeof inferResilience>;
  effectiveReplicationFactor: number;
  /** Present when multi-provider Nodes UI groups cards by provider. */
  providers?: ReturnType<typeof getProviderConfigurations>;
};

export type UseNodesAvailabilityStepOptions = {
  /** Add-geo-partition wizard: re-sync AZ layout when Regions step resilience changes (context keeps stale nodes). */
  isGeoPartition?: boolean;
};

export function useNodesAvailabilityStep(
  forwardedRef: ForwardedRef<StepsRef>,
  options?: UseNodesAvailabilityStepOptions
): UseNodesAvailabilityStepResult {
  const isGeoPartition = options?.isGeoPartition ?? false;
  const [
    {
      generalSettings,
      resilienceAndRegionsSettings,
      nodesAvailabilitySettings,
      placementsByProvider,
      nodesByProvider
    },
    {
      moveToPreviousPage,
      moveToNextPage,
      saveNodesAvailabilitySettings,
      saveNodesByProvider,
      saveResilienceAndRegionsSettings
    }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const providers = useMemo(
    () => getProviderConfigurations(generalSettings),
    [generalSettings?.providerConfigurations]
  );
  const multiPlacement =
    ENABLE_MULTI_PROVIDER_CREATE && isMultiProvider(generalSettings);

  const resilienceForPlacement = useMemo(
    () =>
      resilienceAndRegionsSettings
        ? withEffectiveRegions(resilienceAndRegionsSettings, placementsByProvider)
        : undefined,
    [resilienceAndRegionsSettings, placementsByProvider]
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability'
  });
  const { enablePrimaryRf9 } = useRuntimeConfigValues();

  const placementApplyOptions = useMemo(
    () => ({ multiPlacement, providers, enablePrimaryRf9 }),
    [multiPlacement, providers, enablePrimaryRf9]
  );

  const providerUuid = getPrimaryProvider(generalSettings)?.uuid;
  const isOnPrem =
    generalSettings?.cloud === CloudType.onprem ||
    getPrimaryProvider(generalSettings)?.code === CloudType.onprem;

  const {
    data: onPremProviderNodes,
    isLoading: isOnPremNodesLoading,
    isFetched: isOnPremNodesFetched
  } = useQuery(
    [NodeAgentQueryKey.fetchOnPremProviderNodeList, providerUuid],
    () => NodeAgentAPI.fetchOnPremProviderNodeList(providerUuid!),
    { enabled: isOnPrem && !!providerUuid }
  );

  const freeNodesByAzUuid = useMemo(
    () => computeFreeNodesByAzUuid(onPremProviderNodes ?? []),
    [onPremProviderNodes]
  );

  const isOnPremNodesLoaded = isOnPrem ? isOnPremNodesFetched && !isOnPremNodesLoading : true;

  const onPremContext = useMemo(
    () => ({
      isOnPrem,
      freeNodesByAzUuid,
      isOnPremNodesLoaded
    }),
    [isOnPrem, freeNodesByAzUuid, isOnPremNodesLoaded]
  );

  const regions = resilienceForPlacement?.regions ?? [];
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  const resolver = useMemo(
    () => yupResolver(NodesAvailabilitySchema(resilienceForPlacement, onPremContext)),
    [resilienceForPlacement, onPremContext]
  );

  const formDefaultValues: NodeAvailabilityProps = {
    ...(nodesAvailabilitySettings ?? {
      availabilityZones: {},
      useDedicatedNodes: false
    }),
    ...(multiPlacement
      ? {
          availabilityZones: {},
          [AVAILABILITY_ZONES_BY_PROVIDER]: availabilityZonesByProviderFromNodes(
            nodesByProvider,
            providers
          )
        }
      : { [AVAILABILITY_ZONES_BY_PROVIDER]: undefined })
  };

  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: formDefaultValues,
    resolver
  });

  const persistNodes = useCallback(
    (data: NodeAvailabilityProps) => {
      if (multiPlacement) {
        const byProvider = (data[AVAILABILITY_ZONES_BY_PROVIDER] ??
          {}) as AvailabilityZonesByProvider;
        saveNodesByProvider?.(buildNodesByProvider(byProvider, providers));
        saveNodesAvailabilitySettings(singularNodesFromMultiForm(data));
        return;
      }
      saveNodesByProvider?.({});
      const { [AVAILABILITY_ZONES_BY_PROVIDER]: _drop, ...rest } = data as NodeAvailabilityProps & {
        [AVAILABILITY_ZONES_BY_PROVIDER]?: AvailabilityZonesByProvider;
      };
      saveNodesAvailabilitySettings(rest);
    },
    [multiPlacement, providers, saveNodesByProvider, saveNodesAvailabilitySettings]
  );

  usePersistStepFormValues(methods.watch, methods.getValues, persistNodes);
  const { trigger } = methods;
  const availabilityZones = methods.watch('availabilityZones');
  const availabilityZonesByProvider = methods.watch(
    AVAILABILITY_ZONES_BY_PROVIDER
  ) as AvailabilityZonesByProvider | undefined;
  const effectiveAvailabilityZones = multiPlacement
    ? flattenAvailabilityZonesByProvider(availabilityZonesByProvider)
    : availabilityZones;
  const watchedReplicationFactor = methods.watch(REPLICATION_FACTOR);
  const isExpertMode =
    resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.EXPERT_MODE;
  const effectiveRf = isExpertMode
    ? watchedReplicationFactor ?? resilienceAndRegionsSettings?.resilienceFactor ?? 1
    : resilienceAndRegionsSettings?.resilienceFactor ?? 1;
  const faultToleranceNeeded = isExpertMode ? effectiveRf : getFaultToleranceNeeded(effectiveRf);
  const totalAzCount = multiPlacement
    ? countZonesInByProvider(availabilityZonesByProvider)
    : values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0);
  const onPremNodeViolation =
    isOnPrem && isOnPremNodesLoaded
      ? findFirstOnPremNodeViolation(effectiveAvailabilityZones ?? {}, freeNodesByAzUuid)
      : null;
  const lesserNodesTransValues = {
    faultToleranceNeeded,
    required_zones: faultToleranceNeeded,
    max_az: faultToleranceNeeded - 1,
    nodeCount: totalAzCount,
    availability_zone: totalAzCount,
    selected_regions: regions.length,
    rf: effectiveRf,
    az_name: onPremNodeViolation?.az_name,
    requested_count: onPremNodeViolation?.requested_count,
    free_count: onPremNodeViolation?.free_count
  };
  // Depend on totalAzCount (primitive): nested setValue (RegionCard add/remove AZ) keeps the same
  // availabilityZones object reference, so listing only that object made useMemo skip updates.
  // availabilityZones is still read inside from the latest render when totalAzCount changes.
  const inferredResilience = useMemo(() => {
    if (!resilienceForPlacement) {
      return null;
    }
    return inferResilience(resilienceForPlacement, {
      availabilityZones: effectiveAvailabilityZones ?? {},
      replicationFactor: effectiveRf,
      useDedicatedNodes: false
    });
  }, [resilienceForPlacement, totalAzCount, effectiveRf, effectiveAvailabilityZones]);

  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);
  const previousReplicationFactorRef = useRef<number | undefined>(undefined);
  const previousRfForErrorResetRef = useRef<number | undefined>(undefined);
  const { errors, isSubmitted } = methods.formState;

  const hasErrors = errors && Object.keys(errors).length > 0;
  useEffect(() => {
    if (isSubmitted && !hasErrors) {
      setShowErrorsAfterSubmit(false);
    }
  }, [isSubmitted, hasErrors]);

  useEffect(() => {
    if (showErrorsAfterSubmit) trigger();
  }, [
    JSON.stringify(effectiveAvailabilityZones),
    JSON.stringify(availabilityZonesByProvider),
    showErrorsAfterSubmit,
    trigger,
    watchedReplicationFactor,
    isOnPremNodesLoaded,
    freeNodesByAzUuid
  ]);

  useEffect(() => {
    if (!resilienceAndRegionsSettings) return;
    if (resilienceAndRegionsSettings.resilienceFormMode !== ResilienceFormMode.EXPERT_MODE) {
      return;
    }
    if (watchedReplicationFactor === undefined || watchedReplicationFactor === null) {
      return;
    }
    if (watchedReplicationFactor === resilienceAndRegionsSettings.resilienceFactor) {
      return;
    }
    if (typeof saveResilienceAndRegionsSettings !== 'function') {
      return;
    }
    saveResilienceAndRegionsSettings({
      ...resilienceAndRegionsSettings,
      [RESILIENCE_FACTOR]: watchedReplicationFactor
    });
  }, [
    watchedReplicationFactor,
    resilienceAndRegionsSettings,
    saveResilienceAndRegionsSettings
  ]);

  useEffect(() => {
    if (!isExpertMode || watchedReplicationFactor === undefined || watchedReplicationFactor === null) {
      previousRfForErrorResetRef.current = watchedReplicationFactor ?? undefined;
      return;
    }

    const previousRf = previousRfForErrorResetRef.current;
    previousRfForErrorResetRef.current = watchedReplicationFactor;

    if (previousRf === undefined || previousRf === watchedReplicationFactor) {
      return;
    }

    methods.reset(methods.getValues(), {
      keepValues: true,
      keepDefaultValues: true,
      keepDirty: true,
      keepTouched: true,
      keepSubmitCount: false,
      keepIsSubmitted: false
    });
    setShowErrorsAfterSubmit(false);
  }, [isExpertMode, watchedReplicationFactor, methods]);

  useEffect(() => {
    if (!isExpertMode || watchedReplicationFactor === undefined || watchedReplicationFactor === null) {
      previousReplicationFactorRef.current = watchedReplicationFactor ?? undefined;
      return;
    }

    const previousReplicationFactor = previousReplicationFactorRef.current;
    previousReplicationFactorRef.current = watchedReplicationFactor;

    if (
      previousReplicationFactor === undefined ||
      watchedReplicationFactor >= previousReplicationFactor ||
      !(multiPlacement ? availabilityZonesByProvider : availabilityZones)
    ) {
      return;
    }

    const zonesForTrim = multiPlacement
      ? flattenAvailabilityZonesByProvider(availabilityZonesByProvider)
      : availabilityZones;

    const currentAzCount = values(zonesForTrim).reduce((count, zones) => count + zones.length, 0);
    const currentNodeCount = getNodeCount(zonesForTrim);
    if (
      currentAzCount <= watchedReplicationFactor &&
      currentNodeCount <= watchedReplicationFactor
    ) {
      return;
    }

    const trimmedAvailabilityZones = Object.entries(zonesForTrim).reduce<
      NodeAvailabilityProps['availabilityZones']
    >((acc, [regionCode, zones]) => {
      acc[regionCode] = zones.map((z) => ({ ...z }));
      return acc;
    }, {});

    if (currentAzCount > watchedReplicationFactor) {
      let overflow = currentAzCount - watchedReplicationFactor;

      // Greedy: each removal takes one AZ row from a region with the most AZs among
      // those that still have >1 AZ. Only when every non-empty region has exactly 1 AZ
      // do we remove from single-AZ regions (unavoidable if overflow remains).
      while (overflow > 0) {
        const codes = Object.keys(trimmedAvailabilityZones);
        const nonEmpty = codes.filter(
          (c) => (trimmedAvailabilityZones[c]?.length ?? 0) > 0
        );
        if (nonEmpty.length === 0) break;

        const multiAz = nonEmpty.filter(
          (c) => (trimmedAvailabilityZones[c]?.length ?? 0) > 1
        );
        const pool = multiAz.length > 0 ? multiAz : nonEmpty;

        let bestCode = pool[0];
        let bestLen = trimmedAvailabilityZones[bestCode]?.length ?? 0;
        for (let i = 1; i < pool.length; i += 1) {
          const c = pool[i];
          const len = trimmedAvailabilityZones[c]?.length ?? 0;
          if (len > bestLen || (len === bestLen && c > bestCode)) {
            bestCode = c;
            bestLen = len;
          }
        }

        const regionZones = trimmedAvailabilityZones[bestCode];
        const removalIndex = getPreferredRemovalIndex(regionZones);
        if (removalIndex < 0) break;
        regionZones.splice(removalIndex, 1);
        overflow -= 1;
      }

      Object.keys(trimmedAvailabilityZones).forEach((regionCode) => {
        normalizePreferredRanks(trimmedAvailabilityZones[regionCode]);
      });
    }

    reduceExpertNodeCountsToAtMostRf(trimmedAvailabilityZones, watchedReplicationFactor);

    if (multiPlacement) {
      methods.setValue(
        AVAILABILITY_ZONES_BY_PROVIDER,
        unflattenAvailabilityZonesByProvider(trimmedAvailabilityZones, providers) as any,
        { shouldValidate: true, shouldDirty: true }
      );
      methods.setValue('availabilityZones', {}, { shouldValidate: false });
    } else {
      methods.setValue('availabilityZones', trimmedAvailabilityZones, {
        shouldValidate: true,
        shouldDirty: true
      });
    }
  }, [
    isExpertMode,
    watchedReplicationFactor,
    availabilityZones,
    availabilityZonesByProvider,
    multiPlacement,
    providers,
    methods
  ]);

  useMount(() => {
    if (!resilienceForPlacement) return;
    applyNodesStepPlacementFromResilience(
      methods,
      resilienceForPlacement,
      saveResilienceAndRegionsSettings,
      placementApplyOptions
    );
  });

  const guidedPlacementSyncSignature = useMemo(() => {
    if (!isGeoPartition) {
      return null;
    }
    const r = resilienceForPlacement;
    if (!r || r.resilienceFormMode !== ResilienceFormMode.GUIDED) {
      return null;
    }
    return getGuidedPlacementSyncSignature(r);
  }, [isGeoPartition, resilienceForPlacement]);

  const lastGuidedPlacementSignatureRef = useRef<string | null>(null);

  // Geo-partition wizard only: Regions step drives layout. Stale nodesAndAvailability from context
  // (or prior visit) left availabilityZones non-empty, so applyNodesStepPlacementFromResilience
  // skipped updating. Only resync when zones are empty or region selection changed.
  useEffect(() => {
    if (guidedPlacementSyncSignature === null) {
      lastGuidedPlacementSignatureRef.current = null;
      return;
    }
    if (lastGuidedPlacementSignatureRef.current === guidedPlacementSyncSignature) {
      return;
    }
    lastGuidedPlacementSignatureRef.current = guidedPlacementSyncSignature;

    const selectedRegions = resilienceForPlacement?.regions ?? [];
    const matches = multiPlacement
      ? regionsMatchAvailabilityZonesByProvider(
          selectedRegions,
          methods.getValues(AVAILABILITY_ZONES_BY_PROVIDER) as AvailabilityZonesByProvider
        )
      : regionCodesMatchAvailabilityZones(
          selectedRegions,
          methods.getValues('availabilityZones')
        );
    if (matches) {
      return;
    }

    methods.setValue('availabilityZones', {});
    methods.setValue(AVAILABILITY_ZONES_BY_PROVIDER, undefined as any);
    applyNodesStepPlacementFromResilience(
      methods,
      resilienceForPlacement!,
      saveResilienceAndRegionsSettings,
      placementApplyOptions
    );
  }, [
    guidedPlacementSyncSignature,
    methods,
    resilienceForPlacement,
    saveResilienceAndRegionsSettings,
    multiPlacement,
    placementApplyOptions
  ]);

  // Re-sync when resilience/region context changes — not on every availabilityZones edit (that
  // caused setValue ↔ watch feedback loops in expert mode).
  // Covers expert mode and guided (non-geo) create-universe. Guided geo-partition is handled by
  // the dedicated guidedPlacementSyncSignature effect above.
  useEffect(() => {
    if (!resilienceForPlacement) return;
    const formMode = resilienceForPlacement.resilienceFormMode;
    const isExpert = formMode === ResilienceFormMode.EXPERT_MODE;
    const isGuidedNonGeo = formMode === ResilienceFormMode.GUIDED && !isGeoPartition;
    if (!isExpert && !isGuidedNonGeo) {
      return;
    }
    const regionList = resilienceForPlacement.regions ?? [];
    const matches = multiPlacement
      ? regionsMatchAvailabilityZonesByProvider(
          regionList,
          methods.getValues(AVAILABILITY_ZONES_BY_PROVIDER) as AvailabilityZonesByProvider
        )
      : regionCodesMatchAvailabilityZones(regionList, methods.getValues('availabilityZones'));
    if (matches) {
      return;
    }
    methods.setValue('availabilityZones', {});
    methods.setValue(AVAILABILITY_ZONES_BY_PROVIDER, undefined as any);
    applyNodesStepPlacementFromResilience(
      methods,
      resilienceForPlacement,
      saveResilienceAndRegionsSettings,
      placementApplyOptions
    );
  }, [
    resilienceForPlacement,
    methods,
    saveResilienceAndRegionsSettings,
    isGeoPartition,
    multiPlacement,
    placementApplyOptions
  ]);

  useImperativeHandle(
    forwardedRef,
    () => ({
      onNext: (): Promise<boolean> => {
        setShowErrorsAfterSubmit(true);
        return new Promise<boolean>((resolve) => {
          void methods
            .handleSubmit(
              () => {
                moveToNextPage();
                resolve(true);
              },
              () => resolve(false)
            )()
            .catch(() => resolve(false));
        });
      },
      onPrev: () => {
        moveToPreviousPage();
      },
      setValue: methods.setValue as (name: string, value: unknown) => void
    }),
    [methods, moveToNextPage, moveToPreviousPage]
  );

  return {
    methods,
    regions,
    icon,
    showErrorsAfterSubmit,
    lesserNodesTransValues,
    errors,
    t,
    resilienceAndRegionsSettings: resilienceForPlacement ?? resilienceAndRegionsSettings,
    inferredResilience,
    effectiveReplicationFactor: effectiveRf,
    providers: multiPlacement ? providers : undefined
  };
}
