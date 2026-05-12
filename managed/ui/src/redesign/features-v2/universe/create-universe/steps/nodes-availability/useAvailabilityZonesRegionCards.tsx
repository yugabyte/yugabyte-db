import { useMemo } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { RegionCard } from './RegionCard';
import { NodeAvailabilityProps } from './dtos';
import { ResilienceAndRegionsProps, ResilienceFormMode, FaultToleranceType } from '../resilence-regions/dtos';
import { canSelectMultipleRegions, getFaultToleranceNeeded } from '../../CreateUniverseUtils';
import { REPLICATION_FACTOR } from '../../fields/FieldNames';

type Args = {
  mode: ResilienceFormMode;
  showErrorsAfterSubmit: boolean;
  resilienceAndRegionsSettings?: ResilienceAndRegionsProps;
};

export function useAvailabilityZonesRegionCards({
  mode,
  showErrorsAfterSubmit,
  resilienceAndRegionsSettings
}: Args) {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });
  const { watch } = useFormContext<NodeAvailabilityProps>();

  const availabilityZones = watch('availabilityZones');
  const watchedReplicationFactor = watch(REPLICATION_FACTOR);
  const azCount = Object.keys(availabilityZones ?? {}).reduce(
    (acc, regionCode) => acc + (availabilityZones?.[regionCode]?.length ?? 0),
    0
  );
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

  const regionCards = useMemo(
    () => {
      const regionCodes = Object.keys(availabilityZones ?? {});
      const cards = regionCodes.map((regionCode, index) => {
        const region = resilienceAndRegionsSettings?.regions.find((r) => r.code === regionCode);
        if (!region) return null;

        const regionAzCount = availabilityZones?.[regionCode]?.length ?? 0;
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
            ? effectiveReplicationFactor >= 7
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

        return (
          <RegionCard
            key={region.uuid}
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
          />
        );
      });
      return cards;
    },
    [
      availabilityZones,
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
      t
    ]
  );

  return { azCount, faultToleranceNeeded, regionCards };
}

