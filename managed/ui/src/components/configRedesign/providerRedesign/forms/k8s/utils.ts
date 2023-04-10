import { SuggestedKubernetesConfig } from '../../../../../redesign/helpers/dtos';
import { KubernetesProviderLabel, ProviderCode } from '../../constants';
import { getRegionlabel } from '../configureRegion/utils';
import { generateLowerCaseAlphanumericId } from '../utils';

export const adaptSuggestedKubernetesConfig = (
  suggestedKubernetesConfig: SuggestedKubernetesConfig
) => {
  const {
    config: {
      KUBECONFIG_IMAGE_REGISTRY,
      KUBECONFIG_PROVIDER,
      KUBECONFIG_PULL_SECRET_CONTENT,
      KUBECONFIG_PULL_SECRET_NAME
    },
    regionList,
    name: providerName
  } = suggestedKubernetesConfig;

  const kubernetesPullSecretContent = new File(
    [KUBECONFIG_PULL_SECRET_CONTENT],
    KUBECONFIG_PULL_SECRET_NAME,
    {
      type: 'text/plain',
      lastModified: new Date().getTime()
    }
  );
  const kubernetesProvider = KUBECONFIG_PROVIDER.toLowerCase();
  const regions = regionList.map((region) => ({
    fieldId: generateLowerCaseAlphanumericId(),
    code: region.code,
    regionData: {
      value: { code: region.code, zoneOptions: [] },
      label: getRegionlabel(ProviderCode.KUBERNETES, region.code)
    },
    zones: region.zoneList.map((zone) => ({
      code: zone.name,
      kubernetesStorageClass: zone.config.STORAGE_CLASS
    }))
  }));

  return {
    providerName: providerName,
    kubernetesPullSecretContent: kubernetesPullSecretContent,
    kubernetesImageRegistry: KUBECONFIG_IMAGE_REGISTRY,
    kubernetesProvider: {
      label: KubernetesProviderLabel[kubernetesProvider],
      value: kubernetesProvider
    },
    regions: regions
  };
};
