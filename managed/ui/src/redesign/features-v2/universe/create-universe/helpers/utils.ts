import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import {
  RunTimeConfig,
  RunTimeConfigEntry
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { InstanceTag } from '../steps/advanced-settings/dtos';

// dev_tags is declared as a string list, but the value used (application.conf, v1 form) is
// JSON [{name, value}]; anything else yields no tags rather than a broken form.
export const getDevUserTags = (runtimeConfigs?: RunTimeConfig): InstanceTag[] => {
  const getValue = (key: string) =>
    runtimeConfigs?.configEntries?.find((c: RunTimeConfigEntry) => c.key === key)?.value;
  if (getValue(RuntimeConfigKey.IS_TAGS_ENFORCED) !== 'true') return [];
  try {
    const parsed = JSON.parse(getValue(RuntimeConfigKey.DEFAULT_DEV_TAGS) ?? '[]');
    if (!Array.isArray(parsed)) return [];
    return parsed
      .filter(
        (tag) =>
          typeof tag?.name === 'string' &&
          typeof tag?.value === 'string' &&
          tag.name !== '' &&
          tag.value !== ''
      )
      .map(({ name, value }) => ({ name, value }));
  } catch {
    return [];
  }
};

export const useRuntimeConfigValues = (providerUUID?: string) => {
  const currentCustomer = useSelector((state: any) => state.customer.currentCustomer);
  const customerUUID = currentCustomer?.data?.uuid;

  const { data: runtimeConfigs, isLoading: isRuntimeConfigLoading } = useQuery(
    [QUERY_KEY.fetchCustomerRunTimeConfigs, customerUUID],
    () => api.fetchRunTimeConfigs(true, customerUUID),
    { enabled: !!customerUUID }
  );

  const { data: providerRuntimeConfigs, isLoading: isProviderRuntimeConfigLoading } = useQuery(
    [QUERY_KEY.fetchProviderRunTimeConfigs, providerUUID],
    () => api.fetchRunTimeConfigs(true, providerUUID),
    { enabled: !!providerUUID }
  );

  const getConfigValue = (key: string): string | undefined =>
    runtimeConfigs?.configEntries?.find((c: RunTimeConfigEntry) => c.key === key)?.value;

  const osPatchingEnabled = getConfigValue('yb.provider.vm_os_patching') === 'true';

  const useK8CustomResources = getConfigValue('yb.use_k8s_custom_resources') === 'true';

  const canUseSpotInstance = getConfigValue('yb.use_spot_instances') === 'true';

  const parsedMaxVolumeCount = Number(getConfigValue('yb.max_volume_count') ?? 0);

  const maxVolumeCount =
    Number.isFinite(parsedMaxVolumeCount) && parsedMaxVolumeCount > 0 ? parsedMaxVolumeCount : 32;

  const ebsVolumeEnabled = getConfigValue('yb.universe.allow_cloud_volume_encryption') === 'true';

  const enableAzOverridesK8s = getConfigValue(RuntimeConfigKey.ENABLE_AZ_OVERRIDES_K8S) === 'true';

  const enablePrimaryRf9 = getConfigValue(RuntimeConfigKey.ENABLE_PRIMARY_RF_9) === 'true';

  return {
    runtimeConfigs,
    providerRuntimeConfigs,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading,
    osPatchingEnabled,
    useK8CustomResources,
    maxVolumeCount,
    canUseSpotInstance,
    ebsVolumeEnabled,
    enableAzOverridesK8s,
    enablePrimaryRf9
  };
};
