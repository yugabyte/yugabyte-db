import {
  HOSTNAME_REGEX,
  IP_V4_REGEX,
  IP_V6_REGEX
} from '../../../config/PublicCloud/views/NTPConfig';
import { ProviderCode } from '../constants';
import { AWSProviderEditFormFieldValues } from './aws/AWSProviderEditForm';
import { AZUProviderEditFormFieldValues } from './azu/AZUProviderEditForm';
import { GCPProviderEditFormFieldValues } from './gcp/GCPProviderEditForm';
import { K8sProviderEditFormFieldValues } from './k8s/K8sProviderEditForm';
import { OnPremProviderEditFormFieldValues } from './onPrem/OnPremProviderEditForm';

export const NTP_SERVER_REGEX = new RegExp(
  `(${IP_V4_REGEX.source})|(${IP_V6_REGEX.source})|(${HOSTNAME_REGEX.source})`
);

export const NonEditableInUseProviderField: {
  [ProviderCode.AWS]: (keyof AWSProviderEditFormFieldValues)[];
  [ProviderCode.AZU]: (keyof AZUProviderEditFormFieldValues)[];
  [ProviderCode.GCP]: (keyof GCPProviderEditFormFieldValues)[];
  [ProviderCode.KUBERNETES]: (keyof K8sProviderEditFormFieldValues)[];
  [ProviderCode.ON_PREM]: (keyof OnPremProviderEditFormFieldValues)[];
} = {
  [ProviderCode.AWS]: [
    'enableHostedZone',
    'hostedZoneId',
    'vpcSetupType',
    'sshUser',
    'sshPort',
    'dbNodePublicInternetAccess',
    'ntpSetupType',
    'ntpServers'
  ],
  [ProviderCode.AZU]: [
    'azuTenantId',
    'azuClientId',
    'azuSubscriptionId',
    'azuNetworkSubscriptionId',
    'azuRG',
    'azuNetworkRG',
    'azuHostedZoneId',
    'sshUser',
    'sshPort',
    'dbNodePublicInternetAccess',
    'ntpSetupType',
    'ntpServers'
  ],
  [ProviderCode.GCP]: [
    'gceProject',
    'sharedVPCProject',
    'vpcSetupType',
    'destVpcId',
    'sshUser',
    'sshPort',
    'ybFirewallTags',
    'dbNodePublicInternetAccess',
    'ntpSetupType',
    'ntpServers'
  ],
  [ProviderCode.KUBERNETES]: [
    'kubernetesProvider',
    'kubernetesImageRegistry',
    'editPullSecretContent',
    'kubernetesPullSecretContent'
  ],
  [ProviderCode.ON_PREM]: [
    'sshUser',
    'sshPort',
    'dbNodePublicInternetAccess',
    'ntpSetupType',
    'ntpServers',
    'nodeExporterPort',
    'nodeExporterUser',
    'installNodeExporter',
    'ybHomeDir'
  ]
};
