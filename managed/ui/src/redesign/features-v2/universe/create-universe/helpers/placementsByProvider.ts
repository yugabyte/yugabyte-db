import { Region } from '../../../../features/universe/universe-form/utils/dto';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../steps/resilence-regions/dtos';
import { ProviderType, GeneralSettingsProps } from '../steps/general-settings/dtos';
import {
  FAULT_TOLERANCE_TYPE,
  NODE_COUNT,
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../fields/FieldNames';
import { ENABLE_MULTI_PROVIDER_CREATE, isMultiProvider } from './providerConfigurations';

/** Form/context map: provider uuid → selected regions (stamped with providerUuid). */
export type RegionsByProvider = Record<string, Region[]>;

const emptyPlacementSlice = (): ResilienceAndRegionsProps => ({
  [RESILIENCE_TYPE]: ResilienceType.REGULAR,
  [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
  [REGIONS_FIELD]: [],
  [RESILIENCE_FACTOR]: 1,
  [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
  [NODE_COUNT]: 1
});

/**
 * Multi-provider placement UI gate (regionsByProvider / placementsByProvider).
 * Guided: PLAT-22500. Expert: PLAT-22501.
 */
export function isMultiProviderPlacement(
  generalSettings?: Pick<GeneralSettingsProps, 'providerConfigurations'> | null,
  formMode?: ResilienceFormMode | null
): boolean {
  return (
    ENABLE_MULTI_PROVIDER_CREATE &&
    isMultiProvider(generalSettings) &&
    (formMode == null ||
      formMode === ResilienceFormMode.GUIDED ||
      formMode === ResilienceFormMode.EXPERT_MODE)
  );
}

/** @deprecated Prefer {@link isMultiProviderPlacement} (includes Expert multi). */
export function isMultiProviderGuided(
  generalSettings?: Pick<GeneralSettingsProps, 'providerConfigurations'> | null,
  formMode?: ResilienceFormMode | null
): boolean {
  return isMultiProviderPlacement(generalSettings, formMode);
}

export function stampRegionProviderUuid(region: Region, providerUuid: string): Region {
  return { ...region, providerUuid };
}

export function flattenPlacementRegions(
  placementsByProvider?: Record<string, ResilienceAndRegionsProps> | null
): Region[] {
  if (!placementsByProvider) return [];
  const regions: Region[] = [];
  for (const [providerUuid, slice] of Object.entries(placementsByProvider)) {
    for (const region of slice?.[REGIONS_FIELD] ?? []) {
      regions.push(
        region.providerUuid ? region : stampRegionProviderUuid(region, providerUuid)
      );
    }
  }
  return regions;
}

export function regionsByProviderFromPlacements(
  placementsByProvider?: Record<string, ResilienceAndRegionsProps> | null,
  providers?: ProviderType[] | null
): RegionsByProvider {
  const result: RegionsByProvider = {};
  for (const provider of providers ?? []) {
    if (!provider?.uuid) continue;
    const stored = placementsByProvider?.[provider.uuid]?.[REGIONS_FIELD] ?? [];
    result[provider.uuid] = stored.map((region) =>
      stampRegionProviderUuid(region, provider.uuid)
    );
  }
  return result;
}

/** Only `regions` is meaningful on ByProvider entries (FT/RF stay on the singular slice). */
export function buildPlacementsByProvider(
  regionsByProvider: RegionsByProvider | null | undefined,
  providers: ProviderType[]
): Record<string, ResilienceAndRegionsProps> {
  const result: Record<string, ResilienceAndRegionsProps> = {};
  for (const provider of providers) {
    if (!provider?.uuid) continue;
    const regions = (regionsByProvider?.[provider.uuid] ?? []).map((region) =>
      stampRegionProviderUuid(region, provider.uuid)
    );
    result[provider.uuid] = {
      ...emptyPlacementSlice(),
      [REGIONS_FIELD]: regions
    };
  }
  return result;
}

/**
 * Regions for placement readers: ByProvider when present with any regions, else singular.
 */
export function getEffectiveRegions(
  resilienceAndRegionsSettings?: Pick<ResilienceAndRegionsProps, typeof REGIONS_FIELD> | null,
  placementsByProvider?: Record<string, ResilienceAndRegionsProps> | null
): Region[] {
  const fromByProvider = flattenPlacementRegions(placementsByProvider);
  if (fromByProvider.length > 0) return fromByProvider;
  return resilienceAndRegionsSettings?.[REGIONS_FIELD] ?? [];
}

export function withEffectiveRegions(
  resilienceAndRegionsSettings: ResilienceAndRegionsProps,
  placementsByProvider?: Record<string, ResilienceAndRegionsProps> | null
): ResilienceAndRegionsProps {
  return {
    ...resilienceAndRegionsSettings,
    [REGIONS_FIELD]: getEffectiveRegions(resilienceAndRegionsSettings, placementsByProvider)
  };
}

export function flattenRegionsByProvider(
  regionsByProvider: RegionsByProvider | null | undefined
): Region[] {
  if (!regionsByProvider) return [];
  return Object.entries(regionsByProvider).flatMap(([providerUuid, regions]) =>
    (regions ?? []).map((region) => stampRegionProviderUuid(region, providerUuid))
  );
}

export function providerUuidsNeedingRegion(
  providers: ProviderType[],
  regionsByProvider: RegionsByProvider | null | undefined
): string[] {
  return providers
    .filter((provider) => (regionsByProvider?.[provider.uuid] ?? []).length === 0)
    .map((provider) => provider.uuid);
}
