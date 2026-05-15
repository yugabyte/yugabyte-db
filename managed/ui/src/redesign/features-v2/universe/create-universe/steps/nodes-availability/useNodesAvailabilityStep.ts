import {
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
import { MarkerType, useGetMapIcons } from '@yugabyte-ui-library/core';
import {
  assignRegionsAZNodeByReplicationFactor,
  getExpertNodesStepDefaultPlacement,
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
import { NodeAvailabilityProps } from './dtos';
import { Region } from '../../../../../helpers/dtos';
import { ResilienceFormMode, type ResilienceAndRegionsProps } from '../resilence-regions/dtos';
import { REPLICATION_FACTOR, RESILIENCE_FACTOR } from '../../fields/FieldNames';

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

/** Initializes or refreshes placement from resilience settings when zones are empty (or cleared). */
export function applyNodesStepPlacementFromResilience(
  methods: UseFormReturn<NodeAvailabilityProps>,
  resilienceAndRegionsSettings: ResilienceAndRegionsProps,
  saveResilienceAndRegionsSettings: (data: ResilienceAndRegionsProps) => void
): void {
  const isExpert =
    resilienceAndRegionsSettings.resilienceFormMode === ResilienceFormMode.EXPERT_MODE;

  if (resilienceAndRegionsSettings.resilienceFormMode === ResilienceFormMode.GUIDED) {
    methods.setValue(
      REPLICATION_FACTOR,
      getGuidedNodesStepReplicationFactor(
        resilienceAndRegionsSettings.faultToleranceType,
        resilienceAndRegionsSettings.resilienceFactor
      )
    );
  }

  const zonesSnapshot = methods.getValues('availabilityZones');
  if (isEmpty(zonesSnapshot)) {
    const expertPlacement = getExpertNodesStepDefaultPlacement(resilienceAndRegionsSettings);
    if (expertPlacement) {
      methods.setValue(REPLICATION_FACTOR, expertPlacement.replicationFactor);
      methods.setValue('availabilityZones', expertPlacement.availabilityZones);
      if (
        expertPlacement.replicationFactor !== resilienceAndRegionsSettings.resilienceFactor
      ) {
        saveResilienceAndRegionsSettings({
          ...resilienceAndRegionsSettings,
          [RESILIENCE_FACTOR]: expertPlacement.replicationFactor
        });
      }
    } else {
      if (isExpert) {
        const currentRf = methods.getValues(REPLICATION_FACTOR);
        if (currentRf === undefined || currentRf === null) {
          methods.setValue(
            REPLICATION_FACTOR,
            resilienceAndRegionsSettings.resilienceFactor ?? 1
          );
        }
      }
      const nextZones = assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);
      methods.setValue('availabilityZones', nextZones);
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
    const preferredRank = typeof zone.preffered === 'number' ? zone.preffered : -1;
    if (preferredRank > highestPreferredRank || (preferredRank === highestPreferredRank && index > removalIndex)) {
      highestPreferredRank = preferredRank;
      removalIndex = index;
    }
  });
  return removalIndex;
};

const normalizePreferredRanks = (zones: NodeAvailabilityProps['availabilityZones'][string]) => {
  const sortedByPreferred = zones
    .map((zone, index) => ({ index, preferred: typeof zone.preffered === 'number' ? zone.preffered : index }))
    .sort((a, b) => a.preferred - b.preferred || a.index - b.index);
  sortedByPreferred.forEach(({ index }, rank) => {
    zones[index] = { ...zones[index], preffered: rank };
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
  };
  errors: UseFormReturn<NodeAvailabilityProps>['formState']['errors'];
  t: TFunction;
  resilienceAndRegionsSettings?: ResilienceAndRegionsProps;
  inferredResilience: ReturnType<typeof inferResilience>;
  effectiveReplicationFactor: number;
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
    { resilienceAndRegionsSettings, nodesAvailabilitySettings },
    {
      moveToPreviousPage,
      moveToNextPage,
      saveNodesAvailabilitySettings,
      saveResilienceAndRegionsSettings
    }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;
  
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability'
  });

  const regions = resilienceAndRegionsSettings?.regions ?? [];
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  const resolver = useMemo(
    () => yupResolver(NodesAvailabilitySchema(resilienceAndRegionsSettings)),
    [resilienceAndRegionsSettings]
  );

  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: nodesAvailabilitySettings,
    resolver
  });
  const { trigger } = methods;
  const availabilityZones = methods.watch('availabilityZones');
  const watchedReplicationFactor = methods.watch(REPLICATION_FACTOR);
  const isExpertMode =
    resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.EXPERT_MODE;
  const effectiveRf = isExpertMode
    ? watchedReplicationFactor ?? resilienceAndRegionsSettings?.resilienceFactor ?? 1
    : resilienceAndRegionsSettings?.resilienceFactor ?? 1;
  const faultToleranceNeeded = isExpertMode ? effectiveRf : getFaultToleranceNeeded(effectiveRf);
  const totalAzCount = values(availabilityZones).reduce(
    (acc, zones) => acc + zones.length,
    0
  );
  const lesserNodesTransValues = {
    faultToleranceNeeded,
    required_zones: faultToleranceNeeded,
    max_az: faultToleranceNeeded - 1,
    nodeCount: totalAzCount,
    availability_zone: totalAzCount,
    selected_regions: regions.length,
    rf: effectiveRf
  };
  // Depend on totalAzCount (primitive): nested setValue (RegionCard add/remove AZ) keeps the same
  // availabilityZones object reference, so listing only that object made useMemo skip updates.
  // availabilityZones is still read inside from the latest render when totalAzCount changes.
  const inferredResilience = useMemo(() => {
    if (!resilienceAndRegionsSettings) {
      return null;
    }
    return inferResilience(resilienceAndRegionsSettings, {
      availabilityZones: availabilityZones ?? {},
      replicationFactor: effectiveRf,
      useDedicatedNodes: false
    });
  }, [resilienceAndRegionsSettings, totalAzCount, effectiveRf]);

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
  }, [JSON.stringify(availabilityZones), showErrorsAfterSubmit, trigger, watchedReplicationFactor]);

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
      !availabilityZones
    ) {
      return;
    }

    const currentAzCount = values(availabilityZones).reduce((count, zones) => count + zones.length, 0);
    const currentNodeCount = getNodeCount(availabilityZones);
    if (
      currentAzCount <= watchedReplicationFactor &&
      currentNodeCount <= watchedReplicationFactor
    ) {
      return;
    }

    const trimmedAvailabilityZones = Object.entries(availabilityZones).reduce<
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

    methods.setValue('availabilityZones', trimmedAvailabilityZones, {
      shouldValidate: true,
      shouldDirty: true
    });
  }, [isExpertMode, watchedReplicationFactor, availabilityZones, methods]);

  useMount(() => {
    if (!resilienceAndRegionsSettings) return;
    applyNodesStepPlacementFromResilience(
      methods,
      resilienceAndRegionsSettings,
      saveResilienceAndRegionsSettings
    );
  });

  const guidedPlacementSyncSignature = useMemo(() => {
    if (!isGeoPartition) {
      return null;
    }
    const r = resilienceAndRegionsSettings;
    if (!r || r.resilienceFormMode !== ResilienceFormMode.GUIDED) {
      return null;
    }
    return getGuidedPlacementSyncSignature(r);
  }, [isGeoPartition, resilienceAndRegionsSettings]);

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

    const currentAvailabilityZones = methods.getValues('availabilityZones');
    const hasExistingZones = !isEmpty(currentAvailabilityZones);
    const selectedRegions = resilienceAndRegionsSettings?.regions ?? [];
    const selectedRegionsMatchAvailabilityZones = regionCodesMatchAvailabilityZones(
      selectedRegions,
      currentAvailabilityZones
    );
    if (hasExistingZones && selectedRegionsMatchAvailabilityZones) {
      return;
    }

    methods.setValue('availabilityZones', {});
    applyNodesStepPlacementFromResilience(
      methods,
      resilienceAndRegionsSettings!,
      saveResilienceAndRegionsSettings
    );
  }, [
    guidedPlacementSyncSignature,
    methods,
    resilienceAndRegionsSettings,
    saveResilienceAndRegionsSettings
  ]);

  // Re-sync when resilience/region context changes — not on every availabilityZones edit (that
  // caused setValue ↔ watch feedback loops in expert mode).
  useEffect(() => {
    if (!resilienceAndRegionsSettings) return;
    if (resilienceAndRegionsSettings.resilienceFormMode !== ResilienceFormMode.EXPERT_MODE) {
      return;
    }
    const regionList = resilienceAndRegionsSettings.regions ?? [];
    if (
      regionCodesMatchAvailabilityZones(regionList, methods.getValues('availabilityZones'))
    ) {
      return;
    }
    methods.setValue('availabilityZones', {});
    applyNodesStepPlacementFromResilience(
      methods,
      resilienceAndRegionsSettings,
      saveResilienceAndRegionsSettings
    );
  }, [resilienceAndRegionsSettings, methods, saveResilienceAndRegionsSettings]);

  useImperativeHandle(
    forwardedRef,
    () => ({
      onNext: (): Promise<boolean> => {
        setShowErrorsAfterSubmit(true);
        return new Promise<boolean>((resolve) => {
          void methods
            .handleSubmit(
              (data) => {
                saveNodesAvailabilitySettings(data);
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
    [methods, saveNodesAvailabilitySettings, moveToNextPage, moveToPreviousPage]
  );

  return {
    methods,
    regions,
    icon,
    showErrorsAfterSubmit,
    lesserNodesTransValues,
    errors,
    t,
    resilienceAndRegionsSettings,
    inferredResilience,
    effectiveReplicationFactor: effectiveRf
  };
}
