/**
 * ```
 * `
 * ADD - Add a region to the provider
 * EDIT_NEW - Edit a region which is not yet saved on the DB
 * EDIT_EXISTING - Edit a region which was saved on the DB
 * VIEW - View a region (Readonly)
 * `
 * ```
 */
export const RegionOperation = {
  ADD: 'add',
  EDIT_NEW: 'editNew',
  EDIT_EXISTING: 'editExisting',
  VIEW: 'view'
} as const;
export type RegionOperation = typeof RegionOperation[keyof typeof RegionOperation];

export const K8sRegionFieldLabel = {
  CERT_ISSUER_NAME: 'Cert-Manager Issuer Name',
  CERT_ISSUER_GROUP: 'Cert-Manager Issuer Group (Optional)',
  CERT_ISSUER_KIND: 'Cert-Manager Issuer Kind',
  KUBE_CONFIG_CONTENT: 'Kube Config (Optional)',
  KUBE_DOMAIN: 'Kube Domain (Optional)',
  STORAGE_CLASSES: 'Storage Classes (Optional)',
  KUBE_NAMESPACE: 'Kube Namespace (Optional)',
  KUBE_POD_ADDRESS_TEMPLATE: 'Kube Pod Address Template (Optional)',
  OVERRIDES: 'Overrides (Optional)',
  REGION: 'Region',
  ZONE_CODE: 'Zone Code',
  CURRENT_KUBE_CONFIG_FILEPATH: 'Current Kube Config Filepath',
  EDIT_KUBE_CONFIG: 'Edit Kube Config'
} as const;

export const OnPremRegionFieldLabel = {
  CODE: 'Region Name',
  LOCATION: 'Location',
  LONGITUDE: 'Longitude',
  LATITUDE: 'Latitude',
  ZONE_NAME: 'Zone Name'
} as const;
