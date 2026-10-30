import { useMemo } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { RegionCard } from './RegionCard';
import { NodeAvailabilityProps } from './dtos';
import { ResilienceAndRegionsProps, ResilienceFormMode, FaultToleranceType } from '../resilence-regions/dtos';
import {
  canSelectMultipleRegions,
  getFaultToleranceNeeded,
  maxExpertRfOption
} from '../../CreateUniverseUtils';
import { AVAILABILITY_ZONES_BY_PROVIDER, REPLICATION_FACTOR } from '../../fields/FieldNames';
import { ProviderType } from '../general-settings/dtos';
import {
  countZonesInByProvider,
  type AvailabilityZonesByProvider
} from '../../helpers/nodesByProvider';
import { useRuntimeConfigValues } from '../../helpers/utils';

type Args = {
  mode: ResilienceFormMode;
  showErrorsAfterSubmit: boolean;
  resilienceAndRegionsSettings?: ResilienceAndRegionsProps;
  baselineRegionCodes?: string[];
  baselineZoneUuidsByRegion?: Record<string, string[]>;
  /** When set, cards are grouped by provider and read availabilityZonesByProvider. */
  providers?: ProviderType[];
};

export function useAvailabilityZonesRegionCards({
  mode,
  showErrorsAfterSubmit,
  resilienceAndRegionsSettings,
  baselineRegionCodes,
  baselineZoneUuidsByRegion,
  providers
}: Args) {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });
  const { watch } = useFormContext<NodeAvailabilityProps>();
  const { enablePrimaryRf9 } = useRuntimeConfigValues();

  const multiPlacement = (providers?.length ?? 0) >= 2;
  const availabilityZones = watch('availabilityZones');
  const availabilityZonesByProvider = watch(
    AVAILABILITY_ZONES_BY_PROVIDER
  ) as AvailabilityZonesByProvider | undefined;

  const azCount = multiPlacement
    ? countZonesInByProvider(availabilityZonesByProvider)
    : Object.keys(availabilityZones ?? {}).reduce(
        (acc, regionCode) => acc + (availabilityZones?.[regionCode]?.length ?? 0),
        0
      );
  const watchedReplicationFactor = watch(REPLICATION_FACTOR);
  const resilienceFactor = resilienceAndRegionsSettings?.resilienceFactor ?? 1;
  const effectiveReplicationFactor =
    mode === ResilienceFormMode.EXPERT_MODE
      ? watchedReplicationFactor ?? resilienceFactor
      : resilienceFactor;
  const faultToleranceNeeded = getFaultToleranceNeeded(effectiveReplicationFactor);
  const ft = resilienceAndRegionsSettings?.faultToleranceType;

  const showAddAzButton =
    mode === ResilienceFormMode.EXPERT_MODE
      ? true
      : canSelectMultipleRegions(resilienceAndRegionsSettings?.resilienceType) &&
        ft === FaultToleranceType.AZ_LEVEL;

  const buildCard = (
    regionCode: string,
    index: number,
    providerUuid?: string,
    providerCode?: string
  ) => {
    const region = resilienceAndRegionsSettings?.regions.find(
      (r) =>
        r.code === regionCode &&
        (providerUuid == null || r.providerUuid === providerUuid || !r.providerUuid)
    );
    if (!region) return null;

    const zonesForRegion = providerUuid
      ? availabilityZonesByProvider?.[providerUuid]?.[regionCode]
      : availabilityZones?.[regionCode];
    const regionAzCount = zonesForRegion?.length ?? 0;
    const isRegionAzLimitReached = regionAzCount >= region.zones.length;
    const maxTotalAzCount =
      mode === ResilienceFormMode.EXPERT_MODE
        ? effectiveReplicationFactor
        : faultToleranceNeeded;
    const isGuidedCappedMode =
      mode === ResilienceFormMode.GUIDED && ft === FaultToleranceType.AZ_LEVEL;
    const isTotalAzLimitReached =
      mode === ResilienceFormMode.EXPERT_MODE
        ? azCount >= maxTotalAzCount
        : isGuidedCappedMode && azCount >= maxTotalAzCount;
    const isAddAzDisabled = isRegionAzLimitReached || isTotalAzLimitReached;
    const isAddAzDisabledByAzLevelCap =
      isTotalAzLimitReached &&
      (mode === ResilienceFormMode.EXPERT_MODE || ft !== FaultToleranceType.NODE_LEVEL);
    const addAzTooltipKey = isAddAzDisabledByAzLevelCap
      ? mode === ResilienceFormMode.EXPERT_MODE
        ? effectiveReplicationFactor >=
          maxExpertRfOption(enablePrimaryRf9, effectiveReplicationFactor)
          ? 'tooltips.addAvailabilityZoneDisabledExpertMaxRf'
          : 'tooltips.addAvailabilityZoneDisabledExpert'
        : 'tooltips.addAvailabilityZoneDisabled'
      : undefined;
    const addAzTooltipValues = isAddAzDisabledByAzLevelCap
      ? mode === ResilienceFormMode.EXPERT_MODE
        ? { rf: effectiveReplicationFactor }
        : { outage_count: effectiveReplicationFactor, az_count: maxTotalAzCount }
      : undefined;
    const addAzTooltip = isAddAzDisabled
      ? isTotalAzLimitReached
        ? ''
        : t('tooltips.regionNoMoreAz', { count: region.zones.length })
      : '';
    const isNewRegion =
      Array.isArray(baselineRegionCodes) && !baselineRegionCodes.includes(region.code);
    const baselineZoneUuids =
      baselineZoneUuidsByRegion !== undefined
        ? baselineZoneUuidsByRegion[region.code] ?? []
        : undefined;

    return (
      <RegionCard
        key={providerUuid ? `${providerUuid}:${region.uuid}` : region.uuid}
        region={region}
        index={index}
        mode={mode}
        showErrorsAfterSubmit={showErrorsAfterSubmit}
        showAddAzButton={showAddAzButton}
        isAddAzDisabled={isAddAzDisabled}
        isAddAzDisabledByAzLevelCap={isAddAzDisabledByAzLevelCap}
        addAzTooltip={addAzTooltip}
        addAzTooltipKey={addAzTooltipKey}
        addAzTooltipValues={addAzTooltipValues}
        isNewRegion={isNewRegion}
        baselineZoneUuids={baselineZoneUuids}
        providerUuid={providerUuid}
        providerCode={providerCode}
      />
    );
  };

  const regionCards = useMemo(() => {
    if (multiPlacement && providers) {
      // Figma Multi-Provider Nodes: logo on each card, no provider section headings;
      // "Region N" is numbered per provider.
      return providers.flatMap((provider) => {
        const regionCodes = Object.keys(availabilityZonesByProvider?.[provider.uuid] ?? {});
        return regionCodes
          .map((regionCode, index) =>
            buildCard(regionCode, index, provider.uuid, provider.code)
          )
          .filter(Boolean);
      });
    }

    const regionCodes = Object.keys(availabilityZones ?? {});
    return regionCodes.map((regionCode, index) => buildCard(regionCode, index));
  }, [
    multiPlacement,
    providers,
    availabilityZones,
    availabilityZonesByProvider,
    resilienceAndRegionsSettings,
    ft,
    faultToleranceNeeded,
    azCount,
    mode,
    resilienceFactor,
    effectiveReplicationFactor,
    watchedReplicationFactor,
    showErrorsAfterSubmit,
    showAddAzButton,
    baselineRegionCodes,
    baselineZoneUuidsByRegion,
    enablePrimaryRf9,
    t
  ]);

  return { azCount, faultToleranceNeeded, regionCards };
}
