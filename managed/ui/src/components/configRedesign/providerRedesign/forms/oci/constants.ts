export const OCID_REGEX = /^ocid1\.[\w.-]+\.[\w.-]+\.[\w.-]+$/;

export const OCI_FINGERPRINT_REGEX = /^([0-9a-fA-F]{2}:){15}[0-9a-fA-F]{2}$/;

export const OCI_FORM_MAPPERS = {
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
