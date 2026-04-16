export const AZURE_FORM_MAPPERS = {
  '$.details.cloudInfo.azu.azuClientId': 'azuClientId', //string;
  '$.details.cloudInfo.azu.azuClientSecret': 'azuClientSecret', //string;
  '$.details.cloudInfo.azu.azuHostedZoneId': 'azuHostedZoneId', // string;
  '$.details.cloudInfo.azu.azuRG': 'azuRG', //string;
  '$.details.cloudInfo.azu.azuNetworkRG': 'azuNetworkRG', // string;
  '$.details.cloudInfo.azu.azuSubscriptionId': 'azuSubscriptionId', // string;
  '$.details.cloudInfo.azu.azuNetworkSubscriptionId': 'azuNetworkSubscriptionId', // string;
  '$.details.cloudInfo.azu.azuTenantId': 'azuTenantId', // string;
  '$.details.airGapInstall': 'dbNodePublicInternetAccess', //boolean;
  '$.allAccessKeys[0].keyInfo.sshPrivateKeyContent': 'sshPrivateKeyContent', // File;
  '$.allAccessKeys[0].keyInfo.keyPairName': 'sshKeypairName',
  '$.details.setUpChrony.sshPort': 'sshPort',
  '$.details.setUpChrony.sshUser': 'sshUser',
  '$.name': 'providerName', // string;
  '$.regions': 'regions', // CloudVendorRegionField[];
  '$.details.ntpServers': 'ntpServers', // string[];
  '$.imageBundles': 'imageBundles' //ImageBundle[];
};
