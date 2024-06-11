import { OptionProps } from '../../../redesign/components';
import { RegionOperation } from './forms/configureRegion/constants';

export const CONFIG_ROUTE_PREFIX = 'config';

export const ConfigTabKey = {
  INFRA: 'infra',
  BACKUP: 'backup',
  BACKUP_NEW: 'newBackupConfig',
  SECURITY: 'security',
  TROUBLESHOOT: 'troubleshoot',
} as const;
export type ConfigTabKey = typeof ConfigTabKey[keyof typeof ConfigTabKey];

/**
 * Values correspond to the provider codes expected on the the YBA backend.
 */
export const ProviderCode = {
  UNKNOWN: 'unknown',
  AWS: 'aws',
  GCP: 'gcp',
  AZU: 'azu',
  DOCKER: 'docker',
  ON_PREM: 'onprem',
  KUBERNETES: 'kubernetes',
  CLOUD: 'cloud-1',
  OTHER: 'other'
} as const;
export type ProviderCode = typeof ProviderCode[keyof typeof ProviderCode];

export const CloudVendorProviders = [ProviderCode.AWS, ProviderCode.AZU, ProviderCode.GCP] as const;

// `KubernetesProviderType` and `KubernetesProviderTab` are required because we need to support
// 3 kubernetes provider tabs
// - Tanzu
// - OpenShift
// - Managed Kubernetes Service (GKE, AKS, EKS, Custom)
// in addition to the limited support for deprecated kubernetes providers like PKS
export const KubernetesProviderType = {
  TANZU: 'k8sTanzu',
  OPEN_SHIFT: 'k8sOpenshift',
  MANAGED_SERVICE: 'k8sManagedService',
  DEPRECATED: 'k8sDeprecated'
} as const;
export type KubernetesProviderType = typeof KubernetesProviderType[keyof typeof KubernetesProviderType];

export type KubernetesProviderTab = Exclude<
  KubernetesProviderType,
  typeof KubernetesProviderType.DEPRECATED
>;

export const ArchitectureType = {
  X86_64: 'x86_64',
  ARM64: 'aarch64'
} as const;
export type ArchitectureType = typeof ArchitectureType[keyof typeof ArchitectureType];

/**
 * Maps to 'usabilityState' enum from the backend.
 *
 * Reference: managed/src/main/java/com/yugabyte/yw/models/Provider.java
 */
export const ProviderStatus = {
  READY: 'READY',
  UPDATING: 'UPDATING',
  ERROR: 'ERROR',
  DELETING: 'DELETING'
} as const;
export type ProviderStatus = typeof ProviderStatus[keyof typeof ProviderStatus];

export const TRANSITORY_PROVIDER_STATUSES = [
  ProviderStatus.UPDATING,
  ProviderStatus.DELETING
] as const;

// --------------------------------------------------------------------------------------
// Route Constants
// --------------------------------------------------------------------------------------
export const PROVIDER_ROUTE_PREFIX = `${CONFIG_ROUTE_PREFIX}/${ConfigTabKey.INFRA}`;

// --------------------------------------------------------------------------------------
// Provider Field & Form Constants
// --------------------------------------------------------------------------------------

export const YBImageType = {
  X86_64: ArchitectureType.X86_64,
  ARM64: ArchitectureType.ARM64,
  CUSTOM_AMI: 'customAMI'
} as const;
export type YBImageType = typeof YBImageType[keyof typeof YBImageType];

export const NTPSetupType = {
  CLOUD_VENDOR: 'cloudVendor',
  SPECIFIED: 'specified',
  NO_NTP: 'noNTP'
} as const;
export type NTPSetupType = typeof NTPSetupType[keyof typeof NTPSetupType];

export const VPCSetupType = {
  EXISTING: 'EXISTING',
  HOST_INSTANCE: 'HOSTVPC',
  NEW: 'NEW'
} as const;
export type VPCSetupType = typeof VPCSetupType[keyof typeof VPCSetupType];

export const KeyPairManagement = {
  YBA_MANAGED: 'YBAManaged',
  SELF_MANAGED: 'SelfManaged',
  UNKNOWN: 'Unknown'
} as const;
export type KeyPairManagement = typeof KeyPairManagement[keyof typeof KeyPairManagement];

export const KEY_PAIR_MANAGEMENT_OPTIONS: OptionProps[] = [
  {
    value: KeyPairManagement.YBA_MANAGED,
    label: 'Use YugabyteDB Anywhere to manage key pairs'
  },
  {
    value: KeyPairManagement.SELF_MANAGED,
    label: 'Provide custom key pair information'
  }
];

export const KubernetesProvider = {
  AKS: 'aks',
  CUSTOM: 'custom',
  EKS: 'eks',
  GKE: 'gke',
  OPEN_SHIFT: 'openshift',
  PKS: 'pks',
  TANZU: 'tanzu'
} as const;
export type KubernetesProvider = typeof KubernetesProvider[keyof typeof KubernetesProvider];

