import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { RunTimeConfigEntry } from '@app/redesign/features/universe/universe-form/utils/dto';

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

  const maxVolumeCount = Number(getConfigValue('yb.max_volume_count') ?? 0);

  const ebsVolumeEnabled = getConfigValue('yb.universe.allow_cloud_volume_encryption') === 'true';

  return {
    runtimeConfigs,
    providerRuntimeConfigs,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading,
    osPatchingEnabled,
    useK8CustomResources,
    maxVolumeCount,
    canUseSpotInstance,
    ebsVolumeEnabled
  };
};
