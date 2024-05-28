/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import {
  KubernetesProvider,
  KubernetesProviderLabel,
  KubernetesProviderType,
  KUBERNETES_PROVIDERS_MAP
} from '../../constants';

const convertToOptions = (kubernetesProvider: readonly KubernetesProvider[], isDisabled = false) =>
  kubernetesProvider.map((kubernetesProvider) => ({
    value: kubernetesProvider,
    label: KubernetesProviderLabel[kubernetesProvider],
    isDisabled: isDisabled
  }));

export const KUBERNETES_PROVIDER_OPTIONS = {
  [KubernetesProviderType.MANAGED_SERVICE]: convertToOptions(
    KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.MANAGED_SERVICE]
  ),
  [KubernetesProviderType.DEPRECATED]: convertToOptions(
    KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.DEPRECATED],
    true // Disable selecting deprecated options
  ),
  [KubernetesProviderType.OPEN_SHIFT]: convertToOptions(
    KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.OPEN_SHIFT]
  ),
  [KubernetesProviderType.TANZU]: convertToOptions(
    KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.TANZU]
  )
} as const;

export const QUAY_IMAGE_REGISTRY = 'quay.io/yugabyte/yugabyte';
export const REDHAT_IMAGE_REGISTRY = 'registry.connect.redhat.com/yugabytedb/yugabyte';

export const K8S_FORM_MAPPERS = {
  '$.name': 'providerName',
  '$.details.cloudInfo.kubernetes.kubernetesImageRegistry': 'kubernetesImageRegistry',
  '$.details.cloudInfo.kubernetes.kubernetesProvider': 'kubernetesProvider',
  '$.details.cloudInfo.kubernetes.kubeConfigName': 'kubeConfigName',
  '$.details.cloudInfo.kubernetes.kubeConfigContent': 'kubeConfigContent',
  '$.details.cloudInfo.kubernetes.kubernetesPullSecretContent': 'kubernetesPullSecretContent',
  '$.details.cloudInfo.kubernetes.kubernetesPullSecretName': 'kubernetesPullSecretName',
  '$.details.airGapInstall': 'dbNodePublicInternetAccess',
  '$.regions': 'regions'
};
