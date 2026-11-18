import { CloudType } from '../../../../features/universe/universe-form/utils/dto';
import { GeneralSettingsProps, ProviderType } from '../steps/general-settings/dtos';

/** Max selected provider configs in create-universe (multi-provider). */
export const MAX_PROVIDER_CONFIGURATIONS = 3;

/**
 * Code-only gate for General Settings multi-provider UI (PLAT-22499).
 * Flip to true in source to enable; not an ops runtime config.
 */
export const ENABLE_MULTI_PROVIDER_CREATE = false;

export function getProviderConfigurations(
  generalSettings?: Pick<GeneralSettingsProps, 'providerConfigurations'> | null
): ProviderType[] {
  return generalSettings?.providerConfigurations ?? [];
}

export function getPrimaryProvider(
  generalSettings?: Pick<GeneralSettingsProps, 'providerConfigurations'> | null
): ProviderType | undefined {
  return getProviderConfigurations(generalSettings)[0];
}

/** Cloud codes from the General `cloud` field (string or string[]). */
export function cloudCodes(cloud: GeneralSettingsProps['cloud'] | null | undefined): string[] {
  if (Array.isArray(cloud)) return cloud.filter(Boolean);
  return cloud ? [cloud] : [];
}

/** Primary cloud code for N=1 call sites and shared steps. */
export function primaryCloudCode(
  generalSettings?: Pick<GeneralSettingsProps, 'providerConfigurations' | 'cloud'> | null
): string | undefined {
  return getPrimaryProvider(generalSettings)?.code ?? cloudCodes(generalSettings?.cloud)[0];
}

export function isMultiProvider(
  generalSettings?: Pick<GeneralSettingsProps, 'providerConfigurations'> | null
): boolean {
  return getProviderConfigurations(generalSettings).length >= 2;
}

export function providerCountBand(count: number): 'single' | 'multi' {
  return count >= 2 ? 'multi' : 'single';
}

/** True when selected provider count crosses the single ↔ multi boundary. */
export function crossedProviderCountBoundary(previousCount: number, nextCount: number): boolean {
  return providerCountBand(previousCount) !== providerCountBand(nextCount);
}

export function includesKubernetesCloud(cloud: GeneralSettingsProps['cloud'] | null | undefined): boolean {
  return cloudCodes(cloud).includes(CloudType.kubernetes);
}

/**
 * Normalize a provider list: drop empties/duplicates (first wins), cap at max.
 * Order is selection order.
 */
export function normalizeProviderConfigurations(
  providers: Array<ProviderType | null | undefined> | null | undefined
): ProviderType[] {
  const seen = new Set<string>();
  const result: ProviderType[] = [];
  for (const provider of providers ?? []) {
    if (!provider?.uuid || seen.has(provider.uuid)) continue;
    seen.add(provider.uuid);
    result.push(provider);
    if (result.length >= MAX_PROVIDER_CONFIGURATIONS) break;
  }
  return result;
}

/**
 * One ProviderConfiguration per cloud code, ordered by `clouds` (card selection order).
 * Keeps a still-valid existing config; otherwise leaves a hole for the caller to fill.
 */
export function providersOrderedByClouds(
  clouds: string[],
  current: Array<ProviderType | null | undefined> | null | undefined
): ProviderType[] {
  const byCode = new Map<string, ProviderType>();
  for (const provider of current ?? []) {
    if (!provider?.uuid || !provider.code || byCode.has(provider.code)) continue;
    byCode.set(provider.code, provider);
  }
  return normalizeProviderConfigurations(clouds.map((code) => byCode.get(code)));
}
