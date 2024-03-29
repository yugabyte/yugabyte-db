import { GCPProviderCreateFormFieldValues } from "./GCPProviderCreateForm";

export const GCPCreateFormErrFields : Record<string , keyof GCPProviderCreateFormFieldValues>  = {
    "$.details.cloudInfo.gcp.useHostCredentials" :  "dbNodePublicInternetAccess",
    "$.details.cloudInfo.gcp.destVpcId": "destVpcId",
    "$.details.sshPort": "sshPort",
    "$.details.cloudInfo.gcp.ybFirewallTags" : "ybFirewallTags",
    '$.allAccessKeys[0].keyInfo.sshPrivateKeyContent': 'sshPrivateKeyContent',
    '$.allAccessKeys[0].keyInfo.keyPairName': 'sshKeypairName',
    '$.details.setUpChrony.sshPort': 'sshPort',
    '$.details.setUpChrony.sshUser': 'sshUser',
    '$.name': 'providerName', 
    '$.regions': 'regions',
    '$.details.ntpServers': 'ntpServers',
    '$.imageBundles': 'imageBundles'
};
