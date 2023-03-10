export const RegionOperation = {
  ADD: 'add',
  EDIT: 'edit'
} as const;
export type RegionOperation = typeof RegionOperation[keyof typeof RegionOperation];

export const K8sRegionFieldLabel = {
  CERT_ISSUER_TYPE: 'Cert-Manager Issuer Type',
  CERT_ISSUER_NAME: 'Cert-Manager Issuer Name',
  KUBE_CONFIG_CONTENT: 'Kube Config',
  KUBE_DOMAIN: 'Kube Domain',
  STORAGE_CLASSES: 'Storage Classes',
  KUBE_NAMESPACE: 'Kube Namespace',
  KUBE_POD_ADDRESS_TEMPLATE: 'Kube Pod Address Template',
  OVERRIDES: 'Overrides',
  REGION: 'Region',
  ZONE_CODE: 'Zone Code'
} as const;

export const K8sCertIssuerType = {
  CLUSTER_ISSUER: 'clusterIssuer',
  ISSUER: 'issuer',
  NONE: 'none'
} as const;
export type K8sCertIssuerType = typeof K8sCertIssuerType[keyof typeof K8sCertIssuerType];

// User Facing Label
export const K8sCertIssuerTypeLabel = {
  [K8sCertIssuerType.CLUSTER_ISSUER]: 'Cluster Issuer',
  [K8sCertIssuerType.ISSUER]: 'Issuer',
  [K8sCertIssuerType.NONE]: 'None'
};

export const OnPremRegionFieldLabel = {
  CODE: 'Region Name',
  ZONE_NAME: 'Zone Name'
} as const;
