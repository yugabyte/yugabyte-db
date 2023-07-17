import { SuggestedKubernetesConfig } from '../../../../../redesign/helpers/dtos';
import { KubernetesProvider, KubernetesProviderLabel } from '../../constants';
import { K8sRegionField } from '../configureRegion/ConfigureK8sRegionModal';
import { K8sCertIssuerType } from '../configureRegion/constants';
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
  const regions = regionList.map<K8sRegionField>((region) => ({
    fieldId: generateLowerCaseAlphanumericId(),
    code: region.code,
    name: region.name || region.code,
    regionData: {
      value: { code: region.code, zoneOptions: [] },
      label: region.name || region.code
    },
    zones: region.zoneList.map((zone) => ({
      code: zone.name,
      kubernetesStorageClass: zone.config.STORAGE_CLASS,
      certIssuerType: K8sCertIssuerType.NONE
    }))
  }));

  return {
    providerName: providerName,
    kubernetesPullSecretContent: kubernetesPullSecretContent,
    kubernetesImageRegistry: KUBECONFIG_IMAGE_REGISTRY,
    kubernetesProvider: {
      label: KubernetesProviderLabel[kubernetesProvider as KubernetesProvider],
      value: kubernetesProvider as KubernetesProvider
    },
    regions: regions
  };
};
