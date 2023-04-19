import { OptionProps } from '../../../../../redesign/components';
import { AWSValidationKey, VPCSetupType, VPCSetupTypeLabel } from '../../constants';

export const AWS_INVALID_FIELDS = {
  [AWSValidationKey.ACCESS_KEY_CREDENTIALS]: ['accessKeyId', 'secretAccessKey'],
  [AWSValidationKey.HOSTED_ZONE_ID]: ['hostedZoneId'],
  [AWSValidationKey.IAM_CREDENTIALS]: ['providerCredentialType'],
  [AWSValidationKey.NTP_SERVERS]: ['ntpServers'],
  [AWSValidationKey.SSH_PRIVATE_KEY_CONTENT]: ['sshPrivateKeyContent'],
  [AWSValidationKey.REGION]: ['regions']
} as const;

export const AWSProviderCredentialType = {
  HOST_INSTANCE_IAM_ROLE: 'hostInstanceIAMRole',
  ACCESS_KEY: 'accessKey'
} as const;
export type AWSProviderCredentialType = typeof AWSProviderCredentialType[keyof typeof AWSProviderCredentialType];

export const VPC_SETUP_OPTIONS: OptionProps[] = [
  {
    value: VPCSetupType.EXISTING,
    label: VPCSetupTypeLabel[VPCSetupType.EXISTING]
  },
  {
    value: VPCSetupType.NEW,
    label: VPCSetupTypeLabel[VPCSetupType.NEW],
    disabled: true
  }
];
