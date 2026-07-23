export const OCID_REGEX = /^ocid1\.[\w.-]+\.[\w.-]+\.[\w.-]+$/;

export const OCI_FINGERPRINT_REGEX = /^([0-9a-fA-F]{2}:){15}[0-9a-fA-F]{2}$/;

export const OciAuthType = {
  API_KEY: 'API_KEY',
  INSTANCE_PRINCIPAL: 'INSTANCE_PRINCIPAL'
} as const;
export type OciAuthType = (typeof OciAuthType)[keyof typeof OciAuthType];

export const OCI_AUTH_TYPE_OPTIONS = [
  { value: OciAuthType.API_KEY, label: 'API Key' },
  { value: OciAuthType.INSTANCE_PRINCIPAL, label: 'Instance Principal' }
];

export const OCI_FORM_MAPPERS = {
  '$.details.cloudInfo.oci.ociAuthType': 'ociAuthType',
  '$.details.cloudInfo.oci.ociTenancyId': 'ociTenancyId',
  '$.details.cloudInfo.oci.ociUserId': 'ociUserId',
  '$.details.cloudInfo.oci.ociFingerprint': 'ociFingerprint',
  '$.details.cloudInfo.oci.ociPrivateKeyContent': 'ociPrivateKeyContent',
  '$.details.cloudInfo.oci.ociCompartmentId': 'ociCompartmentId',
  '$.details.cloudInfo.oci.ociRegion': 'ociRegionData',
  '$.details.cloudInfo.oci.ociHostedZoneId': 'ociHostedZoneId',
  '$.details.airGapInstall': 'dbNodePublicInternetAccess',
  '$.allAccessKeys[0].keyInfo.sshPrivateKeyContent': 'sshPrivateKeyContent',
  '$.allAccessKeys[0].keyInfo.keyPairName': 'sshKeypairName',
  '$.details.setUpChrony.sshPort': 'sshPort',
  '$.details.setUpChrony.sshUser': 'sshUser',
  '$.name': 'providerName',
  '$.regions': 'regions',
  '$.details.ntpServers': 'ntpServers',
  '$.imageBundles': 'imageBundles'
};