export const KUBERNETES_PROVIDERS_MAP = {
  [KubernetesProviderType.DEPRECATED]: [KubernetesProvider.PKS],
  [KubernetesProviderType.MANAGED_SERVICE]: [
    KubernetesProvider.AKS,
    KubernetesProvider.CUSTOM,
    KubernetesProvider.EKS,
    KubernetesProvider.GKE
  ],
  [KubernetesProviderType.OPEN_SHIFT]: [KubernetesProvider.OPEN_SHIFT],
  [KubernetesProviderType.TANZU]: [KubernetesProvider.TANZU]
} as const;

export const SUPPORTED_KUBERNETES_PROVIDERS = [
  ...KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.MANAGED_SERVICE],
  ...KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.OPEN_SHIFT],
  ...KUBERNETES_PROVIDERS_MAP[KubernetesProviderType.TANZU]
];

export const ProviderOperation = {
  CREATE: 'create',
  EDIT: 'edit'
} as const;
export type ProviderOperation = typeof ProviderOperation[keyof typeof ProviderOperation];

export const InstanceTypeOperation = {
  ADD: 'add',
  EDIT: 'edit'
} as const;
export type InstanceTypeOperation = typeof InstanceTypeOperation[keyof typeof InstanceTypeOperation];

export const DEFAULT_SSH_PORT = 22;
export const DEFAULT_NODE_EXPORTER_PORT = 9300;
export const DEFAULT_NODE_EXPORTER_USER = 'prometheus';

export const AWSValidationKey = {
  ACCESS_KEY_CREDENTIALS: 'KEYS',
  IAM_CREDENTIALS: 'IAM',
  SSH_PRIVATE_KEY_CONTENT: 'SSH_PRIVATE_KEY_CONTENT',
  NTP_SERVERS: 'NTP_SERVERS',
  HOSTED_ZONE_ID: 'HOSTED_ZONE',
  REGION: 'REGION'
} as const;
export type AWSValidationKey = typeof AWSValidationKey[keyof typeof AWSValidationKey];

// --------------------------------------------------------------------------------------
// User Facing Labels
// --------------------------------------------------------------------------------------
export const ProviderLabel = {
  [ProviderCode.AWS]: 'AWS',
  [ProviderCode.AZU]: 'AZU',
  [ProviderCode.GCP]: 'GCP',
  [ProviderCode.KUBERNETES]: 'Kubernetes',
  [ProviderCode.ON_PREM]: 'On Prem'
} as const;

export const ProviderStatusLabel = {
  [ProviderStatus.ERROR]: 'Error',
  [ProviderStatus.READY]: 'Ready',
  [ProviderStatus.UPDATING]: 'Updating',
  [ProviderStatus.DELETING]: 'Deleting'
};

export const NTPSetupTypeLabel = {
  [NTPSetupType.SPECIFIED]: 'Specify Custom NTP Server(s)',
  [NTPSetupType.NO_NTP]: 'Assume NTP server configured in machine image', // Assume NTP server configured in machine image
  [NTPSetupType.CLOUD_VENDOR]: (providerCode: string) =>
    `Use ${ProviderLabel[providerCode]}'s NTP Server` // Use {Cloud Vendor}'s NTP Server
} as const;

export const VPCSetupTypeLabel = {
  [VPCSetupType.EXISTING]: 'Specify an existing VPC',
  [VPCSetupType.HOST_INSTANCE]: 'Use VPC from YBA host instance',
  [VPCSetupType.NEW]: 'Create a new VPC (Beta)'
};

export const KubernetesProviderLabel = {
  [KubernetesProvider.AKS]: 'Azure Kubernetes Service',
  [KubernetesProvider.CUSTOM]: 'Custom Kubernetes Service',
  [KubernetesProvider.EKS]: 'Elastic Kubernetes Service',
  [KubernetesProvider.GKE]: 'Google Kubernetes Engine',
  [KubernetesProvider.OPEN_SHIFT]: 'Red Hat OpenShift',
  [KubernetesProvider.PKS]: 'Pivotal Container Service (Deprecated)',
  [KubernetesProvider.TANZU]: 'VMware Tanzu'
} as const;

export const KubernetesProviderTypeLabel = {
  [KubernetesProviderType.MANAGED_SERVICE]: 'Managed Kubernetes Service',
  [KubernetesProviderType.OPEN_SHIFT]: 'Red Hat OpenShift',
  [KubernetesProviderType.TANZU]: 'VMware Tanzu'
} as const;

export const RegionOperationLabel = {
  [RegionOperation.ADD]: 'Add',
  [RegionOperation.EDIT_EXISTING]: 'Edit',
  [RegionOperation.EDIT_NEW]: 'Edit',
  [RegionOperation.VIEW]: 'View'
};

export const InstanceTypeOperationLabel = {
  [InstanceTypeOperation.ADD]: 'Add',
  [InstanceTypeOperation.EDIT]: 'Edit'
} as const;

// --------------------------------------------------------------------------------------
// Data Refetching Constants
// --------------------------------------------------------------------------------------
export const PROVIDER_CONFIG_REFETCH_INTERVAL_MS = 30_000;

export const AzuProviderCredentialType = {
  HOST_INSTANCE_MI: 'hostInstanceMI',
  SPECIFIED_SERVICE_PRINCIPAL: 'specifiedServicePrincipal'
} as const;
export type AzuProviderCredentialType = typeof AzuProviderCredentialType[keyof typeof AzuProviderCredentialType];
